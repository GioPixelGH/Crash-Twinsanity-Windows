// The movies (Platform::Movie) natively: the PS2's player (src/platform/ps2/movie/player.cpp) with FFmpeg's MPEG-2 decoder in
// the IPU's place. The PSS file is demultiplexed as libmpeg does it (pss.h); its pictures are decoded by libavcodec and turned
// into RGB the way the IPU's colour conversion does it (PCSX2's IPU yuv2rgb_reference: its coefficients, the chroma of each 2x2
// pixels, alpha 0x80); they go to the GS through the graphics' movie functions (graphics/movie.h) when the game presents, two
// queued at most as on the PS2. The sound is the PS2's: its PCM goes a KB at a time into a ring in the I/O processor's memory
// (the music buffer the game lends) that SDRDRV's block transfer has the SPU2's core 0 play in a loop (its AutoDMA input), the
// emulated I/O processor's (audio/audio.h).
//
// Time: the PS2 presents a picture every second vertical blank (25 a second, the movies' rate). Natively a picture is presented
// every 1/rate seconds of the movie (its sequence header's frame rate) by the sound processor's clock, which plays the sound, so
// pictures and sound stay together whatever the display's rate
#include "audio/audio.h"
#include "graphics/movie.h"
#include "native.h"
#include "platform/movie.h"
#include "pss.h"

extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/frame.h>
}

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace
{
// The PS2's lent memory: the disc stream's buffer (0x50 sectors, 64 byte aligned), then the sound's ring
constexpr u32 DiscBufferSize = 0x50 * 0x800 + 0x40;
constexpr u32 SoundRingSize = 0x6000;
// The sound goes to the ring a KB at a time
constexpr u32 SoundBlockBytes = 0x400;
// SDRDRV's commands and the SPU2's: core 0's block transfer (looping write, stop, its status) and its input volume
constexpr s32 SdBlockTransfer = 0x80E0;
constexpr s32 SdBlockTransferStatus = 0x8100;
constexpr s32 SdSetParameter = 0x8010;
constexpr u32 SdCore0 = 0;
constexpr u32 SdBlockLoopWrite = 0x13;
constexpr u32 SdBlockStop = 2;
constexpr u32 SdInputVolumeLeft = 0xF80;
constexpr u32 SdInputVolumeRight = 0x1080;
constexpr f32 SdMaximumVolume = 32767.0f;
constexpr u32 SampleRate = 48000;
constexpr s32 PictureQueueSize = 2;

struct Picture
{
    std::vector<u8> rgba;
    s32 width = 0;
    s32 height = 0;
};

struct Movie
{
    NativeMovie::Pss pss;
    AVCodecContext* codec = nullptr;
    AVCodecParserContext* parser = nullptr;
    AVPacket* packet = nullptr;
    AVFrame* frame = nullptr;
    bool flushed = false;
    bool finished = false;
    // The picture decoded last and the two waiting to be shown (the PS2's decoded and queue)
    std::unique_ptr<Picture> decoded;
    std::unique_ptr<Picture> queue[PictureQueueSize];
    s32 queued = 0;
    // The ring in the I/O processor's memory
    u32 ring = 0;
    bool ringLent = false;
    u32 ringWrite = 0;
    u32 ringFilled = 0;
    bool soundStarted = false;
    // The pictures' rate and the sound processor's sample the presenting counts from
    double rate = 25.0;
    u64 startSample = 0;
    u64 presented = 0;
};

void (*g_Present)() = nullptr;

// The IPU's YCbCr to RGB (PCSX2's yuv2rgb_reference), alpha 0x80
void ConvertPicture(const AVFrame* frame, Picture* picture)
{
    constexpr s32 YCoefficient = 0x95;
    constexpr s32 GreenCr = -0x68;
    constexpr s32 GreenCb = -0x32;
    constexpr s32 RedCr = 0xCC;
    constexpr s32 BlueCb = 0x102;
    picture->width = frame->width;
    picture->height = frame->height;
    picture->rgba.resize(static_cast<size_t>(frame->width) * frame->height * 4);
    u8* out = picture->rgba.data();
    for (s32 y = 0; y < frame->height; y++)
    {
        const u8* luma = frame->data[0] + static_cast<ptrdiff_t>(y) * frame->linesize[0];
        const u8* cb = frame->data[1] + static_cast<ptrdiff_t>(y >> 1) * frame->linesize[1];
        const u8* cr = frame->data[2] + static_cast<ptrdiff_t>(y >> 1) * frame->linesize[2];
        for (s32 x = 0; x < frame->width; x++)
        {
            s32 lum = (YCoefficient * std::max(0, static_cast<s32>(luma[x]) - 16)) >> 6;
            s32 crValue = static_cast<s32>(cr[x >> 1]) - 128;
            s32 cbValue = static_cast<s32>(cb[x >> 1]) - 128;
            s32 rcr = (RedCr * crValue) >> 6;
            s32 gcr = (GreenCr * crValue) >> 6;
            s32 gcb = (GreenCb * cbValue) >> 6;
            s32 bcb = (BlueCb * cbValue) >> 6;
            *out++ = static_cast<u8>(std::clamp((lum + rcr + 1) >> 1, 0, 255));
            *out++ = static_cast<u8>(std::clamp((lum + gcr + gcb + 1) >> 1, 0, 255));
            *out++ = static_cast<u8>(std::clamp((lum + bcb + 1) >> 1, 0, 255));
            *out++ = 0x80;
        }
    }
}

// $TWIN_MOVIE_GUARD=1 (tests, THE LIST 58): each piece of the stream the parser reads copied to end where a page that can't be read
// begins, so that a read past its end crashes every time instead of now and then (#49). Else the piece itself
const u8* Guarded(const std::vector<u8>& data)
{
    static const bool guard = std::getenv("TWIN_MOVIE_GUARD") != nullptr;
    static u8* region = nullptr;
    static size_t readable = 0;
    if (!guard)
    {
        return data.data();
    }

#ifdef _WIN32
    SYSTEM_INFO system;
    GetSystemInfo(&system);
    const size_t page = system.dwPageSize;
#else
    const size_t page = static_cast<size_t>(sysconf(_SC_PAGESIZE));
#endif
    size_t needed = (data.size() + page - 1) / page * page;
    if (region == nullptr || needed > readable)
    {
        readable = std::max<size_t>(needed, 0x10000);
#ifdef _WIN32
        region = static_cast<u8*>(VirtualAlloc(nullptr, readable + page, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
        DWORD old;
        VirtualProtect(region + readable, page, PAGE_NOACCESS, &old);
#else
        region = static_cast<u8*>(mmap(nullptr, readable + page, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0));
        mprotect(region + readable, page, PROT_NONE);
#endif
        Native::Log("movie: the stream's pieces read before a page that can't be read ($TWIN_MOVIE_GUARD)");
    }

    u8* at = region + readable - data.size();
    std::memcpy(at, data.data(), data.size());
    return at;
}

// The next picture in display order: false once there's none
bool DecodeNext(Movie* movie)
{
    if (movie->finished)
    {
        return false;
    }

    while (true)
    {
        int received = avcodec_receive_frame(movie->codec, movie->frame);
        if (received == 0)
        {
            if (movie->frame->format != AV_PIX_FMT_YUV420P)
            {
                Native::Log("movie: pictures of format %d", movie->frame->format);
            }

            auto picture = std::make_unique<Picture>();
            ConvertPicture(movie->frame, picture.get());
            movie->decoded = std::move(picture);
            if (movie->codec->framerate.num > 0 && movie->codec->framerate.den > 0)
            {
                movie->rate = av_q2d(movie->codec->framerate);
            }

            return true;
        }

        if (received == AVERROR_EOF)
        {
            movie->finished = true;
            return false;
        }

        // The decoder wants more of the stream
        std::vector<u8> data;
        if (!movie->pss.NextVideo(&data))
        {
            if (!movie->flushed)
            {
                movie->flushed = true;
                av_parser_parse2(movie->parser, movie->codec, &movie->packet->data, &movie->packet->size, nullptr, 0,
                                 AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
                if (movie->packet->size > 0)
                {
                    avcodec_send_packet(movie->codec, movie->packet);
                }

                avcodec_send_packet(movie->codec, nullptr);
                continue;
            }

            movie->finished = true;
            return false;
        }

        // The parser reads up to AV_INPUT_BUFFER_PADDING_SIZE bytes past a piece's end (ff_combine_frame copies a picture's end
        // with that much after it): they're there, zeros. Without them it read past the vector's end, which crashed as the
        // memory after it happened not to be there (#49, after the Twins' backstory scene; THE LIST 58)
        int left = static_cast<int>(data.size());
        data.resize(data.size() + AV_INPUT_BUFFER_PADDING_SIZE, 0);
        const u8* at = Guarded(data);
        while (left > 0)
        {
            int used = av_parser_parse2(movie->parser, movie->codec, &movie->packet->data, &movie->packet->size, at, left,
                                        AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
            if (used < 0)
            {
                break;
            }

            at += used;
            left -= used;
            if (movie->packet->size > 0)
            {
                avcodec_send_packet(movie->codec, movie->packet);
            }
        }
    }
}

// The picture decoded last waits to be shown when fewer than two do. Returns whether it went in
bool QueueDecoded(Movie* movie)
{
    if (movie->decoded == nullptr || movie->queued >= PictureQueueSize)
    {
        return false;
    }

    movie->queue[movie->queued++] = std::move(movie->decoded);
    return true;
}

void WriteRing(Movie* movie, const u8* data, u32 size)
{
    while (size != 0)
    {
        u32 piece = std::min(size, SoundRingSize - movie->ringWrite);
        Native::AudioWriteIop(movie->ring + movie->ringWrite, data, piece);
        movie->ringWrite = (movie->ringWrite + piece) % SoundRingSize;
        data += piece;
        size -= piece;
    }
}

// The sound goes to the ring a KB at a time: up to where the SPU2 plays once it started, else until the ring is full (the PS2's
// FeedSound)
void FeedSound(Movie* movie)
{
    u32 room;
    if (movie->soundStarted)
    {
        u32 status = static_cast<u32>(Native::AudioSdRemote(SdBlockTransferStatus, SdCore0, 0));
        u32 playing = (status & 0xFFFFFF) - movie->ring;
        room = (playing + SoundRingSize - movie->ringWrite - SoundBlockBytes) % SoundRingSize / SoundBlockBytes * SoundBlockBytes;
    }
    else
    {
        room = (SoundRingSize - movie->ringFilled) / SoundBlockBytes * SoundBlockBytes;
    }

    size_t waiting = movie->pss.FillSound(room);
    u32 size = static_cast<u32>(std::min<size_t>(room, waiting / SoundBlockBytes * SoundBlockBytes));
    if (size == 0)
    {
        return;
    }

    std::vector<u8> data(size);
    movie->pss.TakeSound(data.data(), size);
    WriteRing(movie, data.data(), size);
    movie->ringFilled += size;
}

// $TWINSANITY_MOVIE_DUMP=folder: every 25th picture shown written there as a PPM (tests)
void DumpPicture(const Picture& picture)
{
    static const char* folder = std::getenv("TWINSANITY_MOVIE_DUMP");
    static u32 shown = 0;
    if (folder == nullptr || shown++ % 25 != 0)
    {
        return;
    }

    char path[1024];
    std::snprintf(path, sizeof(path), "%s/picture_%04u.ppm", folder, shown - 1);
    if (FILE* file = std::fopen(path, "wb"))
    {
        std::fprintf(file, "P6\n%d %d\n255\n", picture.width, picture.height);
        for (size_t i = 0; i < picture.rgba.size(); i += 4)
        {
            std::fwrite(&picture.rgba[i], 1, 3, file);
        }

        std::fclose(file);
    }
}

void SetSoundVolume(f32 volume)
{
    u32 value = static_cast<u32>(static_cast<s32>(volume * SdMaximumVolume));
    Native::AudioSdRemote(SdSetParameter, SdInputVolumeLeft, value);
    Native::AudioSdRemote(SdSetParameter, SdInputVolumeRight, value);
}

void Release(Movie* movie)
{
    avcodec_free_context(&movie->codec);
    if (movie->parser != nullptr)
    {
        av_parser_close(movie->parser);
    }

    av_packet_free(&movie->packet);
    av_frame_free(&movie->frame);
    if (movie->ring != 0 && !movie->ringLent)
    {
        Native::AudioIopFree(movie->ring);
    }

    delete movie;
}
}

// The libraries the app runs with (they can be swapped: native/third_party/ffmpeg/README.md). The app's own build is "LGPL
// version 2.1 or later"; anything GPL (Homebrew's) is said so, since an app carrying it can't be shared under the LGPL
void Native::LogMovieLibraries()
{
    const char* codecLicence = avcodec_license();
    const char* utilLicence = avutil_license();
    unsigned codec = avcodec_version();
    unsigned util = avutil_version();
    Native::Log("movie: FFmpeg %s: libavcodec %u.%u.%u (%s), libavutil %u.%u.%u (%s)", av_version_info(),
                AV_VERSION_MAJOR(codec), AV_VERSION_MINOR(codec), AV_VERSION_MICRO(codec), codecLicence, AV_VERSION_MAJOR(util),
                AV_VERSION_MINOR(util), AV_VERSION_MICRO(util), utilLicence);
    if (std::strncmp(codecLicence, "LGPL", 4) != 0 || std::strncmp(utilLicence, "LGPL", 4) != 0)
    {
        Native::Log("movie: these FFmpeg libraries aren't LGPL: build the app's own with native/tools/build_ffmpeg.sh to share it");
    }
}

// The player in the game's movie controller: the native state's pointer and the pictures stepped since Open (the PS2's frames)
struct Platform::Movie::Player
{
    ::Movie* movie;
    s32 frames;
};
static_assert(sizeof(Platform::Movie::Player) <= Platform::Movie::PlayerSize);

void Platform::Movie::Construct(Player* player)
{
    player->movie = nullptr;
    player->frames = 0;
}

void Platform::Movie::BeginPresenting(Player*, void (*present)())
{
    g_Present = present;
}

bool Platform::Movie::Open(Player* player, const char* file, u32 audioChannel, s32, void* lentMemory)
{
    player->frames = 0;
    Native::Log("movie: %s", file);
    auto* movie = new ::Movie;
    if (!movie->pss.Open(file, audioChannel))
    {
        delete movie;
        return false;
    }

    const AVCodec* decoder = avcodec_find_decoder(AV_CODEC_ID_MPEG2VIDEO);
    movie->codec = avcodec_alloc_context3(decoder);
    movie->parser = av_parser_init(AV_CODEC_ID_MPEG2VIDEO);
    movie->packet = av_packet_alloc();
    movie->frame = av_frame_alloc();
    if (decoder == nullptr || movie->codec == nullptr || movie->parser == nullptr ||
        avcodec_open2(movie->codec, decoder, nullptr) < 0)
    {
        Native::Log("movie: FFmpeg's MPEG-2 decoder didn't start");
        Release(movie);
        return false;
    }

    // The ring in the memory the game lends (an I/O processor address: a music stream's buffer), else the I/O processor's heap
    u32 lent = static_cast<u32>(reinterpret_cast<uiptr>(lentMemory));
    movie->ringLent = lent != 0;
    movie->ring = lent != 0 ? lent + DiscBufferSize : Native::AudioIopAllocate(SoundRingSize);
    player->movie = movie;
    // The first two pictures, and the sound's ring filled
    DecodeNext(movie);
    QueueDecoded(movie);
    DecodeNext(movie);
    FeedSound(movie);
    if (movie->pss.Header().type != 0 && movie->pss.Header().rate != SampleRate)
    {
        Native::Log("movie: %s's sound is at %u Hz", file, movie->pss.Header().rate);
    }

    return true;
}

// The SPU2 plays the ring in a loop
void Platform::Movie::StartSound(Player* player, f32 volume)
{
    ::Movie* movie = player->movie;
    if (movie == nullptr)
    {
        return;
    }

    Native::AudioSdRemote(SdBlockTransfer, SdCore0, SdBlockLoopWrite, movie->ring, SoundRingSize / SoundBlockBytes * SoundBlockBytes,
                          movie->ring);
    SetSoundVolume(volume);
    movie->soundStarted = true;
    movie->startSample = Native::AudioSamplesMade();
    movie->presented = 0;
}

bool Platform::Movie::Step(Player* player)
{
    ::Movie* movie = player->movie;
    if (movie == nullptr || (movie->finished && movie->decoded == nullptr))
    {
        return false;
    }

    if (QueueDecoded(movie))
    {
        DecodeNext(movie);
        FeedSound(movie);
        player->frames++;
    }

    return true;
}

// A picture is presented every 1/rate seconds of the sound processor's clock
void Platform::Movie::WaitFrame(Player* player)
{
    // A movie's frames are the game thread's too (its pace is the sound's, not the vertical blank's): the hang watch counts them
    Native::GameFrameDone();
    ::Movie* movie = player->movie;
    if (movie != nullptr)
    {
        movie->presented++;
        u64 due = movie->startSample + static_cast<u64>(static_cast<double>(movie->presented) * SampleRate / movie->rate);
        Native::AudioWaitForSample(due);
    }

    if (g_Present != nullptr)
    {
        g_Present();
    }
}

void Platform::Movie::QueuePicture(Player* player)
{
    ::Movie* movie = player->movie;
    if (movie == nullptr || movie->queued == 0)
    {
        return;
    }

    const Picture& picture = *movie->queue[0];
    DumpPicture(picture);
    NativeGraphicsUploadMoviePicture(picture.rgba.data(), picture.width, picture.height, picture.width * 4);
    movie->queue[0] = std::move(movie->queue[1]);
    movie->queued--;
}

void Platform::Movie::Draw(Player* player, const Area& area, s32 width, s32 height, u32 skippedRows)
{
    if (player->frames < 2)
    {
        return;
    }

    NativeGraphicsDrawMoviePicture(area, width, height, skippedRows);
}

// The sound stops and the ring goes, the decoder closes
void Platform::Movie::Close(Player* player)
{
    ::Movie* movie = player->movie;
    if (movie == nullptr)
    {
        return;
    }

    SetSoundVolume(0.0f);
    Native::AudioSdRemote(SdBlockTransfer, SdCore0, SdBlockStop, 0, 0);
    Release(movie);
    player->movie = nullptr;
}
