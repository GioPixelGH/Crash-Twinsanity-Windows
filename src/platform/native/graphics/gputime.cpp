#include "gputime.h"

#include "native.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>

namespace NativeGraphics
{
namespace
{
using GLenum = unsigned int;
using GLuint = unsigned int;
using GLint = int;
using GLsizei = int;
using GLbitfield = unsigned int;
using GLuint64 = std::uint64_t;
constexpr GLenum GL_TIME_ELAPSED = 0x88BF;
constexpr GLenum GL_QUERY_COUNTER_BITS = 0x8864;
constexpr GLenum GL_QUERY_RESULT = 0x8866;
constexpr GLenum GL_QUERY_RESULT_AVAILABLE = 0x8867;
// One frame in this many timed on each thread, its results counted this many times: a query a call costs the thread some 0.4 ms
// a frame (the beach at 4x: 250 calls), which a frame in 8 makes nothing; the minute's lines average hundreds of timed frames
constexpr u32 TimedEvery = 8;
// Queries a thread has begun and not read past which its driver is taken never to give their results (some 30 timed frames)
constexpr size_t MostWaiting = 8192;
// A thread's frames after which a query's result not there yet is taken never to come (its begin refused: no result ever, holding
// back every one after it): left out, the ones after it read. (1.2.0 left one out only when the next one's result was there: on
// the beach in a window at unlimited frames on two cores, 1.2.0 too, several in a row never came 0.5 s into the start, the
// next ones waited behind them and the card's time was unknown from 10 s on, as in #32's log. A result comes a frame or two
// after its query, so one 120 frames old won't)
constexpr u32 StuckFrames = 120;

// OpenGL's own entry points: the work the timed ones give the card, and the queries'
struct Real
{
    void (*DrawArrays)(GLenum, GLint, GLsizei);
    void (*Clear)(GLbitfield);
    void (*BlitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
    void (*TexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*);
    void (*TexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*);
    void (*GenerateMipmap)(GLenum);
    void (*GenQueries)(GLsizei, GLuint*);
    void (*BeginQuery)(GLenum, GLuint);
    void (*EndQuery)(GLenum);
    void (*GetQueryiv)(GLenum, GLenum, GLint*);
    void (*GetQueryObjectiv)(GLuint, GLenum, GLint*);
    void (*GetQueryObjectui64v)(GLuint, GLenum, GLuint64*);
    void (*Flush)();
};
Real g_Real = {};
std::once_flag g_LoadOnce;
bool g_Enabled = false;

template <typename T>
bool Load(T& function, const char* name)
{
    function = reinterpret_cast<T>(SDL_GL_GetProcAddress(name));
    return function != nullptr;
}

void LoadReal()
{
    const char* setting = std::getenv("TWIN_GPU_TIME");
    if (setting != nullptr && std::strcmp(setting, "0") == 0)
    {
        return;
    }

    bool ok = Load(g_Real.DrawArrays, "glDrawArrays");
    ok &= Load(g_Real.Clear, "glClear");
    ok &= Load(g_Real.BlitFramebuffer, "glBlitFramebuffer");
    ok &= Load(g_Real.TexImage2D, "glTexImage2D");
    ok &= Load(g_Real.TexSubImage2D, "glTexSubImage2D");
    ok &= Load(g_Real.GenerateMipmap, "glGenerateMipmap");
    ok &= Load(g_Real.GenQueries, "glGenQueries");
    ok &= Load(g_Real.BeginQuery, "glBeginQuery");
    ok &= Load(g_Real.EndQuery, "glEndQuery");
    ok &= Load(g_Real.GetQueryiv, "glGetQueryiv");
    ok &= Load(g_Real.GetQueryObjectiv, "glGetQueryObjectiv");
    ok &= Load(g_Real.GetQueryObjectui64v, "glGetQueryObjectui64v");
    ok &= Load(g_Real.Flush, "glFlush");
    g_Enabled = ok;
    if (!ok)
    {
        Native::Log("graphics: OpenGL has no timer queries' entry points: the card's time unknown");
    }
}

// A thread's queries: the context they're in (the one current when it first timed something: a span on another isn't timed),
// whether the driver times there, its frames and whether this one's timed, the spans open (the outermost's query begun or not),
// the queries begun and not read yet in their order, and those read, to use again
struct Waiting
{
    GLuint query;
    // The thread's frame it was begun in
    u32 frame;
    // Its kind of work (GpuWork) and its label's index
    u32 kind;
    u32 label;
};

struct Timer
{
    SDL_GLContext context = nullptr;
    bool tried = false;
    bool usable = false;
    u32 frames = 0;
    bool timing = true;
    u32 depth = 0;
    bool began = false;
    std::deque<Waiting> waiting;
    std::vector<GLuint> spare;
    // The frames its results couldn't be read (another context current), and the queries left out (their results never came)
    u64 unread = 0;
    u64 leftOut = 0;
    // The results read
    u64 results = 0;
    bool stuckMade = false;
};
thread_local Timer t_Timer;
std::atomic<u64> g_Nanoseconds{0};
// The kind of call being timed (set by the timed entry points), and each kind's nanoseconds
thread_local GpuWork t_Kind = GpuWork::Draws;
std::atomic<u64> g_KindNanoseconds[static_cast<u32>(GpuWork::Count)] = {};
// The labels (0: none, the window's thread's or a call before any label; a label past MostLabels counted as none), each one's
// nanoseconds; the calling thread's label now
const char* g_Labels[MostLabels] = {"the window and the rest"};
std::atomic<u32> g_LabelCount{1};
std::atomic<u64> g_LabelNanoseconds[MostLabels] = {};
thread_local u32 t_Label = 0;
// A context timing, and one found not to (its driver's timer has no bits, or its results never came)
std::atomic<bool> g_Working{false};
std::atomic<bool> g_Failed{false};
// The log told the card's time is measured
std::atomic<bool> g_Said{false};

bool Ready(Timer& timer)
{
    SDL_GLContext current = SDL_GL_GetCurrentContext();
    if (current == nullptr)
    {
        return false;
    }

    if (!timer.tried)
    {
        timer.tried = true;
        timer.context = current;
        GLint bits = 0;
        g_Real.GetQueryiv(GL_TIME_ELAPSED, GL_QUERY_COUNTER_BITS, &bits);
        timer.usable = bits > 0;
        (timer.usable ? g_Working : g_Failed) = true;
        if (!timer.usable)
        {
            Native::Log("graphics: no timer on this context (GL_TIME_ELAPSED's counter has no bits): the card's time unknown");
        }
        else if (!g_Said.exchange(true))
        {
            Native::Log("graphics: the card's time measured by OpenGL's timer queries (%d bits), a frame in %u", bits, TimedEvery);
        }
    }

    return timer.usable && current == timer.context;
}

void TimedDrawArrays(GLenum mode, GLint first, GLsizei count)
{
    t_Kind = GpuWork::Draws;
    GpuTimeBegin();
    g_Real.DrawArrays(mode, first, count);
    GpuTimeEnd();
}

void TimedClear(GLbitfield mask)
{
    t_Kind = GpuWork::Clears;
    GpuTimeBegin();
    g_Real.Clear(mask);
    GpuTimeEnd();
}

void TimedBlitFramebuffer(GLint x0, GLint y0, GLint x1, GLint y1, GLint toX0, GLint toY0, GLint toX1, GLint toY1, GLbitfield mask,
                          GLenum filter)
{
    t_Kind = GpuWork::Copies;
    GpuTimeBegin();
    g_Real.BlitFramebuffer(x0, y0, x1, y1, toX0, toY0, toX1, toY1, mask, filter);
    GpuTimeEnd();
}

void TimedTexImage2D(GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLint border, GLenum format,
                     GLenum type, const void* pixels)
{
    t_Kind = GpuWork::Uploads;
    GpuTimeBegin();
    g_Real.TexImage2D(target, level, internalFormat, width, height, border, format, type, pixels);
    GpuTimeEnd();
}

void TimedTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type,
                        const void* pixels)
{
    t_Kind = GpuWork::Uploads;
    GpuTimeBegin();
    g_Real.TexSubImage2D(target, level, x, y, width, height, format, type, pixels);
    GpuTimeEnd();
}

void TimedGenerateMipmap(GLenum target)
{
    t_Kind = GpuWork::Uploads;
    GpuTimeBegin();
    g_Real.GenerateMipmap(target);
    GpuTimeEnd();
}
}

void* TimedEntryPoint(const char* name)
{
    std::call_once(g_LoadOnce, LoadReal);
    if (!g_Enabled)
    {
        return nullptr;
    }

    const struct
    {
        const char* name;
        void* timed;
    } Timed[] = {
        {"glDrawArrays", reinterpret_cast<void*>(TimedDrawArrays)},
        {"glClear", reinterpret_cast<void*>(TimedClear)},
        {"glBlitFramebuffer", reinterpret_cast<void*>(TimedBlitFramebuffer)},
        {"glTexImage2D", reinterpret_cast<void*>(TimedTexImage2D)},
        {"glTexSubImage2D", reinterpret_cast<void*>(TimedTexSubImage2D)},
        {"glGenerateMipmap", reinterpret_cast<void*>(TimedGenerateMipmap)},
    };
    for (const auto& entry : Timed)
    {
        if (std::strcmp(entry.name, name) == 0)
        {
            return entry.timed;
        }
    }

    return nullptr;
}

void GpuTimeBegin()
{
    Timer& timer = t_Timer;
    if (timer.depth++ != 0 || !timer.timing || !Ready(timer))
    {
        return;
    }

    GLuint query = 0;
    if (!timer.spare.empty())
    {
        query = timer.spare.back();
        timer.spare.pop_back();
    }
    else
    {
        g_Real.GenQueries(1, &query);
    }

    // $TWIN_GPU_TIME_TEST=stuck (the log's check): each thread's first query never begun, so its result never comes
    static const bool stuckTest = std::getenv("TWIN_GPU_TIME_TEST") != nullptr &&
                                  std::strcmp(std::getenv("TWIN_GPU_TIME_TEST"), "stuck") == 0;
    if (stuckTest && !timer.stuckMade)
    {
        timer.stuckMade = true;
        timer.waiting.push_back({query, timer.frames, 0, 0});
        return;
    }

    g_Real.BeginQuery(GL_TIME_ELAPSED, query);
    timer.waiting.push_back({query, timer.frames, static_cast<u32>(SDL_IsMainThread() ? GpuWork::Window : t_Kind), t_Label});
    timer.began = true;
}

void GpuTimeEnd()
{
    Timer& timer = t_Timer;
    if (timer.depth == 0 || --timer.depth != 0 || !timer.began)
    {
        return;
    }

    g_Real.EndQuery(GL_TIME_ELAPSED);
    // Sent at once: a driver may have sent the work already (the card idle: NVIDIA's without a window) and keep the query's end
    // until the next call's, the card's wait for it counted (8.7 ms a frame against 2.2 headless with one thread)
    g_Real.Flush();
    timer.began = false;
}

void GpuTimeFrame()
{
    Timer& timer = t_Timer;
    timer.timing = ++timer.frames % TimedEvery == 0;
    if (!timer.usable || timer.depth != 0)
    {
        return;
    }

    // Its results are read on its own context only: with another current here, none is started until they can be read again
    // (they'd pile up unread)
    if (SDL_GL_GetCurrentContext() != timer.context)
    {
        timer.timing = false;
        timer.unread++;
        return;
    }

    // The results there, in their order, up to the first one that isn't; one not there after StuckFrames never comes (a player's
    // log at 3x and 144 Hz had 8219 waiting after 4 s of the title, the user's at 8x with SSAA 8357, the time unknown from then
    // on): left out, the rest read
    u64 read = 0;
    while (!timer.waiting.empty())
    {
        const Waiting oldest = timer.waiting.front();
        GLint available = 0;
        g_Real.GetQueryObjectiv(oldest.query, GL_QUERY_RESULT_AVAILABLE, &available);
        if (available == 0)
        {
            if (timer.frames - oldest.frame < StuckFrames)
            {
                break;
            }

            if (timer.leftOut++ == 0)
            {
                Native::Log("graphics: a timer query's result not there %u frames after it was begun (%s): left out (the card's "
                            "time a little short)",
                            timer.frames - oldest.frame, SDL_IsMainThread() ? "the window's thread's" : "the render thread's");
            }

            // (A new name for the next one: this one's may never be usable)
            timer.waiting.pop_front();
            continue;
        }

        GLuint64 nanoseconds = 0;
        g_Real.GetQueryObjectui64v(oldest.query, GL_QUERY_RESULT, &nanoseconds);
        read += nanoseconds;
        g_KindNanoseconds[oldest.kind].fetch_add(nanoseconds * TimedEvery, std::memory_order_relaxed);
        g_LabelNanoseconds[oldest.label].fetch_add(nanoseconds * TimedEvery, std::memory_order_relaxed);
        timer.results++;
        timer.waiting.pop_front();
        timer.spare.push_back(oldest.query);
    }

    // (Each timed frame stands for TimedEvery)
    g_Nanoseconds.fetch_add(read * TimedEvery, std::memory_order_relaxed);
    if (timer.waiting.size() > MostWaiting)
    {
        Native::Log("graphics: %zu timer queries' results not there yet (the oldest begun %u frames ago; %llu frames with another "
                    "context current, %llu queries left out): the card's time unknown",
                    timer.waiting.size(), timer.frames - timer.waiting.front().frame,
                    static_cast<unsigned long long>(timer.unread), static_cast<unsigned long long>(timer.leftOut));
        timer.usable = false;
        g_Failed = true;
    }
    // (Most never coming, the card's time would read short: unknown instead)
    else if (timer.leftOut > MostWaiting && timer.leftOut > timer.results)
    {
        Native::Log("graphics: %llu timer queries' results never came, %llu did: the card's time unknown",
                    static_cast<unsigned long long>(timer.leftOut), static_cast<unsigned long long>(timer.results));
        timer.usable = false;
        g_Failed = true;
    }
}

u64 GpuBusyNanoseconds()
{
    return g_Nanoseconds.load(std::memory_order_relaxed);
}

bool GpuTimeKnown()
{
    return g_Working && !g_Failed;
}

u64 GpuWorkNanoseconds(GpuWork kind)
{
    return g_KindNanoseconds[static_cast<u32>(kind)].load(std::memory_order_relaxed);
}

void GpuTimeLabel(const char* label)
{
    if (label == nullptr)
    {
        t_Label = 0;
        return;
    }

    // (Only the render thread adds labels: the list's reads elsewhere see a name before its count)
    const u32 count = g_LabelCount.load(std::memory_order_acquire);
    for (u32 i = 1; i < count; i++)
    {
        if (g_Labels[i] == label)
        {
            t_Label = i;
            return;
        }
    }

    if (count == MostLabels)
    {
        t_Label = 0;
        return;
    }

    g_Labels[count] = label;
    g_LabelCount.store(count + 1, std::memory_order_release);
    t_Label = count;
}

u32 GpuLabelTimes(const char** names, u64* nanoseconds)
{
    const u32 count = g_LabelCount.load(std::memory_order_acquire);
    for (u32 i = 0; i < count; i++)
    {
        names[i] = g_Labels[i];
        nanoseconds[i] = g_LabelNanoseconds[i].load(std::memory_order_relaxed);
    }

    return count;
}
}
