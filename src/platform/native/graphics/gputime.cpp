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
struct Timer
{
    SDL_GLContext context = nullptr;
    bool tried = false;
    bool usable = false;
    u32 frames = 0;
    bool timing = true;
    u32 depth = 0;
    bool began = false;
    std::deque<GLuint> waiting;
    std::vector<GLuint> spare;
};
thread_local Timer t_Timer;
std::atomic<u64> g_Nanoseconds{0};
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
    GpuTimeBegin();
    g_Real.DrawArrays(mode, first, count);
    GpuTimeEnd();
}

void TimedClear(GLbitfield mask)
{
    GpuTimeBegin();
    g_Real.Clear(mask);
    GpuTimeEnd();
}

void TimedBlitFramebuffer(GLint x0, GLint y0, GLint x1, GLint y1, GLint toX0, GLint toY0, GLint toX1, GLint toY1, GLbitfield mask,
                          GLenum filter)
{
    GpuTimeBegin();
    g_Real.BlitFramebuffer(x0, y0, x1, y1, toX0, toY0, toX1, toY1, mask, filter);
    GpuTimeEnd();
}

void TimedTexImage2D(GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height, GLint border, GLenum format,
                     GLenum type, const void* pixels)
{
    GpuTimeBegin();
    g_Real.TexImage2D(target, level, internalFormat, width, height, border, format, type, pixels);
    GpuTimeEnd();
}

void TimedTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type,
                        const void* pixels)
{
    GpuTimeBegin();
    g_Real.TexSubImage2D(target, level, x, y, width, height, format, type, pixels);
    GpuTimeEnd();
}

void TimedGenerateMipmap(GLenum target)
{
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

    g_Real.BeginQuery(GL_TIME_ELAPSED, query);
    timer.waiting.push_back(query);
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
    if (!timer.usable || timer.depth != 0 || SDL_GL_GetCurrentContext() != timer.context)
    {
        return;
    }

    // The results there, in their order, up to the first one that isn't
    u64 read = 0;
    while (!timer.waiting.empty())
    {
        GLuint query = timer.waiting.front();
        GLint available = 0;
        g_Real.GetQueryObjectiv(query, GL_QUERY_RESULT_AVAILABLE, &available);
        if (available == 0)
        {
            break;
        }

        GLuint64 nanoseconds = 0;
        g_Real.GetQueryObjectui64v(query, GL_QUERY_RESULT, &nanoseconds);
        read += nanoseconds;
        timer.waiting.pop_front();
        timer.spare.push_back(query);
    }

    // (Each timed frame stands for TimedEvery)
    g_Nanoseconds.fetch_add(read * TimedEvery, std::memory_order_relaxed);
    if (timer.waiting.size() > MostWaiting)
    {
        Native::Log("graphics: %zu timer queries' results not there yet: the card's time unknown", timer.waiting.size());
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
}
