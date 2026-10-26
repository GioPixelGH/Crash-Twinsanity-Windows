#include "renderthread.h"

#include "hardware.h"
#include "ee/gif.h"
#include "ee/vif.h"

#include <SDL3/SDL.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace NativeGraphics
{
// display.cpp: the render thread's OpenGL context (made on the main thread, sharing the window's), made current on this thread
bool MakeRenderContext();
void UseRenderContext();
void ReleaseRenderContext();
}

namespace NativeGraphics
{
namespace
{
// The queue's records: a header, then its data (quadwords)
enum RecordKind : u32
{
    RecordTransfer,
    RecordKick,
    RecordCall,
    RecordVif1,
};

struct Header
{
    u32 kind;
    // Transfer: the path; Kick: the kicking code's address (TWIN_GS_TRACE); Call: the work's index
    u32 value;
    u32 quadwords;
    u32 unused;
};
static_assert(sizeof(Header) == 16);

// A piece of the queue: records one after another, and the work the call records name
struct Chunk
{
    u8* bytes = nullptr;
    size_t size = 0;
    size_t capacity = 0;
    std::vector<std::function<void()>> calls;

    u8* Grow(size_t more)
    {
        if (size + more > capacity)
        {
            capacity = std::max(capacity * 2, size + more);
            bytes = static_cast<u8*>(std::realloc(bytes, capacity));
        }

        u8* at = bytes + size;
        size += more;
        return at;
    }
};

// A chunk's handed over once it's this full (or at a flush), so the render thread starts on a frame's data while it's made
constexpr size_t ChunkBytes = 512 * 1024;
// Chunks kept for reuse no bigger than this (a huge transfer's goes)
constexpr size_t KeptChunkBytes = 8 * 1024 * 1024;

class RenderThread final : public Ee::Gif::Forward
{
public:
    void Start();
    void Stop();
    // The VU stage ($TWIN_RENDER_THREAD=vu): VIF1 and VU1 on a thread of their own between the game's and the render thread, what
    // they send the GIF (and everything else the game queued, in its place) handed on to the render thread's queue
    void StartStage(RenderThread* next);
    void Kicked(const u8* row, u32 quadwords, u32 source);
    void WaitIdle();

    // The game's side
    void Transfer(u32 path, const u8* data, u32 quadwords) override;
    void Kick(const u8* vuMemory, u32 address, u32 quadwords, u32 source) override;
    void Call(std::function<void()> work);
    void Vif1(const u32* words, u32 count);
    void Flush();
    void Sync();
    u64 QueueFrame(std::function<void(u64 frame)> show);
    void WaitFrame(u64 frame);

    std::atomic<u64> busyNanoseconds{0};
    std::atomic<u64> waitNanoseconds{0};

private:
    void Run();
    void Play(Chunk& chunk);
    Chunk* Take();
    u8* Record(u32 kind, u32 value, u32 quadwords);

    std::thread thread_;
    std::mutex lock_;
    std::condition_variable work_;
    std::condition_variable done_;
    std::deque<Chunk*> ready_;
    std::vector<Chunk*> spare_;
    bool stopping_ = false;
    // Chunks handed over and played (under lock_)
    u64 handed_ = 0;
    u64 played_ = 0;
    // Frames queued (the game's side) and made (under lock_)
    u64 framesQueued_ = 0;
    u64 framesMade_ = 0;
    // The chunk being filled (the game's side)
    Chunk* filling_ = nullptr;
    // The VU stage's: the render thread's queue it hands on to
    RenderThread* next_ = nullptr;
};

RenderThread g_RenderThread;
RenderThread g_VuThread;
std::atomic<bool> g_Active{false};
// Three stages (the game's, VIF1 and VU1's, the GS's): the game's side queues to the VU stage
bool g_ThreeStages = false;
thread_local bool t_OnRenderThread = false;
thread_local bool t_OnVuThread = false;
int g_Wanted = -1;
int g_Vu1There = -1;
bool g_Tried = false;

// $TWIN_RENDER_THREAD: three stages unless it says otherwise (0: none, everything on the game's thread; gs: the render thread
// only, VIF1 and VU1 on the game's; vif: VIF1 and VU1 on the render thread)
const char* RenderThreadSetting()
{
    const char* setting = std::getenv("TWIN_RENDER_THREAD");
    return setting != nullptr ? setting : "vu";
}

bool ThreeStagesWanted()
{
    const char* setting = RenderThreadSetting();
    return std::strcmp(setting, "vu") == 0 || std::strcmp(setting, "1") == 0;
}

// The queue the calling thread puts the GS's work in: the VU stage's for the game's thread when there are three, the render
// thread's for the VU stage's thread (or the game's when there are two)
RenderThread& Producing()
{
    return g_ThreeStages && !t_OnVuThread ? g_VuThread : g_RenderThread;
}

Chunk* RenderThread::Take()
{
    std::lock_guard lock(lock_);
    if (!spare_.empty())
    {
        Chunk* chunk = spare_.back();
        spare_.pop_back();
        return chunk;
    }

    Chunk* chunk = new Chunk();
    chunk->Grow(ChunkBytes);
    chunk->size = 0;
    return chunk;
}

u8* RenderThread::Record(u32 kind, u32 value, u32 quadwords)
{
    size_t bytes = sizeof(Header) + static_cast<size_t>(quadwords) * 16;
    if (filling_ != nullptr && filling_->size != 0 && filling_->size + bytes > ChunkBytes)
    {
        Flush();
    }

    if (filling_ == nullptr)
    {
        filling_ = Take();
    }

    u8* at = filling_->Grow(bytes);
    Header header = {kind, value, quadwords, 0};
    std::memcpy(at, &header, sizeof(header));
    return at + sizeof(Header);
}

void RenderThread::Vif1(const u32* words, u32 count)
{
    std::memcpy(Record(RecordVif1, count, (count + 3) / 4), words, static_cast<size_t>(count) * 4);
}

void RenderThread::Transfer(u32 path, const u8* data, u32 quadwords)
{
    // (VIF1 and VU1 on the render thread: theirs go straight on)
    if (t_OnRenderThread)
    {
        GetHardware().gif->TransferNow(path, data, quadwords);
        return;
    }

    // (VIF1 and VU1 on the VU stage's thread: theirs to the render thread's queue)
    if (t_OnVuThread && next_ != nullptr)
    {
        next_->Transfer(path, data, quadwords);
        return;
    }

    if (quadwords == 0)
    {
        return;
    }

    std::memcpy(Record(RecordTransfer, path, quadwords), data, static_cast<size_t>(quadwords) * 16);
}

void RenderThread::Kick(const u8* vuMemory, u32 address, u32 quadwords, u32 source)
{
    if (t_OnRenderThread)
    {
        GetHardware().gif->KickNow(vuMemory, address, source);
        return;
    }

    if (t_OnVuThread && next_ != nullptr)
    {
        next_->Kick(vuMemory, address, quadwords, source);
        return;
    }

    // The ring's quadwords from the address, wrapping at its end
    constexpr u32 Ring = 0x400;
    u8* to = Record(RecordKick, source, quadwords);
    u32 at = address & (Ring - 1);
    u32 first = std::min(quadwords, Ring - at);
    std::memcpy(to, vuMemory + at * 16, static_cast<size_t>(first) * 16);
    std::memcpy(to + first * 16, vuMemory, static_cast<size_t>(quadwords - first) * 16);
}

// A kick already laid out in a row (the VU stage handing on one the game's thread queued)
void RenderThread::Kicked(const u8* row, u32 quadwords, u32 source)
{
    std::memcpy(Record(RecordKick, source, quadwords), row, static_cast<size_t>(quadwords) * 16);
}

void RenderThread::Call(std::function<void()> work)
{
    // (the chunk taken first: the work's index is in the chunk its record is in)
    Record(RecordCall, 0, 0);
    Header* header = reinterpret_cast<Header*>(filling_->bytes + filling_->size - sizeof(Header));
    header->value = static_cast<u32>(filling_->calls.size());
    filling_->calls.push_back(std::move(work));
    Flush();
}

void RenderThread::Flush()
{
    if (filling_ == nullptr || filling_->size == 0)
    {
        return;
    }

    {
        std::lock_guard lock(lock_);
        ready_.push_back(filling_);
        handed_++;
    }

    filling_ = nullptr;
    work_.notify_one();
}

void RenderThread::Sync()
{
    Flush();
    auto began = std::chrono::steady_clock::now();
    {
        std::unique_lock lock(lock_);
        done_.wait(lock, [this] { return played_ == handed_; });
    }

    // (The VU stage hands on each chunk it plays before it counts it played: the render thread's queue has all of it)
    if (next_ != nullptr)
    {
        next_->WaitIdle();
    }

    waitNanoseconds += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - began).count();
}

// Everything handed over played (the producer's flushed it)
void RenderThread::WaitIdle()
{
    std::unique_lock lock(lock_);
    done_.wait(lock, [this] { return played_ == handed_; });
}

u64 RenderThread::QueueFrame(std::function<void(u64 frame)> show)
{
    u64 frame = ++framesQueued_;
    Call([this, frame, show = std::move(show)] {
        show(frame);
        {
            std::lock_guard lock(lock_);
            framesMade_ = frame;
        }

        done_.notify_all();
    });
    return frame;
}

void RenderThread::WaitFrame(u64 frame)
{
    Flush();
    auto began = std::chrono::steady_clock::now();
    std::unique_lock lock(lock_);
    done_.wait(lock, [this, frame] { return framesMade_ >= frame; });
    waitNanoseconds += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - began).count();
}

void RenderThread::Play(Chunk& chunk)
{
    Ee::Gif& gif = *GetHardware().gif;
    const u8* at = chunk.bytes;
    const u8* end = chunk.bytes + chunk.size;
    while (at < end)
    {
        Header header;
        std::memcpy(&header, at, sizeof(header));
        at += sizeof(Header);
        if (next_ != nullptr)
        {
            // The VU stage: the GIF's data and the work handed on in their places, VIF1's run here
            switch (header.kind)
            {
            case RecordTransfer:
                next_->Transfer(header.value, at, header.quadwords);
                break;
            case RecordKick:
                next_->Kicked(at, header.quadwords, header.value);
                break;
            case RecordCall:
                next_->Call(std::move(chunk.calls[header.value]));
                break;
            case RecordVif1:
                GetHardware().vif1->Process(reinterpret_cast<const u32*>(at), header.value);
                break;
            default:
                break;
            }

            at += static_cast<size_t>(header.quadwords) * 16;
            continue;
        }

        switch (header.kind)
        {
        case RecordTransfer:
            gif.TransferNow(header.value, at, header.quadwords);
            break;
        case RecordKick:
            gif.KickRow(at, header.quadwords, header.value);
            break;
        case RecordCall:
            chunk.calls[header.value]();
            break;
        case RecordVif1:
            GetHardware().vif1->Process(reinterpret_cast<const u32*>(at), header.value);
            break;
        default:
            break;
        }

        at += static_cast<size_t>(header.quadwords) * 16;
    }
}

void RenderThread::Run()
{
    if (next_ != nullptr)
    {
        t_OnVuThread = true;
    }
    else
    {
        t_OnRenderThread = true;
        UseRenderContext();
    }

    for (;;)
    {
        Chunk* chunk;
        {
            std::unique_lock lock(lock_);
            work_.wait(lock, [this] { return !ready_.empty() || stopping_; });
            if (ready_.empty())
            {
                break;
            }

            chunk = ready_.front();
            ready_.pop_front();
        }

        auto began = std::chrono::steady_clock::now();
        Play(*chunk);
        if (next_ != nullptr)
        {
            next_->Flush();
        }

        chunk->calls.clear();
        chunk->size = 0;
        if (chunk->capacity > KeptChunkBytes)
        {
            std::free(chunk->bytes);
            chunk->bytes = nullptr;
            chunk->capacity = 0;
        }

        busyNanoseconds += std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - began).count();
        {
            std::lock_guard lock(lock_);
            spare_.push_back(chunk);
            played_++;
        }

        done_.notify_all();
    }

    if (next_ == nullptr)
    {
        ReleaseRenderContext();
    }
}

void RenderThread::Start()
{
    // The hardware made here first (its statics aren't thread safe), then the GIF's data sent this way
    Hardware& hardware = GetHardware();
    if (!MakeRenderContext())
    {
        std::fprintf(stderr, "graphics: no context for a render thread: everything stays on the game's thread\n");
        return;
    }

    stopping_ = false;
    thread_ = std::thread([this] { Run(); });
    hardware.gif->SetForward(this);
    g_Active = true;
}

void RenderThread::StartStage(RenderThread* next)
{
    next_ = next;
    stopping_ = false;
    thread_ = std::thread([this] { Run(); });
    GetHardware().gif->SetForward(this);
}

void RenderThread::Stop()
{
    if (!g_Active)
    {
        return;
    }

    // The VU stage first (what it holds handed on), then the render thread
    if (g_ThreeStages && this == &g_RenderThread)
    {
        g_VuThread.Sync();
        {
            std::lock_guard lock(g_VuThread.lock_);
            g_VuThread.stopping_ = true;
        }

        g_VuThread.work_.notify_one();
        g_VuThread.thread_.join();
        g_ThreeStages = false;
        GetHardware().gif->SetForward(this);
    }

    Sync();
    GetHardware().gif->SetForward(nullptr);
    {
        std::lock_guard lock(lock_);
        stopping_ = true;
    }

    work_.notify_one();
    thread_.join();
    g_Active = false;
}
}

void SetRenderThreadWanted(bool wanted)
{
    g_Wanted = wanted ? 1 : 0;
}

bool RenderThreadWanted()
{
    if (g_Wanted < 0)
    {
        const char* setting = std::getenv("TWIN_RENDER_THREAD");
        g_Wanted = (setting != nullptr && std::strcmp(setting, "0") == 0) ? 0 : 1;
    }

    return g_Wanted == 1;
}

void StartRenderThread()
{
    if (g_Active || !RenderThreadWanted())
    {
        return;
    }

    g_Tried = true;
    g_RenderThread.Start();
    if (g_Active)
    {
        // VIF1 and VU1 on a thread of their own (the game's thread had them, half its time where the scenes are big)
        if (ThreeStagesWanted())
        {
            g_ThreeStages = true;
            g_VuThread.StartStage(&g_RenderThread);
        }

        std::atexit([] { StopRenderThread(); });
    }
}

void StopRenderThread()
{
    if (!OnRenderThread() && !t_OnVuThread)
    {
        g_RenderThread.Stop();
    }
}

bool RenderThreadActive()
{
    return g_Active.load(std::memory_order_relaxed);
}

bool OnRenderThread()
{
    return t_OnRenderThread;
}

bool RenderThreadPending()
{
    return !g_Tried && RenderThreadWanted();
}

void RenderCall(std::function<void()> work)
{
    if (!RenderThreadActive() || t_OnRenderThread)
    {
        work();
        return;
    }

    Producing().Call(std::move(work));
}

void RenderCallAndWait(const std::function<void()>& work)
{
    if (!RenderThreadActive() || t_OnRenderThread)
    {
        work();
        return;
    }

    RenderThread& queue = Producing();
    queue.Call([&work] { work(); });
    queue.Sync();
}

void RenderSync()
{
    if (RenderThreadActive() && !t_OnRenderThread)
    {
        Producing().Sync();
    }
}

void RenderFlush()
{
    if (RenderThreadActive() && !t_OnRenderThread)
    {
        Producing().Flush();
    }
}

u64 RenderQueueFrame(std::function<void(u64 frame)> show)
{
    return Producing().QueueFrame(std::move(show));
}

void RenderWaitFrame(u64 frame)
{
    Producing().WaitFrame(frame);
}

bool RenderThreadRunsVu1()
{
    if (g_Vu1There < 0)
    {
        g_Vu1There = ThreeStagesWanted() || std::strcmp(RenderThreadSetting(), "vif") == 0 ? 1 : 0;
    }

    return g_Vu1There == 1 && RenderThreadActive();
}

void RenderQueueVif1(const u32* words, u32 count)
{
    Producing().Vif1(words, count);
}

u64 RenderBusyNanoseconds()
{
    return g_RenderThread.busyNanoseconds.load(std::memory_order_relaxed);
}

u64 RenderWaitNanoseconds()
{
    return Producing().waitNanoseconds.load(std::memory_order_relaxed);
}

u64 VuStageBusyNanoseconds()
{
    return g_VuThread.busyNanoseconds.load(std::memory_order_relaxed);
}

const char* ThreadsAskedText()
{
    const char* setting = std::getenv("TWIN_RENDER_THREAD");
    if (!RenderThreadWanted())
    {
        return "one, everything on the game's thread ($TWIN_RENDER_THREAD=0)";
    }

    if (ThreeStagesWanted())
    {
        return setting == nullptr ? "three, the game's, VU1's and the render thread (the default)"
                                  : "three, the game's, VU1's and the render thread ($TWIN_RENDER_THREAD)";
    }

    return std::strcmp(RenderThreadSetting(), "vif") == 0 ? "two, the game's and the render thread with VU1 ($TWIN_RENDER_THREAD=vif)"
                                                          : "two, the game's with VU1 and the render thread ($TWIN_RENDER_THREAD)";
}

Threads ThreadsNow()
{
    if (!RenderThreadActive())
    {
        return Threads::One;
    }

    return g_ThreeStages ? Threads::Three : RenderThreadRunsVu1() ? Threads::TwoVuOnRender : Threads::TwoVuOnGame;
}
}
