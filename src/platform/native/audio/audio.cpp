// The native build's sound (audio.h): the emulated I/O processor run in step with SDL's playback, the EE's RPC calls of its
// modules, Platform::Sound, and Platform::Stream's sound side (streams.h) as the PS2's Platform::Stream does it.
//
// Threads: the I/O processor and the SPU2 belong to whoever holds g_Lock. A thread of the audio's own runs them, a few
// milliseconds of samples at a time, keeping SDL's stream about Latency ahead; the game's thread runs them too while it waits
// for a call (its server's threads, the clock moving on only as far as they take; for longer, the audio thread's steps).
// $TWINSANITY_AUDIO=off plays nothing (the sound is made all the same, in time with the clock), $TWINSANITY_AUDIO=frames plays
// nothing and makes a PAL frame's samples (960) at each Platform::Stream::Update instead, the same sound every run whatever the
// computer's speed, and $TWINSANITY_AUDIO_WAV=path writes what's made to a WAV file
#include "audio.h"

#include "iop.h"
#include "native.h"
#include "spu2.h"
#include "streams.h"
#include "volumes.h"
#include "wav.h"

#include "../../ps2/multistream/multistream.h"
#include "game/controllers.h"
#include "game/language.h"
#include "game/resources.h"
#include "game/sound.h"
#include "platform/sound.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>

using namespace MultiStream;

extern "C" void* MsFastLoadRpc(s32 function, void* data, s32 size) RETAIL(FUN_001e4380);

namespace
{
constexpr u32 SampleRate = 48000;
// What SDL's stream is kept ahead by, and the samples made a step
constexpr u32 LatencyFrames = 1600;
constexpr u32 StepFrames = 256;
// A PAL frame's samples, for the frame-locked mode
constexpr u32 FrameSamples = SampleRate / 50;

std::mutex g_Lock;
bool g_Booted = false;
bool g_BootFailed = false;
// Samples made while the game's thread ran the I/O processor, for the audio thread to play
std::vector<s16> g_Made;

std::thread g_Thread;
std::atomic<bool> g_Running = false;
std::condition_variable g_Wake;
std::condition_variable g_Progress;
bool g_FrameLocked = false;
SDL_AudioStream* g_Stream = nullptr;
std::unique_ptr<Native::WavWriter> g_Wav;

// The call the game's thread made that hasn't ended yet (MultiStream sends one at a time)
struct Outstanding
{
    Iop::Call* call = nullptr;
    void* reply = nullptr;
    void (*ended)(void*) = nullptr;
};
Outstanding g_Outstanding;

void Play(const std::vector<s16>& samples)
{
    if (samples.empty())
    {
        return;
    }

    if (g_Stream != nullptr)
    {
        SDL_PutAudioStreamData(g_Stream, samples.data(), static_cast<int>(samples.size() * sizeof(s16)));
    }

    if (g_Wav)
    {
        g_Wav->Write(samples.data(), samples.size());
    }
}

void AudioThread()
{
    std::vector<s16> samples;
    auto start = std::chrono::steady_clock::now();
    u64 framesMade = 0;
    while (g_Running)
    {
        bool ahead;
        if (g_Stream != nullptr)
        {
            ahead = static_cast<u32>(SDL_GetAudioStreamQueued(g_Stream)) / 4 >= LatencyFrames;
        }
        else
        {
            // Nothing plays: the clock says how much should have been made
            double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            ahead = framesMade >= static_cast<u64>(seconds * SampleRate) + LatencyFrames;
        }

        samples.clear();
        if (!ahead)
        {
            std::lock_guard lock(g_Lock);
            samples.swap(g_Made);
            Iop::RunSamples(StepFrames, &samples);
        }

        if (!samples.empty())
        {
            framesMade += samples.size() / 2;
            Play(samples);
            g_Progress.notify_all();
            continue;
        }

        std::unique_lock lock(g_Lock);
        g_Wake.wait_for(lock, std::chrono::milliseconds(2));
    }
}

void StartOutput()
{
    const char* setting = std::getenv("TWINSANITY_AUDIO");
    g_FrameLocked = setting != nullptr && std::strcmp(setting, "frames") == 0;
    // A headless run (tests, captures) is silent unless $TWINSANITY_AUDIO=on asks for its sound: there's no window to say what's
    // playing
    bool asked = setting != nullptr && std::strcmp(setting, "on") == 0;
    bool silent = g_FrameLocked || (setting != nullptr && std::strcmp(setting, "off") == 0) ||
                  (Native::GetSettings().headless && !asked);
    if (const char* wav = std::getenv("TWINSANITY_AUDIO_WAV"))
    {
        g_Wav = std::make_unique<Native::WavWriter>(wav, SampleRate);
    }

    if (!silent)
    {
        if (!SDL_WasInit(SDL_INIT_AUDIO) && !SDL_InitSubSystem(SDL_INIT_AUDIO))
        {
            Native::Log("audio: SDL's audio didn't start: %s", SDL_GetError());
        }
        else
        {
            SDL_AudioSpec spec = {SDL_AUDIO_S16LE, 2, static_cast<int>(SampleRate)};
            g_Stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, nullptr, nullptr);
            if (g_Stream == nullptr)
            {
                Native::Log("audio: no audio device: %s", SDL_GetError());
            }
            else
            {
                SDL_ResumeAudioStreamDevice(g_Stream);
                // The device the sound goes to, for the log: its name, its rate, channels and buffer (SDL converts the game's)
                SDL_AudioDeviceID device = SDL_GetAudioStreamDevice(g_Stream);
                SDL_AudioSpec format = {};
                int frames = 0;
                if (!SDL_GetAudioDeviceFormat(device, &format, &frames))
                {
                    format = {};
                }

                Native::Log("audio: %s (%s), %d Hz, %d channels, a %d-frame buffer; the game's sound %u Hz stereo",
                            Native::DeviceNameForLog(SDL_GetAudioDeviceName(device)).c_str(),
                            SDL_GetCurrentAudioDriver() != nullptr ? SDL_GetCurrentAudioDriver() : "?", format.freq, format.channels,
                            frames, static_cast<unsigned>(SampleRate));
            }
        }
    }
    else
    {
        Native::Log("audio: none (%s)", g_FrameLocked ? "$TWINSANITY_AUDIO=frames: the tests' sound made with the frames"
                                        : setting != nullptr && std::strcmp(setting, "off") == 0 ? "$TWINSANITY_AUDIO=off"
                                                                                                 : "a test's run, no window");
    }

    if (g_FrameLocked)
    {
        return;
    }

    g_Running = true;
    g_Thread = std::thread(AudioThread);
}

void StopOutput()
{
    if (g_Running)
    {
        g_Running = false;
        g_Wake.notify_all();
        g_Thread.join();
    }

    // SDL may have quit already (Platform::System::Exit)
    if (g_Stream != nullptr && SDL_WasInit(SDL_INIT_AUDIO))
    {
        SDL_DestroyAudioStream(g_Stream);
    }

    g_Stream = nullptr;
    g_Wav.reset();
}
}

namespace
{
// The I/O processor's writes to the EE's memory: MultiStream's count of its runs (OpInitWait gives it g_MsIopUpdates' address,
// which natively is the host address's low 32 bits). The PS2's transfer is 64 bytes of the module's status from there on: the
// native build keeps the count, the word MultiStream reads
void WriteEe(u32 address, const u8* data, u32 size)
{
    if (address == static_cast<u32>(reinterpret_cast<uiptr>(&g_MsIopUpdates)) && size >= sizeof(u32))
    {
        u32 value;
        std::memcpy(&value, data, sizeof(value));
        *static_cast<volatile u32*>(&g_MsIopUpdates) = value;
        return;
    }

    Native::Log("audio: the I/O processor wrote %u bytes to the EE's %#x", size, address);
}
}

namespace
{
// The player's volumes (volumes.h): each voice's kind, from the batches the game sends, and the kinds' volumes
enum class Kind : u8
{
    Effect,
    Music,
    Voice,
};
std::atomic<float> g_Volumes[3] = {1.0f, 1.0f, 1.0f};
Kind g_VoiceKinds[Platform::Audio::Voices] = {};
std::unordered_map<u32, Kind> g_FileKinds;
Kind g_StreamKinds[Streams] = {};

void ApplyGain(u32 voice)
{
    float volume = std::clamp(g_Volumes[static_cast<int>(g_VoiceKinds[voice])].load(), 0.0f, 1.0f);
    Spu2::SetVoiceGain(static_cast<s32>(voice), static_cast<s32>(volume * 0x8000 + 0.5f));
}

void SetKind(u32 voice, Kind kind)
{
    if (voice < Platform::Audio::Voices)
    {
        g_VoiceKinds[voice] = kind;
        ApplyGain(voice);
    }
}

// What a file streams, by its path ("cdrom0:\CRASH6\MUSIC.MB")
Kind FileKind(const std::string& path)
{
    if (path.find("\\MUSIC.") != std::string::npos)
    {
        return Kind::Music;
    }

    if (path.size() > 3 && path.compare(path.size() - 3, 3, ".MB") == 0)
    {
        return Kind::Voice;
    }

    return Kind::Effect;
}

// A sound of the language's voices' table (the game's SoundById finds a sound in the sounds' table first, then in the voices')
bool IsVoiceSound(u32 id)
{
    const GameResources* resources = G_GameResourcesObjectPointer;
    if (resources == nullptr || resources->sounds == nullptr || resources->voices == nullptr)
    {
        return false;
    }

    u32 index = id & ResourceIndexMask;
    const ResourceTable* sounds = resources->sounds;
    if (index < sounds->bits.capacity && sounds->items[index] != nullptr &&
        static_cast<const GameSound*>(sounds->items[index])->soundId == id)
    {
        return false;
    }

    const ResourceTable* voices = resources->voices[g_CurrentLanguage];
    return voices != nullptr && index < voices->bits.capacity && voices->items[index] != nullptr &&
           static_cast<const GameSound*>(voices->items[index])->soundId == id;
}

// $TWIN_SOUND_CHECK (and the detailed log): each sound's samples where the SPU2 plays them from, compared with the first time it
// played (a sound whose memory changed under it plays wrong), and what its voice put out; STREAM.IRX's work on the sound memory
// (the banks read, loaded and freed, the compaction, the transfers, the reverbs on and off). The sound a voice is asked for
// (PlaySound's batch), then its samples as it's keyed on
std::mutex g_SoundCheckLock;
u32 g_VoiceSound[48] = {};
// The sound each voice was last keyed on with
u32 g_VoicePlaying[48] = {};
struct SoundSamples
{
    u32 address;
    u32 blocks;
    u64 hash;
    // Blocks whose samples aren't all 0 (a silent sound has none)
    u32 sounding;
};
std::unordered_map<u32, SoundSamples> g_SoundSamples;

// $TWIN_SOUND_CHECK's own: also the mix's peaks every second and the transfers of the music's and voices' stream buffers (below
// the sound memory, written many times a second)
bool FullSoundCheck()
{
    static const bool check = std::getenv("TWIN_SOUND_CHECK") != nullptr;
    return check;
}

bool SoundCheck()
{
    return FullSoundCheck() || Native::DetailedLog();
}

// Where STREAM.IRX's sound memory starts (halfwords: 0x1D010 bytes); the stream buffers are below
constexpr u32 SoundMemoryStart = 0xE808;

// The samples from address (halfwords) to the first block with the end flag (at most 4096 blocks): their count and hash
SoundSamples Samples(u32 address)
{
    const auto* memory = reinterpret_cast<const u16*>(Native::g_SpuMemory);
    SoundSamples samples = {address, 0, 0xCBF29CE484222325ull, 0};
    for (u32 block = 0; block < 4096; block++)
    {
        const u16* at = memory + ((address + block * 8) & Spu2::AddressMask);
        bool sounding = false;
        for (u32 i = 0; i < 8; i++)
        {
            samples.hash = (samples.hash ^ at[i]) * 0x100000001B3ull;
            sounding = sounding || (i != 0 && at[i] != 0);
        }

        samples.sounding += sounding ? 1 : 0;

        samples.blocks++;
        if ((at[0] >> 8 & 1) != 0)
        {
            break;
        }
    }

    return samples;
}

void CheckKeyOn(s32 voice, u32 address)
{
    std::lock_guard lock(g_SoundCheckLock);
    u32 sound = g_VoiceSound[voice];
    if (sound == 0)
    {
        return;
    }

    g_VoiceSound[voice] = 0;
    g_VoicePlaying[voice] = sound;
    SoundSamples now = Samples(address);
    auto [first, added] = g_SoundSamples.try_emplace(sound, now);
    if (added)
    {
        Native::Log("sound check: %#x first played on voice %d from %#x, %u blocks (%u with samples), hash %016llx", sound, voice,
                    address, now.blocks, now.sounding, static_cast<unsigned long long>(now.hash));
    }
    else if (first->second.hash != now.hash || first->second.blocks != now.blocks)
    {
        Native::Log("sound check: %#x on voice %d from %#x, %u blocks, hash %016llx: NOT as it first played (from %#x, %u blocks, "
                    "hash %016llx)",
                    sound, voice, address, now.blocks, static_cast<unsigned long long>(now.hash), first->second.address,
                    first->second.blocks, static_cast<unsigned long long>(first->second.hash));
    }
}

void CheckVoiceDone(s32 voice, u32 samples, s32 peak, bool dry, bool reverb, s32 masterLeft, s32 masterRight, u16 mmix)
{
    u32 sound;
    {
        std::lock_guard lock(g_SoundCheckLock);
        sound = g_VoicePlaying[voice];
        g_VoicePlaying[voice] = 0;
    }

    if (sound != 0)
    {
        Native::Log("sound check: %#x on voice %d sounded %u samples, peak %d%s%s; core %d's master %d/%d, mmix %03x", sound, voice,
                    samples, peak, dry ? ", dry" : ", NOT in the dry mix", reverb ? ", to the reverb" : "", voice / 24, masterLeft,
                    masterRight, mmix);
    }
}

void CheckTransfer(s32 core, bool written, u32 address, u32 halfwords)
{
    if (!SoundCheck() || (address < SoundMemoryStart && !FullSoundCheck()))
    {
        return;
    }

    Native::Log("sound check: core %d's DMA %s %#x, %u halfwords (to %#x)", core, written ? "writes" : "reads", address, halfwords,
                (address + halfwords) & Spu2::AddressMask);
}

// A second's mix: logged when something played (the gates and volumes said then too)
void CheckMix(const Spu2::MixPeaks& peaks)
{
    if (peaks.voices[0] == 0 && peaks.voices[1] == 0)
    {
        return;
    }

    Native::Log("sound check: mix peaks: core 0's voices %d, core 1's %d, output %d; mmix %03x/%03x, master %d/%d, core 0 into core 1 %d",
                peaks.voices[0], peaks.voices[1], peaks.output, peaks.mmix[0], peaks.mmix[1], peaks.master[0], peaks.master[1],
                peaks.external);
}

// The commands that say what plays on a voice: a file opened, a stream of audio played or interleaved on, a sound played
void Classify(const u16* batch)
{
    auto word32 = [](const u16* words) { return static_cast<u32>(words[0]) << 16 | words[1]; };
    // The SPU2's observers set the first time the check is on (the start-up's banks are read before anything plays)
    if (SoundCheck())
    {
        static const bool observing = (Spu2::SetKeyOnObserver(CheckKeyOn), Spu2::SetVoiceDoneObserver(CheckVoiceDone),
                                       Spu2::SetMixObserver(FullSoundCheck() ? CheckMix : nullptr),
                                       Spu2::SetTransferObserver(CheckTransfer), true);
        (void)observing;
    }

    u32 at = 2;
    for (u32 command = 0; command < batch[1]; command++)
    {
        u16 opcode = batch[at];
        u16 length = batch[at + 1];
        const u16* words = batch + at + 2;
        switch (opcode)
        {
        case OpCreateFileInfo:
        {
            const char* path = reinterpret_cast<const char*>(words + 4);
            g_FileKinds[word32(words)] = FileKind(std::string(path, strnlen(path, (length - 4) * 2u)));
            break;
        }
        case OpPlayStream:
            // Audio streams only (the modes below the loads)
            if (words[5] <= StreamOnce)
            {
                auto found = g_FileKinds.find(word32(words));
                Kind kind = found != g_FileKinds.end() ? found->second : Kind::Music;
                g_StreamKinds[(words[2] >> 8) % Streams] = kind;
                SetKind(words[2] & 0xFF, kind);
            }

            break;
        case OpSetStreamChild:
        {
            Kind kind = g_StreamKinds[(words[0] & 0xFF) % Streams];
            g_StreamKinds[(words[0] >> 8) % Streams] = kind;
            SetKind(words[1], kind);
            break;
        }
        case OpPatchSoundBank:
            if (SoundCheck())
            {
                Native::Log("sound check: bank %#x to be read to %#x", word32(words), word32(words + 2));
            }
            break;
        case OpEnableEffect:
            if (SoundCheck())
            {
                Native::Log("sound check: core %u's reverb on, mode %u (its work area %#x bytes below %#x)", words[0], words[1],
                            g_MsEffectSizesByMode[words[1] % 10], g_MsEffectAreaEnds[words[0] & 1]);
            }
            break;
        case OpDisableEffect:
            if (SoundCheck())
            {
                Native::Log("sound check: core %u's reverb off", words[0]);
            }
            break;
        case OpFreeSound:
            if (SoundCheck())
            {
                Native::Log("sound check: sound %#x freed", word32(words));
            }
            break;
        case OpSoundBankLoaded:
            if (SoundCheck())
            {
                Native::Log("sound check: bank %#x loaded", words[0]);
            }
            break;
        case OpCompactSoundMemory:
            // (every 36 frames: $TWIN_SOUND_CHECK's only; what it moves is in the transfers)
            if (FullSoundCheck())
            {
                Native::Log("sound check: the sounds' memory compacted a step");
            }
            break;
        case OpAllocateSoundTable:
            if (SoundCheck())
            {
                Native::Log("sound check: %u sounds reserved", words[0]);
            }
            break;
        case OpPlaySoundLoop:
            SetKind(words[2], IsVoiceSound(word32(words)) ? Kind::Voice : Kind::Effect);
            if (SoundCheck())
            {
                std::lock_guard lock(g_SoundCheckLock);
                g_VoiceSound[words[2] % 48] = word32(words);
            }

            break;
        default:
            break;
        }

        at += 2 + length;
    }
}
}

void NativeAudioSetVolumes(float music, float effects, float voice)
{
    g_Volumes[static_cast<int>(Kind::Music)] = music;
    g_Volumes[static_cast<int>(Kind::Effect)] = effects;
    g_Volumes[static_cast<int>(Kind::Voice)] = voice;
    for (u32 v = 0; v < Platform::Audio::Voices; v++)
    {
        ApplyGain(v);
    }

    // The movies' sound (the cores' input) counts as music
    Spu2::SetInputGain(static_cast<s32>(std::clamp(music, 0.0f, 1.0f) * 0x8000 + 0.5f));
}

void NativeAudioSetMuted(bool muted)
{
    Spu2::SetMuted(muted);
}

void Native::AudioTraceBatch(const u16* batch)
{
    Classify(batch);
    static const bool trace = std::getenv("TWINSANITY_AUDIO_TRACE") != nullptr;
    if (!trace)
    {
        return;
    }

    u32 at = 2;
    for (u32 command = 0; command < batch[1]; command++)
    {
        u16 opcode = batch[at];
        u16 length = batch[at + 1];
        if (opcode != OpGetStatus)
        {
            char line[512];
            int used = std::snprintf(line, sizeof(line), "ms %#x:", opcode);
            for (u16 i = 0; i < length && used < 480; i++)
            {
                used += std::snprintf(line + used, sizeof(line) - used, " %04x", batch[at + 2 + i]);
            }

            Native::Log("%s", line);
        }

        at += 2 + length;
    }
}

bool Native::AudioBootIop()
{
    std::lock_guard lock(g_Lock);
    if (g_Booted || g_BootFailed)
    {
        return g_Booted;
    }

    Iop::SetEeWriter(WriteEe);
    if (!Iop::Boot())
    {
        g_BootFailed = true;
        return false;
    }

    g_Booted = true;
    StartOutput();
    std::atexit(StopOutput);
    return true;
}

void Native::AudioCallIop(s32 server, u32 function, bool wait, const void* send, u32 sendSize, void* reply, u32 replySize,
                          void (*ended)(void*))
{
    if (!AudioBootIop())
    {
        return;
    }

    auto* call = new Iop::Call;
    call->server = server;
    call->function = function;
    call->send.assign(static_cast<const u8*>(send), static_cast<const u8*>(send) + sendSize);
    call->replySize = replySize;
    {
        std::lock_guard lock(g_Lock);
        Iop::Post(call);
    }

    // A call not waited for ends when AudioPollIop sees it done (MultiStream has one out at a time)
    if (!wait)
    {
        g_Outstanding = {call, reply, ended};
        g_Wake.notify_all();
        return;
    }

    // The call's server runs now, on this thread (the clock moves on only as far as its threads take); a call that waits on the
    // clock (the drive, a timer) waits for the audio thread's steps
    for (u32 tries = 0;; tries++)
    {
        std::unique_lock lock(g_Lock);
        Iop::RunUntilIdle(&g_Made);
        if (call->done)
        {
            break;
        }

        if (tries == 2000 && std::getenv("TWINSANITY_AUDIO_DEBUG") != nullptr)
        {
            Native::Log("audio: a call of %#x is slow", static_cast<u32>(server));
            Iop::LogState();
        }

        if (g_FrameLocked || !g_Running)
        {
            Iop::RunSamples(1, &g_Made);
        }
        else
        {
            g_Progress.wait_for(lock, std::chrono::milliseconds(1));
        }
    }

    std::memcpy(reply, call->reply.data(), call->replySize);
    delete call;
    if (ended != nullptr)
    {
        ended(nullptr);
    }
}

s32 Native::AudioSdRemote(s32 command, u32 a1, u32 a2, u32 a3, u32 a4, u32 a5)
{
    // SDRDRV's server; what's sent (its first word the buffer's address, then the arguments) and the reply's size
    constexpr s32 SdrServer = static_cast<s32>(0x80000701);
    constexpr u32 SendSize = 0x40;
    constexpr u32 ReplySize = 0x10;
    u32 buffer[SendSize / 4] = {0, a1, a2, a3, a4, a5};
    u32 reply[ReplySize / 4] = {};
    AudioCallIop(SdrServer, static_cast<u32>(command), true, buffer, SendSize, reply, ReplySize, nullptr);
    return static_cast<s32>(reply[0]);
}

u32 Native::AudioIopAllocate(u32 size)
{
    if (!AudioBootIop())
    {
        return 0;
    }

    std::lock_guard lock(g_Lock);
    return Iop::Allocate(size);
}

void Native::AudioIopFree(u32 address)
{
    std::lock_guard lock(g_Lock);
    Iop::Free(address);
}

void Native::AudioWriteIop(u32 address, const void* data, u32 size)
{
    std::lock_guard lock(g_Lock);
    for (u32 i = 0; i < size; i++)
    {
        Iop::Ram()[(address + i) & (Iop::RamSize - 1)] = static_cast<const u8*>(data)[i];
    }
}

u64 Native::AudioSamplesMade()
{
    std::lock_guard lock(g_Lock);
    return Iop::SamplesMade();
}

void Native::AudioWaitForSample(u64 sample)
{
    while (true)
    {
        std::unique_lock lock(g_Lock);
        u64 made = Iop::SamplesMade();
        if (made >= sample)
        {
            return;
        }

        if (g_FrameLocked || !g_Running)
        {
            std::vector<s16> samples;
            samples.swap(g_Made);
            Iop::RunSamples(static_cast<u32>(sample - made), &samples);
            lock.unlock();
            Play(samples);
            return;
        }

        g_Progress.wait_for(lock, std::chrono::milliseconds(2));
    }
}

void Native::AudioServeFastLoad()
{
    // SOUND_FASTLOAD_RPC_DEVICE
    constexpr s32 FastLoadServer = 0x12344321;
    std::lock_guard lock(g_Lock);
    Iop::SetEeServer(FastLoadServer, [](u32 function, void* data, u32 size) -> const void* {
        // The reply the IOP takes is 0x40 bytes, the answer's record and what follows it on the PS2 (zeros here)
        static u8 reply[0x40];
        std::memset(reply, 0, sizeof(reply));
        std::memcpy(reply, MsFastLoadRpc(static_cast<s32>(function), data, static_cast<s32>(size)), sizeof(MsFastLoadAnswer));
        return reply;
    });
}

void Native::AudioPollIop()
{
    Outstanding outstanding = g_Outstanding;
    if (outstanding.call == nullptr)
    {
        return;
    }

    {
        std::lock_guard lock(g_Lock);
        // Frame-locked, nothing else runs the I/O processor: the game's thread asking about the call runs it on (the clock
        // moving on as the PS2's does while the EE waits)
        if (!outstanding.call->done && g_FrameLocked)
        {
            Iop::RunUntilIdle(&g_Made);
            if (!outstanding.call->done)
            {
                Iop::RunSamples(1, &g_Made);
            }
        }

        if (!outstanding.call->done)
        {
            return;
        }
    }

    g_Outstanding = {};
    std::memcpy(outstanding.reply, outstanding.call->reply.data(), outstanding.call->replySize);
    delete outstanding.call;
    if (outstanding.ended != nullptr)
    {
        outstanding.ended(nullptr);
    }
}

// libsd's EE side (SDRDRV's client): the server is on the I/O processor once its modules run
s32 Platform::Sound::InitialiseRemote()
{
    return Native::AudioBootIop() ? 0 : -1;
}

// Platform::Stream's sound side: the PS2's Platform::Stream (src/platform/ps2/stream.cpp), its fast load's EE server the native
// build's (no thread of its own: the I/O processor's calls run it)
void Native::AudioStreams::Initialise(s32 channels)
{
    // The module keeps room for so many files per channel
    constexpr s32 FilesPerChannel = 4;
    MsInitialise();
    MsInitDisc(DiscDvd);
    MsInitDisc(DiscSpinStream);
    MsInitStreamData(LoadInternal, static_cast<u16>(channels * FilesPerChannel), 0);
    MsSetStreamCount(static_cast<u32>(channels));
    MsInitFastLoad(0, nullptr, 0);
    MsSetFastLoadMode(FastLoadOn);
    MsSendBatch(SendWait);
    MsSetFastLoadMode(FastLoadContinue);
}

void Native::AudioStreams::Update()
{
    if (g_FrameLocked && g_Booted)
    {
        std::vector<s16> samples;
        {
            std::lock_guard lock(g_Lock);
            samples.swap(g_Made);
            Iop::RunSamples(FrameSamples, &samples);
        }

        Play(samples);
    }

    MsHandleDiscErrors();
    if (MsIsBusy() != 0)
    {
        return;
    }

    MsSend(SendNoWait);
}

void Native::AudioStreams::FileOpened(s32 file, const char* path)
{
    // The module takes the disc's path: "cdrom0:\", upper case, backslashes
    constexpr char Prefix[] = "cdrom0:\\";
    char discPath[0x100];
    u32 length = 0;
    for (; Prefix[length] != 0; length++)
    {
        discPath[length] = Prefix[length];
    }

    for (const char* c = path; *c != 0 && length < sizeof(discPath) - 1; c++)
    {
        char character = *c;
        if (character >= 'a' && character <= 'z')
        {
            character = static_cast<char>(character - 'a' + 'A');
        }
        else if (character == '/')
        {
            character = '\\';
        }

        discPath[length++] = character;
    }

    discPath[length] = 0;
    MsOpenFile(file, discPath, 0);
}

void Native::AudioStreams::FileClosed(s32 file)
{
    MsCloseFile(static_cast<u32>(file));
    MsSend(SendWait);
}

void Native::AudioStreams::AttachBuffer(s32 channel, u32 size, u32 soundAddress, u32 soundSize)
{
    MsAllocateStreamBuffer(channel, soundAddress, size);
    if (soundAddress != 0 && soundSize != 0)
    {
        MsResizeSpuBuffer(channel, soundSize);
    }
}

void Native::AudioStreams::DetachBuffer(s32 channel)
{
    MsCloseStreamBuffer(static_cast<u32>(channel));
}

u32 Native::AudioStreams::FreeBufferMemory()
{
    MsQueryFreeMemory();
    MsSendBatch(SendWait);
    StreamStatus status;
    MsGetStreamStatus(0, &status);
    return status.maxIopMemory;
}

void Native::AudioStreams::ReadSoundBank(s32 channel, u32 bank, s32 file, u32 offset, u32 size)
{
    u32 address = g_MsSoundBankAddress;
    MsSetBankAddress(bank, address);
    MsReadFile(static_cast<u32>(file), offset, size);
    MsLoadFile(LoadToSpu, static_cast<u8>(channel), static_cast<u32>(file), address);
}

bool Native::AudioStreams::IsReading(s32 channel)
{
    StreamStatus status;
    MsGetStreamStatus(static_cast<u8>(channel), &status);
    return status.state != StreamOff;
}

// MsWaitForStream (src/platform/ps2/multistream/streams.cpp) cold, its wait for the I/O processor's next run letting it run on
void Native::AudioStreams::Wait(s32 channel)
{
    auto iopUpdates = []() { return *static_cast<volatile u32*>(&g_MsIopUpdates); };
    auto waitForIop = []() {
        std::unique_lock lock(g_Lock);
        if (g_FrameLocked || !g_Running)
        {
            Iop::RunSamples(1, &g_Made);
        }
        else
        {
            g_Progress.wait_for(lock, std::chrono::milliseconds(1));
        }
    };
    s32 stream = channel;
    if (stream >= g_MsStreamCount)
    {
        return;
    }

    while (MsIsBusy() == 1)
    {
    }

    Begin(OpInitWait);
    Push32(static_cast<u32>(reinterpret_cast<uiptr>(&g_MsIopUpdates)));
    MsCommit();
    Begin(OpGetStatus);
    Push(NoStreamAllowed);
    Push(0);
    MsCommit();
    g_MsStatusRequested = 1;
    MsSend(SendWait);
    g_MsLastIopUpdate = iopUpdates();
    StreamStatus status;
    MsGetStreamStatus(stream, &status);
    static const bool debug = std::getenv("TWINSANITY_AUDIO_DEBUG") != nullptr;
    for (u32 round = 0; status.state != StreamOff; round++)
    {
        if (debug && (round < 4 || round % 2000 == 0))
        {
            Native::Log("audio: wait %d: state %d (ee %d), request %d, updates %u/%u", stream, status.state, g_MsStreamStates[stream],
                        MsGetLoadRequest(), iopUpdates(), g_MsLastIopUpdate);
            std::lock_guard lock(g_Lock);
            Iop::LogState();
        }

        if (iopUpdates() != g_MsLastIopUpdate)
        {
            g_MsLastIopUpdate = iopUpdates();
            s32 request = MsGetLoadRequest();
            // The stream asking may load (MultiStream's own loading; the native build has no other)
            if (request != -1 && static_cast<u8>(g_MsIopLoadType) == LoadByModule)
            {
                g_MsDiscBusy = 1;
                g_MsLoadRequest = -1;
                Begin(OpGetStatus);
                Push(static_cast<u16>(request));
                Push(0);
                MsCommit();
                g_MsStatusRequested = 1;
            }

            MsSend(SendWait);
            MsHandleDiscErrors();
        }
        else
        {
            waitForIop();
        }

        MsGetStreamStatus(stream, &status);
    }

    if (debug)
    {
        Native::Log("audio: wait %d done", stream);
        std::lock_guard lock(g_Lock);
        Iop::LogState();
    }

    MsCloseWaitUpdate();
    MsSend(SendWait);
}
