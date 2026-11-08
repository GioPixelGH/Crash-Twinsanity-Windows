// The streams (the PS2's MultiStream: the disc read in the background on numbered channels, into main memory or into the sound
// processor's). The native build reads from the image as each read is asked for, so a read is done by the time the game polls
// it: the game's readers wait on IsReading and Wait, which both say done. Nothing about what's read or in what order differs.
// What streams into the sound processor (sound banks, music) and the channels' buffers go to MultiStream on the emulated I/O
// processor as on the PS2 (audio/streams.h): files are opened there too, and a sound bank's read is done when the module says
#include "audio/streams.h"
#include "native.h"
#include "platform/stream.h"

#include <cstring>
#include <mutex>
#include <unordered_map>

namespace Native
{
alignas(64) u8 g_SpuMemory[SpuMemorySize];
}

namespace
{
Platform::Stream::State g_State = Platform::Stream::State::Stopped;
std::mutex g_Lock;
// Every file opened gets the next number, as the PS2 side's do
s32 g_NextFile = 0;
std::unordered_map<s32, Native::Disc::Entry> g_Files;
// What the last opening found (FileSize answers for it), 0 when it found nothing
u32 g_LastSize = 0;
// Where the next sound bank goes in the sound processor's memory: audio_silent.cpp's (the emulated I/O processor keeps its own,
// MultiStream's g_MsSoundBankAddress)
u32 g_BankAddress = 0;
}

namespace Native
{
void SetSoundBankAddress(u32 address)
{
    g_BankAddress = address;
}
u32 SoundBankAddress()
{
    return g_BankAddress;
}
}

s32 Platform::Stream::Initialise(s32 channels, void*)
{
    Native::AudioStreams::Initialise(channels);
    g_State = State::Running;
    return 0;
}

s32 Platform::Stream::Update()
{
    Native::AudioStreams::Update();
    return 0;
}

Platform::Stream::State Platform::Stream::GetState()
{
    return g_State;
}

void Platform::Stream::AttachBuffer(s32 channel, u32 size, u32 soundAddress, u32 soundSize)
{
    Native::AudioStreams::AttachBuffer(channel, size, soundAddress, soundSize);
}

void Platform::Stream::DetachBuffer(s32 channel)
{
    Native::AudioStreams::DetachBuffer(channel);
}

u32 Platform::Stream::FreeBufferMemory()
{
    return Native::AudioStreams::FreeBufferMemory();
}

s32 Platform::Stream::OpenFile(const char* path)
{
    std::lock_guard lock(g_Lock);
    s32 file = g_NextFile++;
    Native::Disc::Entry entry;
    if (Native::Disc::Find(path, &entry))
    {
        g_Files[file] = entry;
        g_LastSize = entry.size;
    }
    else
    {
        Native::Log("stream: no file %s on the disc", path);
        g_LastSize = 0;
    }

    Native::AudioStreams::FileOpened(file, path);
    return file;
}

u32 Platform::Stream::FileSize()
{
    return g_LastSize;
}

void Platform::Stream::CloseFile(s32 file)
{
    {
        std::lock_guard lock(g_Lock);
        g_Files.erase(file);
    }

    Native::AudioStreams::FileClosed(file);
}

void Platform::Stream::Read(s32, s32 file, u32 offset, u32 size, void* destination)
{
    // (Its time for the log's slow frames: a disc image on a slow drive)
    Native::GameWorkSpan span(Native::GameWork::Reading);
    std::lock_guard lock(g_Lock);
    auto found = g_Files.find(file);
    if (found == g_Files.end())
    {
        Native::Log("stream: read of file %d, which isn't open", file);
        return;
    }

    // (An archive's file's mod is past the archive's end: read as it is)
    if (Native::Disc::IsMod(found->second.offset + offset))
    {
        Native::Disc::Read(found->second.offset + offset, destination, size);
        return;
    }

    u32 available = offset < found->second.size ? found->second.size - offset : 0;
    Native::Disc::Read(found->second.offset + offset, destination, size < available ? size : available);
}

void Platform::Stream::ReadSoundBank(s32 channel, u32 bank, s32 file, u32 offset, u32 size)
{
    Native::AudioStreams::ReadSoundBank(channel, bank, file, offset, size);
}

bool Platform::Stream::IsReading(s32 channel)
{
    return Native::AudioStreams::IsReading(channel);
}

s32 Platform::Stream::Wait(s32 channel)
{
    Native::AudioStreams::Wait(channel);
    return 0;
}
