# The native build's sound and movies

The PS2 game reaches its sound processor through MultiStream: the EE side (`src/platform/ps2/multistream/`, SCEE's library as
C++) batches commands for STREAM.IRX on the I/O processor, which drives the SPU2 through Sony's libsd (LIBSD.IRX); the movies'
sound goes through libsd's RPC server, SDRDRV.IRX. The native build keeps all of that as it is and emulates the hardware under
it:

- **The game's own IOP modules run.** LIBSD.IRX, SDRDRV.IRX and STREAM.IRX are loaded from the disc, relocated into 2 MB of IOP
  memory and their MIPS R3000 code interpreted (`src/platform/native/audio/iop.cpp`). The IOP kernel's libraries they import
  (thbase, thsemap, thevent, intrman, timrman, sysmem, sifman, sifcmd, sysclib, stdio, loadcore, cdvdman, ioman) are the
  emulator's, with the IOP's semantics: priority scheduling with preemption, semaphores, event flags, interrupt handlers and
  CpuSuspendIntr, the hardware timer's handler, the heap, SIF RPC servers, the drive's asynchronous reads from the disc image.
- **The SPU2 is emulated at its registers** (`spu2.cpp`): libsd's register writes and DMA (channels 4 and 7, AutoDMA) reach it
  exactly as on the PS2. Its behaviour is PCSX2's SPU2 (`pcsx2/SPU2`) written again: PS-ADPCM with its block flags (loop start,
  loop end, stop) and the decoder's queue NAX runs ahead with, the 4-point gaussian interpolation (the hardware's table,
  `gaussian.h`), the pitch counter and pitch modulation, the ADSR envelope's rates and curves, the volume sweeps, the noise
  generator, key on and off taking effect at the next sample, ENDX, IRQA (every address read or written, both cores), the reverb
  (its 39 tap down/upsampling, its work area, every register), the cores' gates (MMIX, VMIX/VMIXE), the writes of
  the voices and mixes into the memory's output areas, core 0's output into core 1's external input, the master volumes.
- **The EE side is the PS2's.** `src/platform/ps2/audio.cpp`, `multistream/commands.cpp` and `multistream/streams.cpp` compile
  natively unchanged (`native/reused_ps2.txt`). The transport (`audio/multistream.cpp`) is the PS2's MsCommit, MsSend and
  MsReadReply, the RPC call going to the emulated IOP instead of the SIF; the fast load's EE server (MsFastLoadRpc) is the
  PS2's too, called by the IOP's sceSifCallRpc. Platform::Stream's sound side (opening and closing files on the module, the
  channels' buffers, the free IOP memory, sound bank reads, IsReading and Wait) is the PS2's Platform::Stream through the same
  calls (`audio/streams.h`, called from `src/platform/native/stream.cpp`); reads into the EE's memory stay stream.cpp's.
- **Why this way.** Everything the game's sound does on the PS2 is decided by STREAM.IRX (SCEE's module with the game's own
  additions: commands 0x101-0x105, its own allocator of SPU memory that places each sound and compacts them every 36 frames, the
  loop counting by ENDX, the smooth volume ramps by sweeps, the music's interleaved streaming, double buffering and end offsets,
  the fast load's load arbitration) and by libsd (the reverb presets' register values, the effect areas, DMA). Running those
  modules gives the PS2's behaviour without re-deriving any of it; what's emulated is hardware with public, well tested
  behaviour (the R3000, the IOP kernel's documented calls, the SPU2 as PCSX2 has it).

Output: the samples go to an SDL3 audio stream at 48 kHz, made by an audio thread a few milliseconds at a time and kept about
33 ms ahead of what plays (the IOP and SPU2 belong to whoever holds the lock: the audio thread, or the game's thread while it
waits for a call).

## Settings

| Environment | Effect |
|---|---|
| `TWINSANITY_AUDIO=off` | Nothing plays; the sound is still made, in time with the clock |
| `TWINSANITY_AUDIO=frames` | Nothing plays; a PAL frame's samples (960) are made at each Platform::Stream::Update (and as the game waits for the IOP), the same sound every run whatever the machine's speed (tests) |
| `TWINSANITY_AUDIO_WAV=path` | What's made is written to a WAV file |
| `TWINSANITY_AUDIO_TRACE=1` | MultiStream's batches logged, a command a line |
| `TWINSANITY_AUDIO_DEBUG=1` | The IOP's threads logged when a call or a wait is slow |
| `TWINSANITY_IOP_WATCH=12f2c,12f30` | With the debug log: STREAM.IRX's words at those offsets |
| `TWINSANITY_MOVIE_DUMP=folder` | Every 25th movie picture shown written there (PPM) |

## The player's volumes

`audio/volumes.h`: `NativeAudioSetVolumes(music, effects, voice)` (0 to 1 each, 1 to start with) and `NativeAudioSetMuted(bool)`.
They act after everything the game sets: each SPU2 voice's output is scaled by its kind's volume after its own volume (so its
reverb send too), the cores' input (the movies) by the music's, and muting zeroes the output (the emulation runs on). A voice's
kind comes from what the game asked MultiStream to play on it (the batches are read as they're sent): a stream of a file of
`Crash6\Music` is music; a stream of a language's bank (`Crash6\English.MB` and the others: the dialogue the game plays as music
through its music players) is a voice; a sound of a sound bank (PlaySound) is a voice when its ID is one of the language's voices'
table (the game's resource table of voices, which SoundById looks in after the sounds'; the PS2's game puts those in sound
banks too), else an effect. The game's own volume groups don't separate these (group 3 holds movies, cutscenes' sound, voices
and menu sounds), so the groups aren't used for it.

The master volume (since 1.3.0, THE LIST 81; Options > Audio's first row, `master_volume` 0-100 in options.txt, 100 by default,
stepped by 5) is `NativeAudioSetMasterVolume(gain)`: SDL's gain on the output stream (`SDL_SetAudioStreamGain`, audio.cpp), so it
scales everything together as it goes to the device, the movies' sound included (it reaches the output through the SPU2's
input, as above), after the volumes above, which keep their own values. Kept for a stream opened later; a run without sound
(headless, `$TWINSANITY_AUDIO_WAV`'s file) isn't scaled.

## The movies

`src/platform/native/movie/`: Platform::Movie as the PS2's player (`src/platform/ps2/movie/player.cpp`) with FFmpeg's libavcodec
in the IPU's place. The PSS is demultiplexed as libmpeg does (video stream 0xE0; the sound is private stream 1's sub-stream
FF A0 00 channel, Sony's 16 bit PCM, 48 kHz, its two sides interleaved in 0x200 byte blocks after a 0x28 byte SShd/SSbd header).
Pictures are converted to RGB with the IPU's colour conversion (PCSX2's `yuv2rgb_reference`: its coefficients, the chroma of each
2x2 pixels, alpha 0x80), queued two at most and sent to the GS by the graphics' `NativeGraphicsUploadMoviePicture` when the game
presents; `Draw` is `NativeGraphicsDrawMoviePicture` once two pictures were stepped. The sound is the PS2's path: the PCM goes a
KB at a time into the ring in the IOP memory the game lends (a music stream's buffer, past the disc buffer's 0x28040 bytes), and
SDRDRV's looping block transfer (sceSdBlockTrans 0x13 on core 0) has the SPU2's AutoDMA input play it, the game's volume being
core 0's input volume.

The FFmpeg is the app's own: `native/tools/build_ffmpeg.sh` builds 9.0.2's libavcodec and libavutil with nothing but the MPEG-2
video decoder and the MPEG video parser, LGPL 2.1, shared (native/third_party/ffmpeg/README.md has the configuration and how to
swap the libraries). CMake links it when it's in `build/native/ffmpeg/<os>-<arch>`, else the system's with a warning (Homebrew's
is GPL 3); the start-up log says which, with its licence.

## How the native build differs from the PS2's

| Where | The PS2 | The native build | Effect |
|---|---|---|---|
| The IOP's timing | 36.864 MHz, instructions taking their cycles, caches, the bus | A cycle an instruction; the clock jumps ahead while every thread waits | The IOP's code runs in a different number of cycles; the SPU2's sample timing (768 cycles) and the timer (its 60 Hz) are the PS2's |
| Load delay slots | The R3000's | Not emulated | None: the modules' compiler never reads a loaded register in the next instruction |
| Where the modules load | After the IOP kernel, the IOPRP image's modules and the drivers loaded before them | From 0x60000 on, the heap after them (1491 KB free once they run) | The free IOP memory MultiStream reports may differ from the PS2's (the game only checks the music's buffers fit) |
| The drive | DVD reads at the drive's speed, seeks | Reads from the image at 4x DVD's rate (5.54 MB/s) after a millisecond | Sound banks and music arrive in similar time; no seek times, no disc errors |
| The SIF's RPC reply | Its end function runs in the EE's interrupt as the reply comes | Runs on the game's thread when MultiStream next asks whether it's busy (MsIsBusy, MsSend, Platform::Stream::Update) | The reply's status lands at the next of those points rather than mid-frame |
| The IOP's count of its runs (OpInitWait) | 64 bytes of the module's state DMAed to `g_MsIopUpdates` | The first word, the count, written | None: MultiStream reads only the count |
| Platform::Stream::Wait | MsWaitForStream | The same steps (src/platform/native/audio/audio.cpp), its spin running the IOP on | None |
| The fast load's EE server | A thread of its own | Called directly by the IOP's sceSifCallRpc; no loads go to the EE's memory through the module | None: the native build's reads into the EE's memory are stream.cpp's |
| AutoDMA | The SPU2 asks for a block a half of its input buffer at a time; MADR moves as it's taken | The same; MADR is worked out from what the core has taken | As PCSX2's |
| The player's volumes, muting | No such thing | Applied after the game's volumes (above) | At 1 and unmuted: none |
| Movie pictures | The IPU's decoding (its IDCT) and colour conversion, sent at the vertical blank every second blank | FFmpeg's decoding, the IPU's colour conversion, sent when the game presents, every 1/rate seconds of the SPU2's sample clock (25 a second: the movies' rate) | Pixel values can differ by the IDCTs' rounding; the movie keeps its pace and its sound whatever the display's rate (50 or 60 Hz) |
| Output | The DACs, 48 kHz | SDL3 at 48 kHz, about 33 ms ahead | Latency |
| The reverbs' work areas | In the sound memory, at its top | In memory of their own at the same addresses; STREAM.IRX's guards of the areas taken out (below) | A sound STREAM.IRX places where a reverb works plays (on the PS2 it's silent or garbled) |

## A retail bug: sounds where a reverb works

STREAM.IRX's allocator of the sound memory manages all of it, from its base (0x1D010) to the top (0x1FFFFF), the reverbs' work
areas included. Those are at the top: the first core's reverb to come on works below 0x1FFFFF, the other's below 0x1DFFFF,
each area as large as its mode (up to 0xF6C0 bytes, Space, for the modes the game uses). With the memory full enough, the
allocator places sounds there, and the module loses them: it skips any transfer that reaches an area while its reverb is on
(its own load of a sound, its compaction's reads, while it still counts the sound as there), it clears the area each time it
sets a reverb, and the reverb writes over it all the time. The Italian voice banks are larger than the English, so in Italian
the caverns' crystals (sounds 0x22D and 0x22E, loaded as Cortex's chunk comes) landed in the second core's area once the
tunnel's reverb (Hall) was on: silent crystals (where a sound lands depends on what's loaded before it, so other crystals of
the same level, or another run, could be fine).

The native build keeps the reverbs' areas apart from the sounds: the SPU2's reverb reads and writes memory of its own at the
same addresses (spu2.cpp's `g_EffectMemory`, an area cleared as its reverb comes on), and two of STREAM.IRX's instructions are
changed as it loads (iop.cpp's `KeepReverbsApart`, checked against the module's words first): its check of a transfer's range
no longer reads the areas' sizes, and its setting of a reverb no longer clears the area in the sound memory. The allocator is
the PS2's, so the sounds go where they did, all of the memory stays usable, and a sound in an area plays. The reverb sounds as
before: in the A/B of the caverns' route in English (no sound in an area there), the output with the reverb on is the same
bit for bit, or a few samples apart where the old build's clear of the area (21 ms of the IOP's time) shifted what came after.

## Tests

`native/audio-tests/` builds a harness of the native build's objects that drives Platform::Audio, Platform::Stream and
Platform::Movie the way the game's code does, frame-locked, and writes WAV files:

    cmake -S native/audio-tests -B build/native/audio-tests -G Ninja && cmake --build build/native/audio-tests
    build/native/audio-tests/audio-tests music 7 20 music7.wav     # Crash6\Music's track 7 (StartMusic, PlayMusic)
    build/native/audio-tests/audio-tests voice 12 10 voice12.wav   # a track of the English voices
    build/native/audio-tests/audio-tests sound - 3 sound.wav        # the front end's first sound, from its bank in Crash6\Crash.BD
    build/native/audio-tests/audio-tests reverb - 4 reverb.wav      # the same with core 0's hall reverb
    build/native/audio-tests/audio-tests movie VIVENDI 10 vivendi.wav

Checked so far:
- Music track 7 (interleaved stereo, 32 kHz): each side matches a reference decode of the bank's ADPCM (correlation 0.99 per
  second windows, the residue the gaussian interpolation against linear resampling), its pace the hardware's integer pitch
  (0xAAA: 23 samples slower every 2 s than exactly 32 kHz).
- A voice track (English 12, mono with its VAG header, which STREAM.IRX skips): matches its reference decode (0.98-1.00 per
  quarter second).
- The front end's first sound (ID 0x8000): read by STREAM.IRX from the disc (sceCdRead), DMAed into the SPU memory its allocator
  chose, played by PlaySound with its own pitch (0x759) and ADSR (0x20FF/0x1FCD from its parameter 0x20 and the release 0xD):
  correlation 0.997 with its reference decode, stopped by its end block. With the hall reverb its tail decays over about 2.5 s.
- VIVENDI.PSS: 168 pictures (6.72 s at 25 a second), the pictures right; its sound equal to the file's PCM (correlation 1.0000,
  gain 1.000).
- In the game (`TWINSANITY_AUDIO=frames TWINSANITY_AUDIO_WAV=...`): the start-up's sound bank loads go through STREAM.IRX's
  allocator; the logo movies' and the intro's sound equal their PSS files' PCM (correlation 1.0000).

`native/movie-tests/` hashes every picture of every movie on the disc (pss.cpp, and libavcodec fed as movie.cpp feeds it), so
two FFmpegs can be compared: Homebrew's FFmpeg 9.0.2 (GPL) and our LGPL 9.0.2 give the same hashes for all 19,890 pictures of the
PAL disc's 20 movies.

    cmake -S native/movie-tests -B build/native/movie-tests -G Ninja && cmake --build build/native/movie-tests
    cmake -S native/movie-tests -B build/native/movie-tests-system -G Ninja -DTWIN_FFMPEG_DIR=/opt/homebrew/opt/ffmpeg
    cmake --build build/native/movie-tests-system
    build/native/movie-tests/movie-tests DISC.iso > ours.txt; build/native/movie-tests-system/movie-tests DISC.iso > system.txt
    cmp ours.txt system.txt

Still to check: the game's sound effects and reverb in play (the native build gets to the beach slowly while the graphics are
being made faster, and `--quick` currently crashes in pickups.cpp's Update before play), and a comparison with PCSX2's own
capture of the same moments (no PCSX2 run was made for this: other work uses the PCSX2 instances on this machine).
