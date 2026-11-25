# The native build on Windows

The native build (native/NATIVE.md) for 64 bit Windows: the same game C++ and the same platform layer (`src/platform/native/`),
built with [LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw)'s clang (UCRT), SDL3's MinGW development release and an LGPL
shared FFmpeg. Everything Windows needs differently is under `_WIN32` (or in a file of its own), so the macOS and Linux builds stay
as they were.

## What you need

| What | Where from |
| --- | --- |
| LLVM-MinGW (UCRT) | `winget install MartinStorsjo.LLVM-MinGW.UCRT` |
| CMake and Ninja | `winget install Kitware.CMake Ninja-build.Ninja` (requirements.txt's ninja works too) |
| Python 3.10 or later and the split | README_DECOMP.md, "Building on Windows": the virtual environment, `tools/split.py` |
| PS2SDK's toolchain | README_DECOMP.md, "Building on Windows" (its headers lay the retail data out: native/tools/convert_data.py) |
| SDL3 (MinGW) and FFmpeg | `native/tools/build_windows.py` fetches SDL3 into `build/native/deps` and builds our decode-only LGPL FFmpeg (`native/tools/build_ffmpeg.sh` in Git Bash, with an MSYS `make` it fetches from MSYS2's repository) into `build/native/ffmpeg/windows-x86_64`; without Git Bash it falls back to a prebuilt LGPL shared FFmpeg (every decoder: 90 MB more) |

## Building and running

    .venv\Scripts\python.exe native\tools\build_windows.py
    build\native\cmake\CrashTwinsanity.exe --iso "C:\path\to\Crash Twinsanity (Europe).iso"

`build_windows.py` finds LLVM-MinGW (on the PATH, `$LLVM_MINGW`, or where winget puts it), configures `build/native/cmake` the
first time (`--configure` again, `--clean` from nothing) and builds. The program, SDL's and FFmpeg's DLLs and the UI's fonts end
up together in `build/native/cmake`. Without `--iso`, the disc image is `$TWINSANITY_ISO`, local.json's `disc_image` (in the
working folder), the one picked before, or asked for with a file dialog.

It's a windowed program: its log goes to the terminal it was started from, or to whatever its output was given (a pipe, a
file), else to `log.txt` in the settings folder. `-DTWIN_WINDOWS_CONSOLE=ON` makes a console program instead.

The settings, saves (`memorycard/`), bindings and texture packs are in `%APPDATA%\Crash Twinsanity\`, or in portable mode in
the `User` folder beside the program (below).

## What Windows needed

| Where | Why | What |
| --- | --- | --- |
| Bitfields (`-mno-ms-bitfields`, native/CMakeLists.txt and native/tools/host.py) | MinGW lays bitfields out the Microsoft way by default: a field of another type starts a new unit. The game's structs (and the data it reads from its files over them) are GCC's | GCC's layout, as on every other host; the layout tools are given the same flag |
| Narrow return values (gcc2.h's `CallNarrowed`, convert_data.py's `mark_narrow_returns`) | The retail vtables' calls read 32 bits (`CallVirtual<u32>`) of functions the C++ declares returning `bool`, `u8` or `u16`. The PS2's and Apple's arm64 callees extend those to the whole register; x86-64's set the low byte and leave the rest. `Sm2WantPolicy::DoesNotWant` answered true at depth 0 and the beach's loader was freed while still the focus | The data converter marks each vtable entry of such a function in its `index` (only virtual bases use it, none of the game's: 0x1000 u8/bool, 0x2000 s8, 0x3000 u16, 0x4000 s16); `CallVirtual` calls those through a pointer of the narrow type, so the compiler extends it. 88 entries |
| Mixed float and integer arguments (abi.h's `TWIN_EABI_CC`) | The retail code passes integers and floats each in their own registers in their order (GCC's EABI), and the C++ declares the functions it calls through its pointers with the floats wherever the EABI lets them be (`PushNode(f32 strength, ObjectNode* node, ...)` called as `(node, 0.0f, other)`). AAPCS64 and System V do the same, so macOS and Linux need nothing; Windows' x64 convention gives an argument the register of its position whatever its kind | `RETAIL_N32`'s functions (the 434 the PS2 build thunks) take the System V convention (`__attribute__((sysv_abi))`). A call through the retail vtables and pointers (`Abi::CallEabi`) goes the way of the function behind it: a vtable's slot can hold one taking the float and one leaving it out (`Agent::Launch`'s, called with the gravity, the velocity and the target: `BasicAgent::Launch` takes only the velocity, and crashed the first boss), which the EABI passes alike. native/tools/sysv_functions.py lists `RETAIL_N32`'s functions for a table of their addresses (src/platform/native/abi_win32.cpp); one of them is called the System V way, any other Windows' own with a mixed call's floats left out. native/tools/check_conventions.py (in the build) checks every function taking floats with its integers is `RETAIL_N32`'s; `--selftest-abi` tries the three cases, `$TWIN_ABI_TRACE` logs each function called across |
| Data alignment (convert_data.py) | x86-64's ABI gives a global array of 16 bytes or more 16 byte alignment and clang uses aligned SSE stores on one the C++ declares (`g_MsStreamStates`: `movdqa`); the converter's `alignas` of less lowered it | Objects of 16 bytes or more are 16 byte aligned |
| Script commands' four floats (game/math.h) | A command's vtable pointer is 8 bytes natively, so the four floats some commands keep (an offset, a velocity, a rotation, an impulse) start at 0x1C or 0x2C; read as a `Vector4` (16 byte aligned) they gave `movaps` 12 bytes past a 16 byte boundary, a fault Windows reports at 0xFFFFFFFFFFFFFFFF (a mine's cradle in Slip Slide Icecapades: `Cmd193_LaunchAtTarget_Execute`) | `VectorFrom` and `VectorTo` copy them a float at a time |
| An object's slots past its own (objects.cpp's `GetObjectModelId`, `GetObjectAnimationId`) | The retail code reads the model or animation ID of any slot it's asked, past the object's array too: whatever the heap has there, another model's ID or one past the models' table (whose items past it are any pointer). The angry skunk a skipped cutscene left taking its object from the scene's director (4 models) played its own slots 13 and 4 through it: drawn as Crash, or a crash setting its collision box from the "OGI" read (a heap pointer 8 bytes off 16: `movaps 0x20(%rdx)` in `SetCollisionOgi`, `FUN_001eb400`+0xd, issue #4's log; not a GameOGI's alignment: one is 192 bytes natively, an entry of the small allocations' size class of 192, 16-byte aligned). On the PS2 the read is as wrong but no aligned load faults; the skip, which the PS2 game never had, was the cause (NATIVE.md, "What a skip leaves") | A slot past the object's array is none (no model change, no animation), as its sound slots already are; logged the first times ("objects: slot N of ... past its M models") |
| `long` (convert_data.py) | 32 bits on Windows (LLP64): pointer-sized integers are `long long` there | The converter's table knows it, and takes a scalar's written type (`uiptr`) before its canonical one |
| The retail code's null pointers (`src/platform/native/lowmemory_win32.cpp`) | Windows never maps the first 64 KB | A vectored exception handler runs a copy of the faulting load or store with its displacement moved by `g_NativeLowMemory`'s address, then jumps back past it (no single step: debuggers see nothing). The executable is linked below 2 GB at a fixed base so that address fits a displacement. Also the crash report (registers, the stack's return addresses as module+offset) |
| Frame pacing (display.cpp, entry.cpp) | Windows' sleeps are as coarse as its timer (up to 15.6 ms late): 50 Hz came out at about 40 | `SDL_DelayPrecise` for the vertical blank's wait, `timeBeginPeriod(1)` at start-up |
| The disc image (disc.cpp) | No `pread` | `ReadFile` with an `OVERLAPPED` offset (the threads reading it share no position), the path as UTF-8 |
| Files (`std::filesystem`) | An open file can't be removed | The non-throwing calls; the self-tests close what they remove |
| DMA addresses (ee/address.cpp) | The renderer's 32 bit DMA addresses take a slot per 64 MB window of host memory, and an unused slot held 0: the same as the first window's base. macOS and Linux never map anything below 4 GB; Windows has the stack and heaps there, and a tag pointing into them took a slot that was never kept (later given to another window: the chain read other memory) | A slot's window is kept with bit 0 set (bases are 64 MB apart) |
| The level titles' pictures (shapes.cpp's `Shape2D::Read`, a retail bug) | A picture's sprite reads its texture unless one of its ID is loaded, then its material. A level's title picture has the texture of the icons' title of the level, loaded with the icons, so its material was read from the texture's bytes (a name 16 MB long): a crash on entering the first boss's area (the level sweep never shows a title) | The texture's bytes skipped when it's loaded already, and referenced as a read one is (`$TWIN_SWEEP_PICTURES` reads every picture OLEG loads) |
| Pointers to instances that are gone (instances.cpp, a retail use after free) | An object node keeps its AgentRef1 and AgentRef2 after their instances are freed; the retail code tells a gone one by the asleep flag its memory still says, which on the PS2 it does long enough. Natively (bigger structures, the memory used again sooner) the flag read another object's data: Cortex carrying his object into the caverns' next chunk while he and Crash roll and fight crashed | A set of the instances made and not destroyed: a reference to one that's gone let go of where the retail code reads it (`ForgetFreedInstance`, logged the first times) |
| An animation of no length (animation.cpp) | Its position is 0 / 0: the host's NaN, an index of 0x80000000, the frame 4 GB on (the PS2's FPU has no NaN, its 32 bit addresses wrap: the values before the frames read). A crash in the caverns | Its first frame; the frames' addresses wrap at 32 bits as the PS2's |
| Floats made integers out of range (`-fno-strict-float-cast-overflow`, native/CMakeLists.txt; HAB3X's 3b2d267 in the Mac port) | The retail code makes every float an integer with one `cvt.w.s`, which saturates: 0x7FFFFFFF from 2^31 up, 0x80000000 from -2^31 down (an unsigned or narrower result is its low bits, so a negative float wraps). x86-64's `cvttss2si` gives 0x80000000 for both signs and NaN; arm64's saturates, so the Mac build never had it. The level sweep built with `-fsanitize=float-cast-overflow` found two, neither with an effect in play: the check intervals of 31 cameras of High Seas Hi-Jinks and the school's roof, +-2^32 clock units (the one at +2^32, gpa03's, came out the most negative number: checked every frame instead of never; but those cameras are never polled, the character checks them), and the HUD sprites' wobble angle (`Wobble`: the anchored constructor never sets its base, a retail bug, so it's the heap's leftovers, text read as a float; either value turns the sprite by whole turns) | Clang's saturating conversions (`--selftest-abi` checks them). The game's C++ makes floats only `s32`s, as the asm does (338 conversions in the LLVM IR, against the executable's 559 `cvt.w.s`, none of GCC's unsigned sequences), so its results are the R5900's; the flag's 0 for a negative float made unsigned and its clamping of narrower types only reach the platform's code (the GS's depth, fog and level of detail, the overlay's colours), on values in range or clamped already. The VU's FTOI saturates by hand as before. The sweep and the pictures as before; no slower (the game thread's median 5.4 ms on the beach, 8.2 in the school's Cortex chunk cogpa08, as 1.0.13's) |
| A gamepad's sticks (ui/bindings.cpp's `Squared`) | The DualShock 2's stick reaches its square's corners; a modern gamepad's reports a circle, a full diagonal 0.71 of each axis (an Xbox Series pad's; an Xbox One pad's goes a little past the circle). The game takes its dead zones off each axis and runs past a push it measures on them: on a diagonal an Xbox Series pad's Crash walked | Each stick's point moved out along its direction so the circle's edge is the square's (an axis's own push unchanged), as PCSX2 scales its axes by 1.33. The keyboard's diagonals were the square's corners already. `--selftest-ui` checks it |
| A crash's report | Where the game was | The player's chunk and the save's after the backtrace |
| stb_truetype (`stbtt_PackFontRangesRenderIntoRects`) | The missing glyph was remembered by its index in its own range's table and copied into later ranges' by it: past a shorter table's end (a crash now and then making the overlay's font atlas) | Its data remembered instead (a change of the bundled header's, marked) |
| The rest | No `posix_memalign`, `localtime_r`, `setenv`; `GLsizeiptr` was `long` | `malloc` (16 byte aligned on Windows), `Native::LocalTime`, `Native::SetEnvironment`, `ptrdiff_t` |

## Speed

The renderer is the PS2's own (native/GRAPHICS.md): its DMA chains run on an emulation of the VIF, VU1 and GS, the GS's primitives
drawn by OpenGL. At the beach (60 Hz, the default) it went from 23 frames a second to the full 60 on the machine it was measured on (an
i7-8700K): the render thread (the GS's side, on the CPU: OpenGL's driver is about 2% of it) went from over 40 ms a frame to about
13.5 in play (65000 primitives in 227 batches). Each change gives the same pictures, checked by dumps of the same frames with and
without it (`$TWIN_FIXED_TIME`, below):

| Where | What | Switch |
| --- | --- | --- |
| `Gs::PagesOfRect` (gsmemory.cpp) | The pages a rectangle meets by page sized regions when the buffer starts on a page (a pixel's address every 8 pixels otherwise) | `$TWIN_GS_PAGES_BY_PIXELS` for the old way, `$TWIN_GS_CHECK_PAGES` compares the two (random rectangles at start, then every call) |
| `Hw::Gl::GetUniformLocation` (hw/gl.cpp) | The answers kept, by program and name, per thread: the hardware renderer's passes asked for their uniforms by name thousands of times a frame | `$TWIN_HW_UNIFORM_CACHE=0` |
| `Gs::DrawPrimitive` (gsdraw.cpp) | The primitive's DrawState kept while no register it's made from is written (`Gs::stateGeneration_`) | `$TWIN_GS_STATE_CACHE=0` |
| `HwGs::RectPages`, the batch key (hwgs.cpp) | The loops' bounds worked out once; the key compared and copied inline | - |
| `Gs::DrawPrimitive`'s state copy, the batch key (gsdraw.cpp, hwgs.cpp) | No copy for the hardware renderer: it draws from the cache's own state (copying its 800 bytes a primitive, even inline, was a twelfth of the thread), and the two places that change it (`DecodeForBackend`, `GlyphForBackend`: `Gs::stateTouched_`) leave the cache to be made again; the software rasteriser draws from a copy (`DrawSoftware`). The key copied only when it changes | - |
| `Gs::WriteImage` (gs.cpp) | The textures streamed every frame (32 and 8 bit) written a block's run of a row at a time: the block's address and page worked out once a run, each pixel's place by the column's layout (it was the thread's busiest function, 11.6%) | - |
| The pixels' addresses (gs/gsaddress.h) | Inline, in a header of their own (the transfers worked one out per pixel through a call into gsmemory.cpp) | - |
| The compiler's target | x86-64-v2 (SSE4.2, POPCNT: what Windows 11 asks of a processor): floor and ceil inline; no FMA, so the same results | - |
| `HwGs::DecodeOnGpu` (hwgs.cpp) | A texture looked up again only when a register was written or the CLUT changed (`Gs::stateGeneration`); a complete one checked a page at a time against the pages' changes rather than a tile at a time | `$TWIN_HW_NO_TEXTURE_GENERATION`, `$TWIN_HW_NO_PAGE_CHECK` |
| `HwGs::Draw` (hwgs.cpp) | A mesh's next primitive drawn as the last one was while nothing it depended on changed (`DrawLikeLast`: the GS's state, the CLUT, the targets, the settings, the texture good as it was): no key, targets or texture worked out again, only its pages made current and its vertexes added. A quarter of the render thread's time | `$TWIN_HW_NO_FAST_DRAW` |
| `WaitForVBlank` (display.cpp) | `SDL_DelayPrecise` | - |
| VU programs' rounding (ee/rounding.h) | Toward zero for a run by MXCSR's rounding bits alone: MinGW's `fesetround` also sets the x87's, through `__mingw_setfp`, at each program's start and end (4% of VU1's thread); the inline kicks of one thread round to nearest the same way | - |
| `Dispatcher::Identify` (ee/vu1translate.cpp) | The game swaps its VU1 programs in micro memory several times a frame, and each is told again when it's back: by the code its hash was found for, compared, before any hash of a program not seen yet (the FNV-1a hash a byte at a time was a tenth of VU1's thread) | - |
| The VIF's unpacks (ee/vif.cpp) | The VU's memory wrapped by a mask, not a division an element | - |
| `Gs::WritePackedRun` (gs.cpp) | A GIF tag's PACKED quadwords written in one loop in the GS's own file rather than a call a quadword from the GIF | - |
| `Gs::DrawPrimitive` (gsdraw.cpp) | Its debugging switches and the software path's state copy out of line (their registers and stack were made at every primitive); the state cache `constinit` (no guard tested at each primitive) | - |
| `HwGs::RectPages`, `FewPagesOf`, `EnsureValid`, `MarkDrawn` (hwgs.cpp) | A page's place by the target's page shifts, not four divisions; a primitive with up to 8 pages (nearly all) tests them a page at a time, the 512 page sets' work only when a page isn't current or the target's own yet (the two calls' early tests did that work on every primitive, a twelfth of the thread) | - |
| `HwGs::ProcessWrites` (hwgs.cpp) | The CPU's writes' flag read before its exchange (a locked instruction at every primitive, 3% of the thread) | - |

ThinLTO over the emulation's files (`-DTWIN_EMULATION_LTO=ON`) was measured no faster, and is off.

Older processors (#23: an i7-2600 at 39 frames a second, its render thread 25 ms a frame; 2026-10-30, the last two rows): the
render thread is the limit, and it's the CPU's: at 1x in a 1280x720 window, uncapped, on the i7-8700K its time was the same at
2x, 4x and 8x (14 ms) while the graphics card's was 1 to 5 ms, and the driver is under 4% of a profile; the rest is the GS's
emulation a primitive at a time (WriteImage, DrawPrimitive, HwGs::Draw, RectPages, the GIF's packets, AddPrimitive, each 4-13%).
Before and after the two rows, one run each (perf.py, the performance lines' averages):

| Where | Frames a second | The render thread (one thread: all of it) |
| --- | --- | --- |
| The beach, three threads | 69.2 -> 87.8 | 14.4 -> 11.4 ms |
| The beach, one thread (`TWIN_RENDER_THREAD=0`) | 35.4 -> 39.1 | 28.3 -> 25.6 ms |
| The beach, three threads on two cores (affinity 0x5) | 51.0 -> 57.4 | 18.8 -> 15.6 ms |
| The Academy's hub, three threads | 83.4 -> 102.7 | 12.0 -> 9.7 ms |
| The Academy's hub, one thread | 47.5 -> 52.9 | 21.0 -> 18.9 ms |
| The Academy's hub, three threads on two cores | 63.3 -> 74.1 | 15.7 -> 12.6 ms |

VU1's thread (about 10 ms at the beach) is next once the render thread is under it.

The second step (2026-10-31, the rows from "VU programs' rounding" on), aiming at every thread under 8.5 ms at the beach at 1x
here (#23's i7-2600 took 1.7 to 2 times these times on 1.0.19: under 8.5 here would be about 60 there). Two runs of each
program in turn, 1.0.19, the first step's and this one's (perf.py); frames a second, then the game's thread, VU1's and the
render thread (one thread: all of it):

| Where | 1.0.19 | The first step | This one |
| --- | --- | --- | --- |
| The beach, three threads | 71.0; 6.9, 10.0, 14.1 ms | 83.7; 7.0, 10.0, 11.9 ms | 93.1; 7.0, 8.8, 10.7 ms |
| The beach, one thread | 34.8; 28.8 ms | 38.3; 26.1 ms | 45.0; 22.2 ms |
| The beach, three threads on two cores | 54.8; 11.9, 15.0, 18.0 ms | 56.5; 13.0, 14.9, 16.9 ms | 62.7; 12.2, 13.1, 15.1 ms |
| The Academy's hub, three threads | 81.8; 4.8, 6.2, 12.3 ms | 99.2; 4.9, 6.2, 10.1 ms | 113.1; 5.0, 5.6, 8.8 ms |
| The Academy's hub, one thread | - | 52.0; 19.2 ms | 60.2; 16.6 ms |
| The Academy's hub, three threads on two cores | 64.7; 7.0, 8.6, 15.4 ms | 73.8; 8.9, 9.9, 13.2 ms | 80.9; 8.6, 8.8, 11.9 ms |

The target isn't reached at the beach: VU1's 8.8 ms is near it, the render thread's 10.7 isn't. Its time is now spread thinly
(HwGs::Draw 14%, AddPrimitive 9%, the GIF's PACKED writes 8%, FewPagesOf 8%, the textures' tile check 7%, WriteImage 6%, the
driver 5%, SDL's current context asked at every primitive 3%), so the rest needs work of another kind: the page tracking done
a batch rather than a primitive at a time, the GS's side (the GIF, the registers, the kicks) on a thread of its own, or the
textures' check by page. VU1's: the translated programs are two thirds of it.

The threads: VIF1 and VU1 run on a thread of their own between the game's and the render thread (renderthread.h, three stages,
the default): the game's thread queues VIF1's words, the GIF's transfers and the GS's work in their order, the VU stage runs
VIF1 and VU1 and hands it all on to the render thread's queue, so the GS gets the same data in the same order. Where the scenes
are big the game's thread went from about 17.5 ms a frame to 9 (VU1 was half of it): the Totem's beach, the school's boiler room
and rooftops went from 57 to 60 frames a second. `$TWIN_RENDER_THREAD=gs` puts VIF1 and VU1 back on the game's thread.
`$TWIN_FRAME_STATS`' `game` is the game's thread's work between its waits for the vertical blank.

The fast renderer (1.4.0, THE LIST 74 and 75, GRAPHICS.md "The fast renderer"): Display > Renderer > Renderer, fast
(experimental), draws the scenery's strips at once instead of the GS's primitives one by one, with the same pictures. The
beach at 1x, OpenGL, 1.3.0 (dist) A against this build B in one session, A B B A A B B A (perf.py; 2026-11-20,
compare/runs_b75 perf5 to perf7); frames a second, then the render thread, VU1's and the game's thread and their sum, the
averages:

| Where | 1.3.0 | This build |
| --- | --- | --- |
| The beach, fast, two cores (0x5) | 60.6; 14.0, 10.7, 9.8 ms (sum 34.5) | 68.8; 9.0, 11.4, 8.4 ms (sum 28.9) |
| The beach, accurate, two cores | 61.7; 13.9, 10.8, 9.7 ms (sum 34.4) | 59.5; 14.8, 11.0, 10.0 ms (sum 35.8) |
| The beach, accurate, all cores (0xFFF) | 84.6; 11.9, 9.6, 6.0 ms | 88.7; 11.2, 8.9, 5.5 ms |

On two cores the three threads take turns, so their sum is the measure beside the frames a second; the fast mode takes 5.6 ms
off it. The accurate mode's row on two cores was the bench. Its four pairs in two series (perf5, perf8) were -2.5, -1.9, -0.1
and -2.6 frames a second, but perf.py's runs, with new settings each, hashed the disc image in the background for their
first 28 to 30 s, over the first measured line (a profile on two cores, compare/runs_b75/prof8: that thread busy in both
programs). perf.py now gives each run the image's disc_identity.txt, as a played copy has it, so no run hashes (every log
says "checked before"). Measured again so (2026-11-21, compare/runs_c/perf1, A B B A A B B A on two cores, A 1.3.0, B this
build accurate), the pairs fall on both sides of 0: +0.2 and -0.8 frames a second, the averages 66.8 and 66.5, the render
thread 12.5 and 12.7 ms, VU1's 9.9 and 10.0 ms. The bench also drifts up to 17% in minutes (THE LIST 74's speed question,
compare/runs_b74/speed_question.txt). VU1's thread is the fast mode's longest now (THE LIST 104).

Comparing two builds' pictures: `$TWIN_FIXED_TIME=1` with `$TWINSANITY_AUDIO=frames` (the sound made in step with the frames)
gives the same frames run after run (with the sound in real time, a run's speed changes what the game does with it after a
few hundred frames).

### The performance measure (perf.py)

THE LIST 67 (1.3.0's phase E): `native/tools/perf.py` measures the same place played the same way, so a change to the renderer
is measured before and after it on one PC. Each run plays the beach from the level sweep's start with no input
(`$TWIN_SWEEP_LEVEL`, `$TWIN_SWEEP_IDLE`: the same view every time) in a 1280x720 window at 1x, unlimited frames with v-sync
off, the process on two cores (affinity 0x5: two physical cores where each has two threads, a weak PC's count), with a
performance line every 10 s and `$TWIN_GL_CALLS`. The first 10 s (the loading's end) are left out; three runs of 60 s by default,
the summary their median and range; `--out FILE.json` keeps it, `--before FILE.json` prints each number beside the kept one's
and the change, `--show` shows a kept one. `--cores`, `--scale`, `--chunk`, `--threads one`, `--option`, `--env` change the
run; a player's portable folder (build/native/dist) is copied first, its User folder never used. The window opens on the
screen: the PC left alone while it runs (about 5 minutes).

`$TWIN_GL_CALLS` (hw/gl.cpp, sessionlog.cpp; with the performance lines) logs after each one the render thread's work a frame
since the last: its OpenGL calls by kind (`graphics counts: calls 14370, draws 321, binds 1987, uniforms 6198, state 5534,
data uploads 330, ...`: each entry point of hw/gl.cpp counted while the GS is emulated, a plain add on the render thread), the
GPU renderer's own counts (batches, textures decoded, GS memory's pages uploaded and downloaded, copies of a target, pictures
read back) and the card's time by kind (`card draws 0.86, card clears 0.00, card copies 0.00, card uploads 0.19, card window
0.01`, ms); then the twelve entry points called most (`graphics calls a frame, the most: Uniform1f 2524, Uniform1i 1653,
Disable 1084, ...`). The numbers above are the beach's at 1x before phase E (i7-8700K, RTX 4070 SUPER).

The base for phase E (2026-11-15, three runs of 60 s, the median and the range; the program as of 1.3.0's phase D):

| What | A frame |
| --- | --- |
| Frames a second | 60.9 (60.8-61.4) |
| The game's thread | 8.3 ms, and 5.3 waiting; the window's present 2.9 |
| VU1's thread | 10.0 ms |
| The render thread | 14.3 ms (14.0-14.3): the GS 12.5, OpenGL 1.6, the picture handed over 0.1 |
| The graphics card | 1.04 ms: the render thread's draws 0.84, uploads 0.19, the window's 0.01 |
| OpenGL's calls | 14,383: uniforms 6,204, state 5,540, binds 1,988, data uploads 330, draws 321 |
| The GPU renderer | 229 batches, 10 copies of a target, 17 pages' downloads, 15 uploads, 9 decodes |

So on two cores the render thread is the limit (the game's thread waits 5.3 ms for it), and of it the GS's emulation; the
card works 1 ms of the 16.4. OpenGL's calls are 63 a batch, most of them uniforms and state, all set at every batch.

The card's time by the GPU renderer's pass (`$TWIN_GL_CALLS`' `graphics card by pass:` line, perf.py's `pass` numbers; each
pass of hwgs.cpp labels its calls, gputime.h's `GpuTimeLabel`): at 1x of its 1.0 ms the draws were 0.58, GS memory written
from the targets 0.20 (the same at 4x: memory's side), textures decoded 0.13, memory written into a target 0.07, a target's
copies 0.04; 79 passes a frame. The conversions are the shadows' and the colour filter's reinterpretations of a buffer (their
depth and colour read as 8 or 16 bit textures): the accurate path's own (THE LIST 23, 24).

Fewer passes (THE LIST 68): the 9 textures decoded a frame were each 1024x1024 decoded whole (89,000 tiles of 8x8 a frame),
where the draws read strips of 10 to 34 texels across the screen (`$TWIN_HW_DECODE_TRACE`): a texture read from a render
target a piece at a time was decoded whole so the next pieces would find it decoded, and its width past the buffer's (its
texels lie on other pages there) and its rows below the screen were decoded with it. Now the rows asked for, across the
buffer's width and a tile more (21,000 tiles); the rest is decoded when a draw asks for it, so the texels drawn are the same
(the 25 pictures as 1.2.0's), and fewer of memory's pages are written from the targets first. Decoding only the stale tiles
inside the box would have saved a tenth of what was left. Three runs of 60 s each (the program before it with the passes'
labels, then with it):

| What | Before | After |
| --- | --- | --- |
| Frames a second | 59.1 | 63.4 |
| The render thread | 14.4 ms (the GS 12.8) | 13.2 ms (the GS 11.6: fewer tiles and pages checked) |
| The card | 1.00 ms: textures decoded 0.132, memory from the targets 0.197 | 0.96 ms: 0.083, 0.137 |
| OpenGL's calls | 14,358 | 14,281 (16 pages' downloads a frame, 17 before) |

Fewer calls (THE LIST 69): the hardware renderer set every uniform and most of the context's state at every batch, mostly to
what they already were. hw/gl.cpp now leaves out each call that would change nothing, above the counted entry points (so
`$TWIN_GL_CALLS` counts what reaches the driver): a uniform's value by program and location (a program's own state: only the
renderer uses its programs, and it makes one current before setting its uniforms), a texture's sampling by texture, and the
context's switches, viewport, scissor, masks, blending, stencil, active unit, bound textures, framebuffers, vertex array,
buffer and program. The context's state only where the context is the renderer's own (the render thread's, or its own hidden
window's): with one thread (`$TWIN_RENDER_THREAD=0`) the window's code draws in it too, so there only the uniforms are left
out. Deleting a bound texture or framebuffer leaves 0 bound, as OpenGL does; another context made current, or the renderer
made again, forgets the state. `$TWIN_HW_STATE_CACHE=0` makes every call. The pictures: the 25 as 1.2.0's, and windowed runs
at a fixed time with one and with three threads (compare/tools/cmp_options.py, `B:TWIN_HW_STATE_CACHE=0`) the same frame for
frame. Three runs of 60 s each (the program with 68, then with this):

| What | Before | After |
| --- | --- | --- |
| OpenGL's calls a frame | 14,281: uniforms 6,169, state 5,515, binds 1,968 | 2,936: uniforms 535, state 922, binds 851 |
| The render thread | 13.2 ms: the GS 11.6, OpenGL 1.5 | 12.2 ms: the GS 10.9, OpenGL 1.1 |
| Frames a second | 63.4 | 67.1 |

The calls left are 13 a batch: the draw and its vertexes, the texture bound, and the viewport set twice (the far bigger one
the draws use, then the target's again for the passes after: leaving that out would need each of them checked).

The command list (THE LIST 70, the base for Vulkan): hwgs.cpp no longer calls OpenGL. It describes its work through
hw/gpu.h's `Gpu` the way it called OpenGL (the bound targets and textures, the switches, the blending, the uniforms), in no
API's terms, and `Gpu` records it as a list of commands, each whole by itself: a draw carries its framebuffer, its program,
the texture on each unit and its state (blending, depth, stencil, masks, scissor, viewport: `RenderState`, what a pipeline
is made from), a clear and a copy theirs, an upload its bytes (copied: GS memory changes before the list is played). A
backend plays the list: hw/glreplay.cpp's with OpenGL, setting what each command's state changes from the last it set
(gl.cpp's cache under it). Textures', framebuffers' and programs' names are the backend's, made at once (OpenGL's own). A
uniform or a texture's sampling set again to the value it has isn't recorded. The list is played after each batch, when a
result is wanted (a read back, a new target's check, a fence) and at the frame's end (as the GS's work in the log's split, so
`$TWIN_GL_CALLS` counts it: played during the hand-over it was left out, 1,613 calls a frame instead of 2,279).
`$TWIN_HW_LIST_BYTES=N` plays it only once it holds N bytes: in bigger pieces the driver's calls cost more here (two cores:
OpenGL 1.0 ms a frame after each batch, 1.4 every 64 KB or 1 MB; 67.6, 66.3 and 65.3 fps), so after each batch is the
default. The design follows Tenkaichi3Decomp's port/src/gs/gs_draw.c and gs_gpu.c at its MIT commit 06e4671 (a frame
recorded, a back end replays it); the code is written anew. The pictures: the 25 as 1.2.0's, and windowed runs at a fixed
time with one and with three threads (compare/tools/cmp_options.py, `EXEA=` the program before it) the same frame for frame.
Three runs of 60 s each (the program before it, built from the same tree, then this, one after the other):

| What | Before | After |
| --- | --- | --- |
| OpenGL's calls a frame | 2,940: state 923, binds 851, uniforms 535 | 2,278: state 308, binds 803, uniforms 535 |
| The render thread | 12.0 ms: the GS 10.8, OpenGL 1.1 | 12.1 ms: the GS 10.9 (the list recorded and played), OpenGL 1.0 |
| Frames a second | 67.3 | 68.0 |

The Vulkan backend (THE LIST 71) is hw/sdlgpu.cpp. It plays the same lists with SDL's GPU API on its "vulkan" driver, SPIR-V
only, behind `$TWIN_GPU_API=vulkan` until 73 made it a setting ("The graphics API", below). The renderer's GLSL 3.30 programs become SPIR-V while the
game runs, made by glslang 16.6.0 as a static library (native/tools/build_windows.py builds it from its release, checked by
SHA-256, and without it the build has OpenGL only). The translation gathers each stage's loose uniforms into a std140 block,
binds the float samplers in order, reads the integer textures (R32UI, R16UI, which SDL doesn't sample) as storage textures
with texelFetch and matches the varyings by name. What OpenGL does and SDL GPU doesn't is made in the backend:
- a pipeline for each state a draw has, made when first drawn with and kept;
- clears and copies as draws, and depth read back through a float copy;
- the vertex colour's bytes as OpenGL's 0-255 floats, and new textures zeroed;
- dual-source blending (the GS's alpha as the blend factor while the alpha is written too) as two passes, the colour then
  the alpha;
- each vertex shader's y negated, since SDL's viewport is flipped, so a texture's first row is OpenGL's in both.

In 71 the picture shown was still read back into memory for the window's OpenGL (72 moved the window onto the backend: "The
window on the backend", below). The 25 pictures are the same as 1.2.0's OpenGL ones, byte for byte. Validation (the Vulkan SDK's layers, `$TWIN_VULKAN_DEBUG`)
reports only VUID-vkCmdDraw-None-09600, which comes from SDL's own memory defragmentation (sdlgpu.cpp's top says why).
Measured with perf.py (the same program, OpenGL then Vulkan, one after the other):

| What | OpenGL | Vulkan |
| --- | --- | --- |
| Frames a second | 64.1 | 63.6 |
| The render thread | 12.9 ms: the GS 11.6, OpenGL's calls 1.1, the picture handed over 0.2 | 15.5 ms: the GS 12.6 (Vulkan's calls in it), the picture handed over 2.9 (read back for the window until 72) |
| The game's thread | 9.3 ms (the window's present 2.3) | 5.4 ms (the window's present 0.4) |

The list played costs the same in both (the GS and the API's calls 12.7 and 12.6 ms); the picture read back for the window is
what Vulkan costs more until 72. The card's own time isn't measured with Vulkan (SDL's GPU API 3.4.18 has no timer
queries).

The window on the backend (THE LIST 72): everything that draws into the window records through gpu.h's Gpu as the renderer
does: the picture's presenter (display.cpp: the picture at the TV's shape, FXAA, the downsampling, the CRT filter), SMAA
(smaa.cpp), the overlay's menus and texts (ui/overlayrender.cpp) and the window shots ($TWIN_WINDOW_SHOTS). Framebuffer 0 is
the window. With OpenGL the window's backend is a GlBackend on the window's context calling OpenGL's entry points as they are
(gl.h's LoadDirectGl: the call cache of 69 is the render thread's), and the window's pictures came out the same as before byte
for byte (compare/tools/window_ab.py: the beach at a fixed time with anti-aliasing off, SMAA, FXAA, the CRT filter, 8x made the
window's size). With Vulkan the window is made with SDL_WINDOW_VULKAN and claimed by SDL GPU on the device the renderer
shares; framebuffer 0 is a texture of the window's size, copied into the swapchain's picture when shown (the swapchain's own
can't be read, and the window shots read it), and the render thread's copy of each picture is handed over as SDL GPU's
texture (gpu.h's NativeTexture and TakeTexture), so nothing is read back. A window Vulkan can't show in is made again for
OpenGL. The same shots with Vulkan: anti-aliasing off and SMAA the same as OpenGL's byte for byte, FXAA, the CRT filter and 8x
a few hundred pixels 1 apart (of 255).

SDL defragments its memory at the submit that presents the window, moving a fragmented allocation's textures into new images
at the end of that command buffer; it doesn't spare a texture another thread's command buffer is still recording with, whose
earlier draws then land in the old image (sdlgpu.cpp's g_DeviceLock comment; SDL_gpu_vulkan.c 3.4.18's VULKAN_Submit and
VULKAN_INTERNAL_DefragmentMemory). So while a window is claimed the renderer's backend records only under g_RecordingLock,
from a command buffer's start to its submit (each playing of the list ends with one), and the presenting submit takes it too.
The overlay's offscreen shots ($TWIN_UI_SETTINGS_SHOTS) draw on a device of their own, with a target kept for each size: on
the shared device the settings screen's first 2560x1440 shot lost its backdrop to such a move, and making and deleting the
target each shot lost another. Validation reports nothing then.

Measured with perf.py (the beach at 1x on two cores, three runs of 60 s; the same program, OpenGL then Vulkan, one after the
other; compare/perf_f/gl_72.json and vulkan_72.json against 71's):

| What | OpenGL (71 / 72) | Vulkan (71 / 72) |
| --- | --- | --- |
| Frames a second | 64.1 / 62.6 | 63.6 / 68.7 |
| The render thread | 12.9 / 13.2 ms (the GS 11.8, OpenGL 1.2, the picture handed over 0.2) | 15.5 / 14.4 ms (the GS 12.8, Vulkan 1.5, the picture handed over 0.1) |
| The window's present | 2.3 / 2.5 ms | 0.4 / 0.2 ms |

OpenGL's numbers moved within a run's spread (VU1, which 72 doesn't touch, 3.8% slower in the same runs). Measured again in
one session, the two programs one after the other (pre-72, 72, pre-72, 72; compare/perf_f/ab73): the program before 72
62.0, 63.8 and 62.1 fps, 72's 64.9 and 64.1, so OpenGL lost nothing. With Vulkan the
picture's read back is gone (2.9 ms to 0.1) and the window's present is SDL GPU's (0.2 ms against OpenGL's 2.5 here), so the
game runs faster than with OpenGL on this PC, though its render thread is 1.2 ms longer (the GS's work 1.0, the API's calls
0.3). The card's own time isn't known with Vulkan.

The graphics API (THE LIST 73): a setting, Display > Renderer > Graphics API (`graphics_api`: OpenGL, the default in 1.3.0, or
Vulkan), used from the next start, since the window is made for one API (graphics/graphicsapi.cpp). A start can fail with one
API in two ways. The window can't be drawn in with it (no OpenGL 3.3, no Vulkan driver): system.cpp makes the window again for
the other API at once ("graphics: OpenGL can't show the game here (...): Vulkan instead"). Or the start goes on and never shows
its pictures, a driver that hangs or crashes first, as #12 and #16's black screens and #4's hang may be. For that one
`graphics_start.txt` in the settings folder names the API before the window is made, and goes once 30 pictures were shown or the
session ended in order (the window closed, a fatal error's message, the exit). Found at the next start for the same API, the
other API is taken ("graphics: the last start with OpenGL never showed the game's pictures (graphics_start.txt left behind), so
Vulkan this time"). Either way the setting changes to the API taken, so the next start begins with it and the settings show
the one in use. A start with `$TWIN_GPU_API` set leaves both alone. A build without Vulkan (the AppImage before 1.3.0) hides the
row and keeps OpenGL. A black screen whose pictures are shown, black, isn't caught: only a start that never shows any.

### The render thread by kind of drawing (THE LIST 74)

The fast renderer's map (`$TWIN_RENDER_SPLIT`, graphics/rendersplit.cpp; compare/runs_b74/split.sh). Each record the render
thread plays is timed against what sent it: a kick against the VU1 program whose XGKICK it was (the dispatcher knows which
translated program micro memory holds there: `Ee::Vu1ProgramAt`, its address in .vutext from vu1_translate.py's
`g_ProgramAddresses`), a transfer against its GIF path; and against the frame's bucket (LinkRenderBuckets tells where each starts
in the chain). Every run of VU1 starts at the renderer's program 1 (0x2DB0E0), which goes on through the entries VIF1 unpacked
to the material's shader type's program, where the kicks are; a skin's part is a run of program 1 after a blend program's
run (renderer/skins.cpp), so its kicks are told apart. Measured 2026-11-19 (perf.py, one run of 40 s, two cores, 1x, OpenGL);
the render thread's milliseconds a frame, of them each kind's, and its primitives:

| Where | Render thread | Type 01 (0x2DF700) | Path 2 (textures) | Then | Skins (characters) |
| --- | --- | --- | --- | --- | --- |
| The beach | 14.4 ms | 5.84 ms, 33,400 | 2.28 ms | type 04 1.62, 02 1.16, 17 1.00, 1A 0.78 | 0.21 ms, 1,000 |
| The Academy's hub | 12.2 ms | 4.99 ms, 26,500 | 2.59 ms | type 02 2.49, 0A 0.41, 04 0.39 | 0.17 ms, 1,000 |
| Amberly's fight | 6.1 ms | 2.14 ms, 12,700 | 1.65 ms | type 04 0.46, 02 0.43, 12 0.34 | 0.15 ms, 750 |

By bucket the opaque materials' take 45-70% (the beach 8.6 of 13.2 ms), the startup chunk's objects 9-17%. So the scenery and
the placed objects are what the GS's emulation spends its time on, the type 01 program's above all (the source shader family
TwinsanityEvolved's prepared meshes draw, with 0A); the characters are 1 to 3% of it. THE LIST 75 (the scenery) comes first, as
planned, and the textures' transfers (path 2) are the next largest part after the kinds of drawing.

### Hitches

THE LIST 19 (#23: frames of 0.25-1.8 s as new areas load, GNRTX-3's i7-2600 and GT 730 on NVIDIA 472.12, his first 1.1.0
session; #32: drops to 24 fps at 60). Measured once with the log's slow frames (below, "The log") and `$TWIN_PROFILE`, here
(i7-8700K, RTX 4070 SUPER) through the hub's links (`$TWIN_HOP=huba,hubb,huba,beach` from the beach) with the driver's shader
cache emptied (`__GL_SHADER_DISK_CACHE_PATH` an empty folder: as a first start after an update finds it):

- The chunks' loading is spread over frames already: the game's background work fills what's left of a frame up to 1.7 ms
  before its end (context.cpp's budgets, the PS2's), at most 12 ms a frame here at a chunk change, no step of it over 0.2 ms;
  the disc's reads 1-2 ms a frame (the image in the system's cache). The profile: the game's thread 70% waiting for the
  vertical blank, the loading not among its busy functions.
- What made the frames wait were the hardware renderer's shader programs, one for each new combination of the GS's states,
  compiled and linked the first time it's drawn (hwgs.cpp's ProgramFor) while the game waits for the render thread: the first
  frame of play 156-247 ms (11 programs in 119 ms), then 42 ms (2), the first frame in huba 29 ms (1); 25 programs in all,
  11-23 ms each. The driver's own disk cache makes the next sessions' quick, but every first session of a version (its
  shaders' source changes) and every driver update pays them, and an old card and driver far more than these.
- So each start now makes them before anything's drawn (display.cpp's WarmPrograms): those the player's sessions made
  (`shaders.txt` in the settings folder: each one made while playing is added; another version's list starts again) and this
  version's own list (KnownPrograms: those check_windows' runs and the hub's walk drew with, the default options); all asked
  for first (the driver compiles them on its own threads where it has KHR_ or ARB_parallel_shader_compile), then waited for,
  at the start. Measured: the 25 made at the start in 172 ms (274 one after another; left to their first draw instead, that
  frame still waited 76 ms for the driver), the slowest frame of the walk 30 ms instead of 247, no program made while playing.
  A first session (no shaders.txt, the driver's cache empty): this version's 41 made at the start in 277 ms, none while
  playing, the slowest frame the first of play, 43 ms (OpenGL 33.5 ms, the driver's first drawing; no program made).
- The frames' times were taken as the game's thread began waiting for the vertical blank, so a frame after one that had
  waited long counted that wait: 26-29 ms "frames" while a chunk loaded, at 60 frames a second all the same. They're taken as
  each frame's let go now (display.cpp's WaitForVBlank).
- Not changed: the disc's reads stay synchronous (a read ahead would help only a slow drive: nothing measured here says so;
  a player's slow frames say "reading the disc" when it does), and the loading's budget stays the game's.

## Languages

The PAL game has five: English, French, German, Italian and Spanish, each with its texts (the archive's Language folders), its
voices (CRASH6\<LANGUAGE>.MB and .MH, loaded once at start-up) and its sound in the story's movies (a channel each). The PS2
took the language from its own settings once, at start-up; the native build takes Windows' (system.cpp's
`Platform::System::Language`), or the one Options > Game > Text language names (options.txt's `language`, 1-5), from the game's
next start.

## Mods

The modders' and players' side is [MODDING.md](MODDING.md); this is how the port does it.

The settings folder's `mods` folder (`%APPDATA%\Crash Twinsanity\mods`) holds the mods (mods.h, mods.cpp), each a folder: a
Crate Mod Loader Mod Crate (its `ModCrateInfo.txt`; `layer0\` the disc's files, `layer1\` the archive's, CRASH6\CRASH.BD's, by
their paths; a crate one folder down, as a zip unpacks, is found too; one of another game, `Game=` not `CrashTS`, refused;
`ModCrateSettings.txt`, Crate Mod Loader's settings of the game's code, doesn't apply), or a folder of files laid out as the
disc's (`CRASH6\`, `FMV\`) and the archive's (`Levels\`, `Language\`, `Startup\`, `Extras\`). The mods folder's own files and
folders of those names (`mods\Levels\Earth\Hub\beach.rm2`, as before 1.2.0) are one more mod, "the loose files". Names in any
case. `mods.txt` in the settings folder keeps their order (the first wins where two have a file) and switches; a mod not in it
comes first and on; it's written only when the player changes something. The mods are looked at once, when the disc image is
open (`Mods::Scan`, system.cpp's OpenDiscImage), and the log says each (`[native] mods: NAME (on): a Mod Crate by ..., N disc
files, N archive files (N new)`) and each file used. A mod's SLES_525.68, SYSTEM.CNF and CRASH6\SYS\ (the I/O processor's modules, run as the PS2 runs them) are never used (the program is the
port's, the game's data read from the player's disc): the log says what its executable changes (code and data, in words and
places, compared section by section). While any mod is on, the memory card is a folder of their own (`memorycard-mods-` and
their names; the level select's `...-level-select` beside it), so the player's saves never meet a mod's levels.

Importing (modimport.cpp): a zip or an `.iso` in the mods folder becomes a mod folder of its name, from Options > Mods >
Import mods (a thread of its own, the dialog's bar) or `--import-mods` (the console says each step; its exit status how many
failed). A zip is unpacked (TwinsanityEvolved's zip reader and inflate, NativeEE's MIT: modzip.h, modinflate.h; its one top
folder left out when every file is in it; a name leading out of the folder skipped). A disc image (ISO 9660, 2048-byte
sectors) is compared with the player's disc (`Disc::FindOriginal`: the image's own files, whatever the mods): each disc
file kept whole in `layer0\` when it differs or the disc hasn't it, CRASH6\CRASH.BD entry by entry through both tables
(by name: an archive whose entries moved still compares) into `layer1\`, with a ModCrateInfo.txt of its name, so the result
is a Mod Crate. Each import is written in `.importing-NAME` and takes the mod's place only once whole; `imported.txt` (the
source's name, size and date) marks it, so an unchanged source isn't offered again; a folder of that name not made by an
import stops it. An xdelta, PPF or BPS patch isn't applied here (a patcher makes the image first). Options > Mods (the
settings' seventh tab, settings.cpp's BuildMods): Import mods while there's something to; each mod a choice, off or on in its
place ("on · 1", the first; moving one shifts the others; its row's label and description its own, in the overlay's
Windows-1252 with the prompts' glyphs made plain characters, cut to one line); Open the mods folder. Changes are saved to
mods.txt and used from the next start (the mods are looked at once: the session keeps the list it started with, the
page's own list is apart). An import's end looks at the mods folder again (Mods::Refresh: the new mods first and on, as the
next start would put them, mods.txt written) and makes the page's rows again, the selection on the first mod; a hidden
row's selection steps to the line next to it (MoveRow). check_windows' "mods import" line: a Mod Crate's zip
and a small changed image (one archive file changed, one the disc's own, a disc file the disc hasn't) imported, then played
with both on (the first by name wins, the card of both) and with mods.txt turning one off.

`CrashTwinsanity.exe --list-files` lists the archive's files and `--extract=PATH` copies one (or one of the disc's) into the
mods folder to be changed (commands.cpp); `native/tools/iso_extract.py IMAGE [--archive] --list` and `iso_extract.py IMAGE
[--archive] PATH OUT` do the same without the game.

The disc.cpp side: a disc file's mod is given a place past 16 GB (a GB each, its sectors still 32 bit for the I/O processor).
The archive's files' mods follow the archive's own data in a copy of it at 12 GB: while any mod has one, `CRASH6\CRASH.BD` is
found there (its size the data's and the mods'), and `CRASH6\CRASH.BH`, the archive's table, is made again with their starts
and sizes and the files they add (new levels: the game's own table is its list of files), as Crate Mod Loader rebuilds it
for the PS2. Before 1.2.0 an archive file's mod was placed past the image's end and only FileStreamOpen knew it: the I/O
processor's STREAM.IRX, which reads the sound banks, refuses an offset past the file's size it was given and reads on from
where it was, so each sound bank of a mod's level came as the bank before it, cut to its first 3 KB (#36's garbled sounds;
the "mods" line of check_windows puts the disc's own Default.rm2 and beach.rm2 in a mods folder and checks two of
Default.rm2's banks' lengths). STREAM.IRX counts in signed 32 bits (a size of 0xffff0000 crashed it): the archive with its
mods should stay under 2 GB, and the log says when it doesn't.

## Portable mode

Issue #11's request: the settings folder in the game's folder, to carry the game on a USB stick. `portable.txt` beside
`CrashTwinsanity.exe` makes it so (src/platform/native/portable.cpp), as PCSX2's, DuckStation's and Dolphin's portable modes:
a file and not an option of options.txt, since the folder has to be known before anything reads the settings. Then
`Native::SettingsFolder()`, which everything the game keeps goes through (options.txt, resolution.txt, bindings.txt,
disc_image.txt, xbox_disc.txt, `memorycard/`, `mods/`, `textures/`, the log), is the `User` folder beside the program, laid out as
`%APPDATA%\Crash Twinsanity\` is. Nothing else reads `%APPDATA%` (no `SDL_GetPrefPath`; the self-test's resolution check sets
`APPDATA` to a folder of its own). Without the file nothing changes: the default stays `%APPDATA%`.

- **At start-up** (`Native::StartPortable`, the first thing `main` does, before the log): portable.txt there and the folder
  usable (a file can be made in it, `FILE_FLAG_DELETE_ON_CLOSE`; its path is one the settings' strings can hold: they're in
  Windows' code page, as `getenv("APPDATA")`'s are) is portable mode. A folder that isn't (C:\Program Files, without an
  administrator's rights) keeps `%APPDATA%`, a message box says why. A first portable start with no `User` folder (portable.txt
  made by hand) copies `%APPDATA%\Crash Twinsanity`'s data there, with the shell's progress dialog (`IProgressDialog`, ole32
  looked up, not imported; it closes only while the thread that opened it takes its window messages, so they're pumped). The
  dialog stays, "starting the game", till the game's window is there, so the window opens while the game is the app in front;
  a message box or the disc image's file dialog closes it first. The log's lines after the version: what the start found, then
  "settings and saves in %APPDATA%\Crash Twinsanity" or "in the User folder beside the program" (named, not spelt out: the
  logs go to bug reports, and `%APPDATA%`'s path has the user's name).
- **Options > Game > Portable mode** (yes/no, ui/settings.cpp; shown only on Windows) is the mode in use. Stepping it asks
  (a dialog over the settings, the controller's or the mouse's: left and right or the mouse over a button choose yes or no,
  no at first so a press by chance doesn't switch, select takes the chosen one, back is no), refused at once when the folder
  can't be written to, then
  `Native::StartPortableSwitch` copies on a thread of its own from the folder in use to the other, the dialog showing a bar and
  the megabytes (texture packs and mods can be hundreds). The copy never moves anything: it's made whole beside the destination
  (`User.copying`, `Crash Twinsanity.copying`) and only then put in its place; what the destination held is set aside whole
  beside it (renamed: `User (backup 2026-10-24)`, `Crash Twinsanity (backup 2026-10-24)`, " 2" after the date for a second
  one the same day), an empty one removed. A failure (a full disk, a file of the old folder open) undoes it: nothing changed,
  the reason under the dialog's and in the log. Then portable.txt is made (it says what it's for) or removed, and from then on
  what the game writes goes to the new folder; the dialog says done, where the old folder went, and the game closes (on select,
  or by itself after 10 s), so nothing more is written to the old one. The next start uses the new folder.
- **The disc image** (system.cpp): in portable mode, with none remembered or the one remembered gone or wrong, the program's
  folder is looked in first (`.iso`, `.bin`, `.img` by name, each checked as the PAL disc) and the first right one is used
  without asking. An image inside the program's folder is remembered relative to it (`disc_image.txt`: `Crash Twinsanity.ISO`),
  so the folder can move and a USB stick change its letter; one outside is remembered as before, its whole path. A relative
  line is read as the program's folder's in either mode (one copied back by the switch still works). `--iso`,
  `$TWINSANITY_ISO` and local.json's are given images, as before.
- **Packaging**: `package_windows.py` remakes only what it makes in `build/native/dist/Crash Twinsanity/` (a folder played from
  can be portable): portable.txt, `User`, its backups and disc images are left there and never zipped, and `release_windows.py
  check` fails a zip with any.

## Frame rates past 60

Options > Display > refresh rate has 120 Hz, 144 Hz and unlimited past the game's 50 and 60 (`refresh_60hz` 2, 3 and 4 in
options.txt; NTSC's timing). The game's clock is its frames' real time (clock.cpp: the PS2's 50 and 60 Hz both play at one
speed), so it plays at the same speed at any rate; the steps a frame's time is capped at (a frame far slower than the PS2's
isn't taken whole) stay two 60ths of a second. What counts in 60ths of a second a frame (the particles' emitters, their events'
wheel, the particles each makes a frame; the decals' lives) is done once for each 60th gone by past 60 Hz (`NativeUi::GameTicks`),
as at 60 Hz once a frame, so effects last as long and are as dense; the particles' places are by the time every frame. The
movies keep the sound processor's clock. Unlimited, with v-sync off, ran at about 140 frames a second on the beach, 123 at the
first boss's and 91 on the Totem's beach (the heaviest) on the i7-8700K it was measured on.

The physics play as at 60 Hz too (issues #7 and #8). Past 60 Hz a computer's frames are uneven, a slow one now and then among the
fast ones, and the game measured velocities from the last frame's move over this frame's time: a movement node (movementnode.cpp)
is updated before its instance moves, so the move between its two places was made in the last frame, and retail divided it by this
frame's advance. At a steady rate that's the same; a short frame after a long one made the velocity several times too fast (a 40th
of a second's move over a 240th: six times). A jump takes off with the character's measured velocity
(`CharacterAgent::MeasureVelocity`), so a jump after a slow frame went several times as far (at 144 Hz with a 60 Hz frame now and
then the slide jump went 20.8 instead of 9); and the tied characters' swing takes the vertical velocity of what it stands on, which
the leader takes when the swing is above it (`SolveLinked`), so Crash stepping off Cavern Catastrophe's rising lift with Cortex on
it was thrown up as high as the frames happened to make it. Natively the node divides by the advance of the frame the move was
made in, its last update's (`MovementNodeBits::lastAdvance`, in bits retail leaves unused): the same as retail at a steady rate,
so the 60 Hz pictures are the PS2's still. And past 60 Hz the character's fall makes up for its shorter steps: the gravity is added
to the velocity before the step, so each of 60 Hz's steps comes down g·h²/2 further than the curve and its jumps are 3 to 4%
lower than shorter steps make them (2.14 high, 2.22 at 240 Hz); each step in the air past 60 Hz comes down what a 60th's would have
more (`CharacterAgent::Move`, `FramesPast60`). 50 Hz stays the European PS2's. check_windows.py's physics check measures it:
`$TWIN_PHYSICS_TEST` (below) at a 144 Hz and a 240 Hz screen's uneven frames against 60 Hz's.

The rigid bodies the other characters move with keep 60 Hz's timing past 60 Hz too (1.4.0, THE LIST 29a: issue #44). They counted
frames (rigidbodyframe.cpp): a launch that stops at its first touch is armed 15 steps after it (Bandicoot Pursuit's Evil Crash
jumps the gaps so: a tenth of a second at 144 Hz instead of a quarter), a ride is let go 5 steps after the last touch, a drag and a
grip keep a share of the velocity once a frame, and a body steering itself is pressed 0.07 into what it stands on once a frame (off
the ground too); past 60 Hz each counts 60ths (`SixtiethsGoneBy`) or keeps its 60 Hz share or move over the frame's time
(`KeptOverSeconds`, `MovedOverSeconds`), and a launch's steps fall as at 60 Hz as the character's do. At 50 and 60 Hz nothing
changes (the pictures and the physics byte for byte); `$TWIN_FRAME_STEPS=1` gives retail's counts past 60 Hz too. check_windows.py's physics check (the jumps and the lift at 144 and 240 Hz
against 60 Hz's) passes as before.

What stays past 60 Hz (#17, #44): the characters' behaviours (the scripts' graphs, behaviours.cpp) take a state a frame, and on
the PS2 a frame is a 60th, so past 60 Hz a chain of states that takes a quarter of a second at 60 Hz takes a sixteenth at 240 Hz.
High Seas Hi-Jinks' walrus runs a chain of 11 to 15 states between its chase packets, so it runs 2.5 units closer behind Crash at
240 Hz and catches him (#17, reproduced with Crash run by a pad script timed by the game clock, `$TWIN_PAD_SCRIPT_TIME`); Bandicoot
Pursuit's Evil Crash leaves 0.1 to 0.35 s early behind Cortex. A rule that steps every behaviour once a 60th past 60 Hz, in 60 Hz's
order, kept both chases as at 60 Hz, but it moved a few cutscenes' characters at their ends (17_dorm_room's Cortex a unit farther
from 60 Hz's place at 144 Hz, three skips that came out the same with retail's steps came out otherwise): kept for 29b in
the bench (its patch, its measures and what's open). Fixed steps with the pictures between them drawn (THE LIST
29b) would remove the question, and the frames' finer steps that meet a landing or a ledge sooner than 60 Hz's (Evil Crash lands
0.2 to 0.3 units short of 60 Hz's spot, on the far bank all the same) and the stick turned by a camera that moves otherwise at
another rate.

Until 29b, the two chases run at 60 Hz past 60 Hz (1.4.0, THE LIST 29c: #44's Evil Crash still went into the floor and the water
at 144 Hz in the user's own play after 29a, never in the bench's fixed time). While the player is in Bandicoot Pursuit's three
chunks (`altdoc`, `altdoc_b`, `altdoc_c`: Evil Crash after Crash and Cortex in each) or the walrus chase's (`gpa10`) and the rate
chosen is past 60 (120 Hz, 144 Hz or unlimited), the frames are paced at 60 Hz and the game clock's frames a second is 60, as with
60 Hz chosen; the rate chosen again once the player is in another chunk (features.cpp's `ChaseRateFrame`, every frame after the
trail has seen the player's chunk; `FrameRate` gives 60 meanwhile, the game clock's `HoldClockAt60` makes a test run's
`$TWIN_FIXED_TIME` frames 60ths). The setting itself stays as chosen; the log says it each way (`refresh: 60 Hz in
levels\altearth\hub\altdoc, a chase played at 60 Hz past 60 Hz (144 Hz chosen)`, `refresh: 144 Hz again, the player in ...`).
check_windows.py's physics check measures both chases at 144 and 240 Hz against 60 Hz's (`CHASES`): with the hold the walrus's
lead and Evil Crash's times at the first gap are 60 Hz's to the last digit; without it (`$TWIN_CHASES_PAST_60=1`) the walrus ran
up to 3.0 units closer and Evil Crash up to 0.3 s early. Its scenario plays Bandicoot Pursuit at 144 Hz, then Twinsanity Island.

Options > Display > Show FPS (`show_fps` in options.txt, off; issue #4 asked for it) draws the frames a second in the window's top
left corner, as a PC game's counter does (ui/fpscounter.cpp): the pictures the window shows, counted by the overlay's hook as each
is drawn (the game's frames, the movies' and the loading screens' alike), worked out every half second. It's the window's own
corner whatever the picture's shape (in a pillarbox's or letterbox's bar when there's one), small, above the HUD (the health bar,
the wumpa fruit and the lives start 5% of the picture in) and away from the menus' hints (the bottom corners). Being the overlay's,
it's never in the game's picture: the frame dumps and the pictures check don't see it. Its check is `--fps-shots` (below).

## A runaway VU program

VU1's model program (the one at address 0: a list of commands, each a handler) never ended in some frames: after Crash dies on
the beach, in cutscenes, and all the time at the first boss's (hundreds in a couple of minutes). The cause was the data's
conversion, on every host: a retail label two files declare differently got the last declaration's type, and the converter
wrote only that much of it. `D_002EC39C` is `s16 g_ShaderType0BEntries[2]` in shaders.cpp (the program's entry and its
entry for clipped triangles, 0x74) and `s16 g_ShaderType0BSize` in vuprograms.cpp: two bytes were written, the clipped entry
read the next object's padding, 0. A triangle crossing the guard band went to the clipper (0x92 on), which ends by calling the
command's clipped entry: the model handler (0x2E4) itself, which drew the model again, met the triangle again, for ever. Shader
types 0x10 and 0x11 (`D_002EC398`, `D_002EC3C0`) were the same. A worse one: `DecalUnusedInt` is a `u32` to decals.h and the
stream's 2048 byte `g_ScratchBuffer` to stream.cpp, and only 4 bytes were made: the stream's reads overran into other data.
native/tools/data_types.py keeps a label's widest declaration now (a sized array over a scalar, the larger count first, an
array without its size last), so the converter takes in the labels after it (as it does for any type longer than its label).

The guard stays: a program past 2 million instructions (the game's never run more than about 20000: `$TWIN_VU_LONGEST` says
the longest; `$TWIN_VU_RUNAWAY` sets it, 0 never) is stopped and said (`[native] vu: a program started at ... stopped`), the frame
going on without the rest of its work. To look at one: `$TWIN_VU_DUMP=FOLDER` writes the VU's memories and registers
(vu_code.bin, vu_data.bin, vu_registers.txt: native/tools/vudecode.py disassembles them), `$TWIN_VIF_HISTORY=FOLDER` VIF1's last
256 codes with their registers and data (vif_history_N.txt), `$TWIN_VU_TRACE_RUNAWAY` (with `$TWIN_VU_TRACE_STEPS`) the
instructions it goes round, each with the integer registers.

## The game's data from the disc

The game's C++ uses the retail executable's data: its tables, texts, numbers and the VU microcode (`.data`, `.rodata`, `.sdata`,
`.vutext`, `.vudata`). `native/tools/convert_data.py` lays each object out for the native build from the split. The program
doesn't carry those bytes, on every platform. `build/native/data.cpp` has every object zeroed, with where each run of its bytes
comes from in `SLES_525.68`: 381 runs, 66 KB of the executable (53 KB of it the VU microcode), 1 address-sized integer widened to
64 bits, and the 88 vtable entries' narrow-return marks. At start-up, before the fixups and before the game runs,
`CheckDiscImage` (system.cpp) reads `SLES_525.68` from the player's disc image and `NativeLoadRetailData` (data.cpp) lays
it out (the North American disc's `SLUS_209.09`: below, THE LIST 31b).

The checks:

- **At build time:** the converter lays the executable's bytes out the same way and compares them with the objects it made from
  the split. The build stops on any difference.
- **At start-up:** the executable's size and FNV-1a hash must be the PAL release's. Then the objects' hash must be the one the
  build made. A disc image given (`--iso`, `$TWINSANITY_ISO`, local.json) that isn't there, or is without `SLES_525.68`,
  another version or a changed copy, is refused with a message box (the log only, `--headless`). One remembered
  (`disc_image.txt`) or just picked is asked for again at once: a message says what's wrong ("isn't where it was" when it was
  moved), then the file dialog, the new one remembered.

Tested: the same pictures as the build with the bytes compiled in, frame for frame (35 pictures, 1 every 100 frames, from the
start with `$TWIN_FIXED_TIME=1` and `$TWINSANITY_AUDIO=frames`); the beach, the first boss's area, the caverns' tunnel and High
Seas played 30 s each by the level sweep; another game's disc image and a changed `SLES_525.68` (as a mod) refused.

VU1's and VU0's programs are still translated to C++ at build time (`vu1_translate.py`), from the split's copy of the microcode:
generated code, like the decompilation's own C++. The translation doesn't keep the microcode: the dispatcher tells which
program micro memory holds by the FNV-1a hash of its instructions (vu1jit.h's `CodeHash`), and where it can be entered by its
instruction's key (`EntryKey`), instead of comparing the words. The interpreter that could run the programs from the microcode
read at start-up instead is too slow: the beach at an unlimited frame rate ran at 15 fps interpreted (`$TWIN_VU1=interpret`),
70 translated.

Checked on the program: no 32-byte block of the executable's data is in `CrashTwinsanity.exe` (of the 1407 that aren't
repetitive); of its 16-byte blocks, two are, which are text of the program's own ("crash twinsanity", "Levels\Earth\Hub").
`release_windows.py check` checks it again for every release ("Releasing").

### The North American disc's map (THE LIST 31a)

The North American v2.00 disc's `SLUS_209.09` is the same program linked again from slightly different sources: the same
objects at other addresses, a few moved between sections, some with other contents. `native/tools/region_map.py` finds where
it has each piece of the PAL executable's data. It is TwinsanityEvolved's `tools/discs/elf_region_map.py` (MIT, its notice
kept; native/third_party/twinsanityevolved), with this port's changes marked "Ours". It aligns the two programs' code
instruction by instruction (the addresses each instruction forms are votes for where a variable went) and their data by
content, then writes `native/discs/region_map_SLUS-20909.json` for this program:

    .venv\Scripts\python.exe native\tools\region_map.py native --pal-iso PAL.iso --other-iso USA.iso
        --out native\discs\region_map_SLUS-20909.json [--full-out FULL.json] [--full FULL.json]

The file holds addresses, sizes and hashes only: the 42 segments that lay the data image out at the PAL addresses, the North
American executable's size, FNV-1a and SHA-256, the image laid out that way's FNV-1a, and every difference from the PAL image
with whether this program reads it (build/native/data.cpp's runs say which bytes it reads; the pointers are the native
build's own, so the map's pointer words, code pairs and fills aren't needed). `region_map.py selftest` checks the method on
synthetic programs, with no game data.

Measured (2026-11-21): the full map our run makes is TwinsanityEvolved's committed map exactly (42 segments, 197,990 of the
image's 198,384 bytes aligned, 12,355 pointer words, 6,660 code pairs, 1,503 of 1,506 data symbols, 58 differences over 146
bytes). Of the 66,069 bytes this program reads (381 runs and 1 widened integer), 65,941 are the same on the North American
disc, 56 are other bytes and 72 have no counterpart (7 of them not zero on the PAL disc). Those are 24 of the 58 differences:
the memory card folder's name (its serial), the languages' names (the North American program has one), the screen's height
(512 and 448 lines) and a PAL flag beside it, a table beside the languages' with the same height and another number in each
entry, and one count in D_002E80E0. THE LIST 31b decides each one.

### The North American disc's program (THE LIST 31b)

The program plays the North American v2.00 disc as TwinsanityEvolved does: the PAL program's code (its own code changes are
played the European way) on the North American executable's data. `CheckDiscImage` (system.cpp) looks for `SLES_525.68`, else
for another release's program this build has a map for (`NativeOtherRelease`: `SLUS_209.09`). `convert_data.py` writes the
maps of `native/discs/*.json` into data.cpp as a table of releases (the program, serial and version, the executable's size and
FNV-1a, the image's file range and FNV-1a, the segments as file offsets), and `NativeLoadRetailDataAs` reads one:

1. The executable's size and FNV-1a must be the map's (another version, v1.00, or a changed copy is refused with a message).
2. A buffer the PAL executable's size gets the North American bytes at the PAL file offsets through the segments (zero where
   no segment reaches). The image laid out that way must have the map's FNV-1a.
3. The same runs, widened integers and vtable marks as the PAL executable's are read from that buffer (`LayOut`).
4. The values kept PAL's are written over them.

The objects' hash (`DataHash`) stays the PAL disc's check only: the North American differences change it, and the build doesn't
read that executable. The image's hash checks every byte the program reads instead (all of them are inside the image: the
converter checks it).

The 24 differences in the bytes the program reads, decided one by one in `convert_data.py`'s `DECISIONS` by the object they
land in (a new map with a read difference in an object not decided there stops the build):

- `g_Movies` (D_002E70F8): the North American table, which says what its disc's movies are (TTIdent 512x448, the story's
  movies with one sound channel, English, instead of five, Complete 640x480). Its table has 21 movies (FMV\Spyro before
  Vivendi); the map gives the PAL program's 20th, Vivendi, the North American Vivendi.
- `g_ScreenMessages` (D_002E80E0): the start's no-card screen shows message 11 instead of PAL's 12. On its disc text 0x31 (11's)
  says what 0xB7 (12's) says, so nothing shown changes.
- `g_ProductCode` (D_002F4840): BASLUS-20909, the North American saves' name. `savedevice.cpp` takes their folder's region and
  product from it too ("BASLUS-20909", the North American program's), and they go on a card of their own
  (`memorycard-SLUS-20909` in the settings folder, `Native::DiscMemoryCard`; its level select's and mods' cards are named
  from it), so its saves and the PAL disc's never meet.
- The languages' names (D_003098A8 to D_003098C8): read, but not used. The North American program has one language, named as
  its archive's banks and voice bank are (`Language\Code\American.txt`, `CRASH6\AMERICAN.MB`), so `GameContext::StartGame`
  gives it that one language whatever the console's (the PAL program's five names' place holds other strings there).
- `g_ScreenHeight` and `g_Pal` (D_00309AE8, D_00309AEC): PAL's 512 lines and 1 kept. The port's refresh rate option changes
  only the pacing, never these two, so the PAL program has only ever run at 512 lines here. With PAL's values the North
  American disc starts as the PAL program does at 60 Hz (the option's default for every disc). NTSC's 448 lines only if its
  data needs them, proved by pictures (THE LIST 31c).

Some differences the map marks as not read are constants of the program's own C++, which its disc names differently:

- The font: `StartUp\Fonts\Crash_Euro` on the PAL disc, `StartUp\Fonts\Crash` on the North American one (its archive has only
  that one). `GameController::LoadGlobalResources` reads the North American name on its disc.
- The saves' region and product code (`BE` and `SLES-52568` in savedevice.cpp): taken from `g_ProductCode` instead, above.
- The rest are the PS2 build's disc check file (stream.cpp, not compiled natively), a library's version string and path
  separators moved with their neighbours.

Tested (2026-11-21, `compare/runs_c/us_run.py`, the level sweep without a window): the beach loaded in 3.8 s and played 30 s
at 59.9 fps, its first scene starting at 23.8 s, no fatal line.

### The North American disc's files and checks (THE LIST 31c)

What the North American disc's files mean for the PAL program, looked at one by one:

- The files the C++ names: `native/tools/disc_paths.py` looks every string of `src/` that is a path in the disc's folders up
  in both discs' files and archives, and the files each disc's languages are read from (`Language\Code\<name>.txt`,
  `CRASH6\<name>.MB`...). The font is the only name one disc lacks (above). The North American archive has 612 files to
  PAL's 697: one language, American, its own Euro-less font, `Credit01-03.psm` (read by its credits' text), and
  `FMV\H04_A.PSS` and `FMV\SPYRO.PSS` besides.
- The levels' data differs, and stays as it is (the reference for that disc is PCSX2 with its image, not the PAL disc). The
  beach played by the sweep at a fixed time parts from the PAL disc's at one frame: the player dies, and the North American
  disc restarts at the checkpoint a frame sooner, its first scene 0.2 s sooner. The PAL disc with that disc's
  `COM_CRASH_DEFAULT_DEATH` script (beach.rm2's script 0x131F, an earlier version of it, 112 bytes shorter) put in its own
  beach.rm2 plays frame for frame as the North American disc does; its other 200 different sections (most only the
  padding's bytes) change nothing (`compare/runs_c/divergence_note.txt`).
- The texts: one language, American, English texts and voices; the game reads no other there, so the settings' two language
  rows are shown only on the PAL disc, and the log says "languages: the SLUS-20909 disc's one, American". The console's
  wording becomes the PC's as on the PAL disc: the English entries of `pctext.cpp` serve American where its line is the same
  (22 of 28), six entries of its own where it's worded otherwise. The port's own texts are English there. Its font has 141
  glyphs (32 to 172, no accented letters; a character past them draws as a space), so the ui self-test checks each line's
  characters against the disc's font's glyphs (`FontCharacters`, from the font file's glyph table).
- The movies: TTIdent is 512x448 and Complete 640x480 (30 fps; Complete is another movie there, 76.5 s). The game stretches
  a movie over the screen's area and skips rows only of one taller than the screen, so in the PAL program's 512-line frame
  both show whole and in their 4:3 shape: Complete's lit band at 0.157-0.841 of the frame's height is its own picture's rows
  76-403 of 480. So the screen keeps PAL's 512 lines: no data of that disc asks for 448. PCSX2 with that disc's image
  agrees (`compare/runs_c/pcsx2_us_note.txt`): its start's movies fill the screen's height as the port's do, its loading
  screen and its title stand at the same heights within 0.4% of the screen.
- The sounds: every sound is the disc's data's, found by its number in the tables its own chunks load (levels, particles,
  menus). The two numbers the C++ fixes, 0x10A (the extra life) and 0x1BF (the school's kid herd), are the same in the North
  American program's same functions. Nothing is translated at load (a program that keeps PAL's tables in its code would
  have to).
- The level select's recipes: their self-test (`$TWIN_FIXES_TEST=recipes`) edits the same 117 scripts on that disc, none
  failed, none missing.
- The cutscene skip's table: every site and graph of its 51 cutscenes is a script of the same number in that disc's chunk.
  `check_cutscene_skips.py --iso usa` (`compare/runs_c/skips_us`): 45 of 46 cutscenes end as on the PAL disc's last full
  run (40 the same watched and skipped, 01, 29 and 34 different as there, 2 not ending from a chunk's start). The battleship's
  switch thrower (09) leaves Crash 0.7 units from the watched scene's place 0.5 s after it, the same a second later. The
  table stays the PAL disc's.
- A PAL bug that disc hasn't: Bandicoot Pursuit's Evil Crash (THE LIST 37, Bug fixes) follows Cortex on the North American
  disc with the fixes off too, his first route step not missed there. Its scenario has `expect_usa` and `forbid_usa`, the
  expectations a scenario has on that disc where its data plays otherwise.
- Mods: made for the PAL disc's files, so none is used with the North American disc (`Native::Mods::Scan`; the Mods page
  lists them and says so in a note; a disc image's import, which compares with the disc in use, isn't offered).

The checks for that disc: `check_windows.py --disc usa` (its work folder `build/native/check-usa`) checks the disc's paths,
the four self-tests with its image, the level recipes, the scenarios, the beach's jumps at 60 Hz against the PAL disc's at
60 Hz (crashwoc 0.8.1's North American 60 Hz jumped higher; here the five jumps measure the same) and the disc identity;
with `--sweep` every chunk of its disc against the PAL disc's sweep. The program's own checks (the pictures, the window, the
settings, the graphics APIs, portable mode, the log) are the PAL run's: the program is one. The scenarios play in real
time, so a machine busy with other runs fails some of them (frames of 300 ms and more in their logs): run them alone.
Measured 2026-11-21 (c435ff8): every line passes, scenarios 26/26, and the sweep's 131 chunks (108 played, 23 entered from
a neighbour) end as the PAL disc's do.

Which future changes reach the North American disc by themselves, and which to check there:

- A change of the code reaches it by itself: the program is one.
- A new read of the executable's data goes through the map: `convert_data.py` stops on a difference no decision names.
- To check with `--disc usa` (and in PCSX2 with its image where the PS2's way isn't known): a change that depends on a level's
  data (scripts, instances, a chunk's name), on sound numbers, on movies, on the cutscene skip's table, on the level select's
  recipes, on a file name in the C++ (`disc_paths.py` then) or on the game's texts (the ui self-test). Before each release,
  `--disc usa` once with `--sweep`.

## Checking a change

    .venv\Scripts\python.exe native\tools\check_windows.py [--build] [--sweep]

A line a check, the logs and pictures in `build/native/check/`, exit 1 when one fails: the four self-tests; the places that
once crashed played again (its `SCENARIOS`: Slip Slide's mine cradles; Dingodile beaten, then the boiler room's lock and the
Academy's hub; with the cutscene skip on, issue #9's: the angry skunk's scene skipped and the skunk attacked at once, the same
scene skipped and the skunk given back its own object, Cortex chased by the bear skipped and the controls given back, Rooftop
Rampage's scene in roofcor2 reached as Nina through the bus chase, skipped, and Cortex moved by the scripted input, the Academy
hub's first scene skipped and its staircase extended), each
failing on a crash and on the log's signs of memory gone wrong (the disk manager's pool out of room, DMA reading memory that
isn't there), the skip's ones also on what their logs must and mustn't say (`$TWIN_SCENE_STATE`, below); the pause menu's hints (its `HINTS`: the game's own with a PlayStation pad's, an Xbox pad's,
a Nintendo pad's and the keyboard's prompts, and the bar), each shown as the menu hints option has them and the game's own
clicked with the mouse (`$TWIN_HINTS_TEST`, below); the camera turned by the mouse (`$TWIN_LOOK_TEST`, below); the physics past
60 Hz (its `PHYSICS_RATES`, `$TWIN_PHYSICS_TEST`, below): the beach's jumps and Crash stepping off Cavern Catastrophe's lift with
Cortex tied, at a 144 Hz and a 240 Hz screen's uneven frames, against 60 Hz's (a jump's distance within 6%, its height within 2%,
its time in the air within 0.05 s, the lift's hop within 0.15); portable mode (`portable()`, `$TWIN_PORTABLE_TEST`, below): the
program copied to a folder of its own with a settings folder of its own as `%APPDATA%`, started as Explorer starts it (through
pythonw, with no console and no output handles, so its log goes to log.txt), six cases in turn: a first start with portable.txt
made by hand (`%APPDATA%`'s data copied to User, the log there, `%APPDATA%` untouched), turned on in the options over an older
User folder (set aside whole), turned off after playing portable (`%APPDATA%` set aside whole: nothing was written to it
meanwhile), a disc image found in the folder past a decoy and remembered relative, the folder renamed and the image still
found, and a folder no file can be made in (icacls's deny of `WD,AD`, as Program Files is: the settings stay in `%APPDATA%`, the
switch refused); the beach's
first 2,500 frames at a fixed time, every 100th picture compared byte for byte with the last packaged build's
(`build/native/dist/`, or `--reference PROGRAM`; a program whose folder is a player's portable one, with a `portable.txt` or
a `User` folder, runs from a copy of its own files, never with the player's settings and saves). Against 1.1.0 or a later
release (the reference's version information says which) they're compared once, as players have them. Against an older
one, first with the game's colour
filter and the scenes drawn into narrow buffers at the PS2's pixels, VU1's packets read at the XGKICK and mipmaps' levels
the 1x ones as 1.1.0 and before did (its `AS_BEFORE`: `$TWIN_HW_DEPTH_INDEX=0`, `$TWIN_HW_SMALL_SCENES=0`,
`$TWIN_VU1_KICK_AT_ONCE=1`, `$TWIN_HW_LOD_AT_SCALE=0`, `$TWIN_HW_THIN_PRIMITIVES=0`), then with the GS's two at the scale
(`GS_FIXES_ONLY`), where the
changed pixels must
lie within a PS2 pixel of an outline but one in a thousand (an edge between a near and a far surface of like colours is none
to the colours: a dark leaf over the olive behind it; the line "pictures with them at the scale" names the pictures changed,
their pixels, those off the outlines and where, and how far), then as a player has them all (the line "with VU1's packets
as the GIF reads them and mipmaps' levels and thin primitives at the scale too" names the pictures that changes and their
pixels); VU1's packets
(`vu1_packets()`: the first boss's cutscene in bossarea, Cortex against the sky, the frame's GS commands written as a GS
dump with the packets read at the XGKICK and as the GIF reads them, `gsdump.strips`: only the skinned strips' last two vertices' colours apart, GRAPHICS.md's
"Where the native build differs from the PS2", XGKICK); the colour filter (`colour_filter()`: the beach at 4x with the
camera looking up at Crash and Aku Aku, the filter at the PS2's pixels and at the scale: the PS2-sized steps gone, the
changes along outlines; and the software GS's `$TWIN_FILTER_PROBE`: every filtered pixel's index the depth's bits 8-15,
GRAPHICS.md's "The game's colour filter at the scale"); the scene buffers (`scene_buffers()`: bossarea's first boss cutscene at 4x, Cortex
against the sky the game draws into a 256 wide buffer, with that buffer at the PS2's pixels and at the scale: the PS2-sized
steps gone, GRAPHICS.md's "Scenes drawn into narrow buffers"); the draw distance (`render_distance()`, a minute: the beach's
frame 700 with the original distance, unlimited, and unlimited with `$TWIN_RENDER_DISTANCE=far`: unlimited draws more, keeps
the game's far plane there and the frame's lower half the original's byte for byte; the forced far plane moves the colour
filter's haze, NATIVE.md's "Draw distance"); with `--sweep`, every level
chunk, its outcome
against `native/sweep/results_windows.json` (a run that went on and one that ended in a game over count alike: the scripted
input decides which). Some 5 minutes, 25 more with the sweep. Each run has a settings folder of its own; the disc image is
local.json's. The runs that open a window have the mouse and mouse look off (`without_mouse`: a window takes the focus, and a
hand on the PC's mouse turned the SMAA run's camera in 1.2.0's first release check); the look test scripts the mouse
headless, the hints test with a window too (mouse=1 in its options: hintstest.cpp leaves the PC's mouse out from its start, so
its clicks are its own). `--hints-shots FOLDER` runs only the hints' check, with a window, and saves the pause menu's pictures there;
`--physics` only the physics' (a minute or so); `--scenarios` only the scenarios (two minutes or so); `--portable` only
portable mode's (under a minute; the dialog's pictures in `build/native/check/portable/shots/`); `--fps-shots FOLDER` only the
FPS counter's (its `FPS_CASES`, `$TWIN_FPS_TEST`, below: at 60 Hz, 50 Hz, 144 Hz without v-sync, and off; a minute or so), with
a window, as the counter counts the pictures shown (the self-test checks its sums and its place without one), and saves the
window's pictures there. The log's check (`session_log()`, `--log` alone, a minute or so) starts the program as Explorer does
(its own log.txt) three times: a start played on the beach for 25 s, the last session's log a 1.0.18 one that crashed with a path
in the profile folder in its last line, a mod and a few options; then a start that ends on an error (a disc image that isn't
there); then, with a settings folder of its own, `$TWIN_SETTINGS_LOG_TEST=1` (settingsshots.cpp: the settings screen opened from
the main menu, the refresh rate stepped from 144 Hz to 120, 60, 50 and 60 again, the resolution from 2x to 3x, 2x and 3x, a key
bound to F8, the screen closed, the program ended). Each log must have its kinds of lines in their order (`LOG_START`,
`LOG_ERROR`, `LOG_SETTINGS`: below, "The log"; the played start's performance lines with the graphics card's time), as many
lines of settings changed as it should (none in the played start, three in the settings' run) and nothing private: not the
profile folder's name anywhere, nor the user's in a path, nor the computer's, nor a path with `/` in a folder written as its
variable. With Vulkan the played start's log names the Vulkan device and says the card's time is unknown, where OpenGL's has
the timer query left out, and its performance lines say "the graphics card unknown" (`log_start()`).

The graphics API's check (`graphics_api()`, `--graphics-api` alone, under two minutes; THE LIST 73) runs five short window
starts of `--quick` with the setting and `$TWIN_GPU_API` empty, as a player starts: OpenGL's start left unfinished
(`graphics_start.txt` saying `opengl`) and Vulkan's, a PC without OpenGL (`SDL_OPENGL_LIBRARY` naming a DLL that isn't there:
SDL reads its hints from the environment) and one without Vulkan (`SDL_VULKAN_LIBRARY`), and a Vulkan start as it should be.
Each must show its window with the API expected, write its log line, leave the setting on that API and remove
`graphics_start.txt` once its pictures showed.

`--api vulkan` runs every check with Vulkan (`$TWIN_GPU_API=vulkan` for all the runs; the pictures are compared with the
reference's OpenGL ones, byte for byte); `--api opengl`, the default, runs them as a player's start (the variable not set).

## The log

What an issue's first questions ask, the log says itself (src/platform/native/sessionlog.cpp), short enough to attach: a line
for each thing, none a frame. Each line is `[native] [SECONDS]` from the start.

At the start, after the version's line (`Crash Twinsanity 1.0.19`: native/CMakeLists.txt's, only entry.cpp is compiled with it):

- `started`: the local date and time.
- `system:` Windows' name and version (RtlGetVersion's: build 22000 on is Windows 11), "the program emulated on Arm64" on an Arm64
  Windows, and Wine's version and host (ntdll's `wine_get_version`: Proton is Wine) or "not under Wine".
- `command line:` the arguments (not the program's own path), or none.
- `processor:` its name (CPUID's), its cores (a hybrid's performance cores: the highest efficiency class) and threads, AVX2 or
  not; the memory and how much of it is free.
- `settings and saves in` (portable.cpp's).
- `options (...)`: two lines of every option by its name in options.txt, and whether they're options.txt's or the defaults (no
  options.txt yet: a first start).
- `resolution (...)`: the render scale, the window's size or fullscreen, resolution.txt's or the defaults.
- `threads:` the threads asked for: three by default, one with `$TWIN_RENDER_THREAD=0` (the one-thread .bat's).
- `variables:` every `TWIN_` and `TWINSANITY_` variable set, with its value.
- `mods:` the mods folder's files (their paths in it, the first 20, then how many more), or none.
  Once the disc is open, a line each mod (on, off, refused; what it has), what its executable changes, the card its saves go on.
- `texture pack:` on or off and whether there's a textures folder (once it's read, `texture pack: N replacement textures in`).
- `the last session (log.previous.txt): VERSION for TIME, ...`: how the session before ended, from its log's end: its window
  closed (`quit`), another way out (`exit: ...`), an error (`fatal: ...`), a crash (`fatal exception ...`), a hang the hang watch
  saw with nothing after it, or no last line (closed from the Task Manager, a hang, the computer turned off). When the log goes
  to a terminal or a file, the settings folder's `log.txt` is the one read (the last start that wrote one).

Then, as each is there: the disc image and `disc:` what it is (below), FFmpeg's version, the graphics card (OpenGL's renderer and Windows' adapters, under
"Debugging"), `display:` the screen the window is on (its number of the screens, its name as Windows has it, its desktop's size
in pixels, refresh rate and scale, HDR on or off; "none" headless), `audio:` the device the sound goes to (SDL's name of it, a
person's name in it left out: "Headphones (someone's AirPods)"; the driver, its rate, channels and buffer) or why there's none.

The disc's line (discidentity.cpp, since 1.2.0) says what the image is, by its content, never its name: the program its
SYSTEM.CNF boots (VER), whether that program's bytes are a retail one's (PAL 1.01's SLES_525.68, NTSC-U 2.00's SLUS_209.09, by
their SHA-1), its size, and its CRC-32 and SHA-1 over the whole image against redump.org's dumps of the game (their disc pages,
read 2026-11-06: PAL 1.01 disc 4514, NTSC-U 1.00 42680 and 2.00 13689, NTSC-J 1.00 26782, the two demo discs 35281 and 28365).
`disc: the retail disc, SLES-52568 v1.01 PAL (Europe, Australia): its size, CRC-32 9055a7bc and SHA-1 are redump.org's disc
4514`; `a modified copy of the PAL 1.01 disc: ... the image isn't one of redump.org's dumps (2,579,234,816 bytes; redump.org's
disc 4514 has 2,558,263,296)` (a translation, a mod built into the ISO); `an unknown image: ...`. Only an image with a dump's size
is hashed (otherwise it can't be one), once: on a thread of its own in Windows' background mode (its processor and disc
priority below the game's; some 5 s for the PAL image here), its line logged when it's done (`(checked in 5.4 s)`), the hashes
kept in the settings folder's `disc_identity.txt` by the image's size and date, so later starts say `(checked before)`. Runs
with `--headless` (the checks) don't hash. An image the game refuses (another release) gets a line too: its program, its size
and the dumps of that size. `--identify-disc` recognises the disc image given (`--iso`, ...) there and then, hashing it, and
exits. The CRC-32 and SHA-1 code is TwinsanityEvolved's engine's (NativeEE, MIT: discimagehash.cpp, its notice kept;
native/third_party/twinsanityevolved).

`languages:` (languages.cpp, 1.2.0) says whether each text bank is in the language its name says (a bank's first lines against
TwinsanityEvolved's words of each language, languageprobes.h) and which language the voices are in.

`update check:` lines come only with the option on (updatecheck.cpp: the request's answer, "nothing newer", the newer version
and its page, or why there was no answer); `$TWIN_UPDATE_TEST=FILE` reads FILE as GitHub's answer, so the checks never use the
network.

While it runs, besides the game's trail (under "Debugging"): `game: LEVEL loaded in N s` for each loading (the title's and a
level's, from the loading state to the next), and `performance:` a line a minute of play (play and the cutscenes, not the menus,
the pause, the loadings or the movies): the frames a second, the slowest frame, a frame's work on each thread (the game's own
and its waits for the others, the window's present, VU1's, the render thread's; with fewer threads, which one has VU1 and the
GS), the graphics card's own time a frame, and the player's chunk. The threads' counters are those they keep anyway
(graphics/display.cpp's game work, renderthread.cpp's busy and wait times), a clock read and a few sums a frame: nothing is
profiled. `$TWIN_PERFORMANCE_EVERY=SECONDS` sets the minute (the check's 10).

Since 1.2.0 the render thread's time is split (graphics/renderwork.cpp): `the render thread 10.7 ms (the GS 6.1, OpenGL 3.2,
waiting for the card 0.4, the picture handed over 1.0)`. The GS is its emulation on the processor (the GIF's data, the hardware
renderer's work deciding what to draw; with `$TWIN_RENDER_THREAD=vif` VIF1 and VU1 too), OpenGL the time inside OpenGL's calls
the GS emulation makes (the driver's work: every entry point of hw/gl.cpp is counted), waiting for the card the pictures and GS
memory read back (`glReadPixels`, `glFinish`: the thread stops until the card has drawn them), the picture handed over the
frame's picture copied into the slot the window shows it from, and its wait for the window to have drawn the slot before. The
GS's share is what's left of the render thread's time once the others are taken out. Each change is counted with the
processor's time stamp counter (`rdtsc`, a few nanoseconds; twice an OpenGL call), turned into
nanoseconds against the steady clock over the render thread's first 2 s. The window's present (`the window's present 1.2 ms`)
is the game's thread's: the window's context drawing the picture and the UI over it and its swap (with vsync, the swap waits
for the screen's refresh); since 1.2.0 it's taken out of the game's own time (1.1.0's "the game" had it in). A PC whose present
takes long (a picture crossing from one graphics card to another, two screens on two adapters) shows it there; one whose driver
is slow, in OpenGL; one whose processor is slow, in the GS. With Vulkan (1.3.0) the split names it `Vulkan`: the time inside the
Vulkan backend's playing of the lists (SDL GPU's calls and the backend's own work making them), a read back's wait as waiting
for the card; the window's present is SDL GPU's (the picture and the UI drawn, the swapchain's picture waited for and shown), and
the card's own time is unknown (`the graphics card unknown`: SDL's GPU API has no timer queries).

Since 1.2.0 a frame of play over 50 ms gets a line of its own (at most 10 a minute of play, the rest counted in the next):
`slow frame: 180 ms in levels\earth\hub\huba; the game 150.2 ms (its loading 140.0 ms, of it the chunks' 138.5 ms, their
longest step 120.3 ms; reading the disc 2.1 ms) and 3.0 ms waiting, the window's present 1.1 ms; the render thread 20.4 ms (the
GS 12.0, OpenGL 7.2 with 2 shader programs made in 5.0 ms, waiting for the card 0.1, the picture handed over 1.1)`. The game's
loading is its background work while it plays (game/context.cpp's BackgroundWork: the chunks' loaders and readers, the
instances destroyed, the resources' steps, the disk pool's compaction), the chunks' share and their longest step
(chunkloading.cpp's UpdateChunkLoading: a reader handing over a file's section is one), reading the disc the game's reads of the
image (stream.cpp: synchronous, a slow drive's seeks show here), the shader programs the hardware renderer's new combinations of
the GS's states compiled and linked the first time they're drawn (hwgs.cpp's ProgramFor: OpenGL's driver work). The render
thread's numbers are its work in the same time (it draws the frame before). `$TWIN_SLOW_FRAME_MS` sets the 50 (a fast PC's
measures). After each performance line, when the hardware renderer made shader programs since the last (a first start, an
option drawing new combinations): `graphics: 3 shader programs made since, 45.2 ms in all, the longest 18.1 ms`; at its start,
`graphics: 31 shader programs made at the start in 180 ms (25 listed in shaders.txt, 31 this version's own)`. THE LIST 19's
hitches (#23, #32) were measured with these; "Hitches" under "Speed" has what they were.

Since 1.4.0 (THE LIST 92, #55/#56: Vulkan stopping 5-6 s in roof01 and the bus chase on a 4 GB GTX 1650 at 3x with a 3913-file
texture pack) Vulkan's slow frames end with its own split, `; of Vulkan's, textures made 2.0 ms, submits 0.8 ms, fences waited
5088.6 ms` (renderwork.h's VulkanPart: SDL_CreateGPUTexture with the new texture's levels cleared; the submits, where SDL
defragments its memory; the waits for the frames in flight, the card's own work behind them and SDL's destroying of the
textures released, THE LIST 97), and each performance line, and
each frame of a second or more, is followed by `memory: the program 1.34 GB (0.59 GB of it in RAM), the PC's RAM 6.8 GB free of
15.9; the texture pack's images held 0, 0.00 GB, on the graphics card 155, 0.12 GB; video memory 0.71 GB of a 10.97 GB budget
(NVIDIA GeForce RTX 4070 SUPER), 0.08 GB in shared memory` (the program's committed memory and working set; the pack's images
read and not given to the card yet, and those given to it with the bytes they take there, THE LIST 25a; Windows' own count of the program's video memory on the adapter where it uses the most, DXGI 1.4's QueryVideoMemoryInfo, whatever the
API, its budget and what it moved to shared memory; Linux has the RAM only). Measured on this PC (12 GB RTX 4070 SUPER) with
`compare\tools\vram_hog.exe 10` holding 10 GB in use (a 4 GB card's room left, about 0.5 GB, under the 0.7 GB the port takes
at 3x with the player's pack copy) and `compare\tools\vram_run.py` taking the port from roof01 through the rooftop, the bus
chase, the beach, the hub and back to roof01: Vulkan stopped 5107 ms in roof01, 5088.6 ms of it waiting on the card (the
textures made 2.0 ms, the submits 0.8 ms: neither SDL's allocations nor its defragmenting); OpenGL on the same way, under the
same pressure, 660 ms at most (the beach's loading, in the GS). Without the hog neither stopped in those runs (but Vulkan's stop
was another thing: THE LIST 97, below). SDL 3.4.18 can't be asked for
VK_EXT_pageable_device_local_memory (it chains only the core features' structures, not an extension's), so with Vulkan the
answer is less video memory (THE LIST 25: the pack's textures kept compressed) or OpenGL.

Vulkan's stops of 4-6 s (THE LIST 97) weren't the card. Without any memory pressure, on vram_run.py's way (ROUTE=steady), 1.3.0
stopped in 5 runs of 6 (4.1-5.8 s) and 1.4.0 before the fix in 2 of 6, in roof01, cogpa08 and highpath, mostly in the first
frame of play after a loading or a restart. `$TWIN_VULKAN_STALLS=1` showed the command buffer waited for done after 4-14 ms
while SDL_WaitForGPUFences took 4.1-4.3 s: after vkWaitForFences, under its submit lock, SDL destroys the textures released
since (SDL_gpu_vulkan.c 3.4.18, VULKAN_INTERNAL_PerformPendingDestroys), and each colour target view it destroys looks through
every framebuffer it keeps (VULKAN_INTERNAL_RemoveFramebuffersContainingView), about one a texture level cleared (each new
texture's levels are, by a render pass) or drawn into. Every stop came right after hwgs.cpp dropped its cache of textures
decoded on the card, past 4096: 4100 textures released, 28,422 levels, against some 31,000 framebuffers.
`compare\tools\sdl_destroy_bench.exe` does the same outside the game: 4096 textures of 64x64 with 7 levels cleared by render
passes take 3520 ms to destroy (1 level 71 ms, 2048 with 1 level 16 ms: the views times the framebuffers), zeroed by uploads
instead 13 ms. SDL's main branch still does it (2026-11-19). So the Vulkan backend keeps a texture it lets go of as a spare
(sdlgpu.cpp's LetGo, up to 256 MB; a block-compressed one, which has no colour target views, is released), and the next one
made with its format, size and levels takes it, its levels cleared again: nothing is destroyed when the cache is dropped and
made again. The drop's spares came to 181-197 MB (4096 of them, taken back as the cache fills again); the same way four times,
the drop in each, no frame over 1 s (the slowest 34 ms without a pack, the beach's loading 527 ms with the PNG pack, as 1.3.0's
539-618 ms there); and twice with vram_hog.exe 10 as THE LIST 92's run, which stopped 5107 ms, none either: 92's stop was most
likely this one.

The graphics card's time (graphics/gputime.cpp) tells a render thread slow on the processor from one waiting on the card: the
render thread's own time has the driver's work in it, and its waits for the card (a picture read back, a full queue), so 25 ms
there can be either. Each OpenGL call that gives the card work (`glDrawArrays`, `glClear`, `glBlitFramebuffer`,
`glTexImage2D`, `glTexSubImage2D`, `glGenerateMipmap`: the hardware renderer's, the window's picture's and the UI's, timed where
the three loaders take their entry points) is timed by a query of its own (`GL_TIME_ELAPSED`, OpenGL 3.3's), so the card's idle
time between calls (waiting for the thread to send more) isn't counted. A single query around a frame's work would count it:
it would read near the render thread's own time whenever the thread is the slow one. Each query's end is sent at once
(`glFlush`): a driver may send a call's work to an idle card at once and keep the query's end until the next call, the card's
wait counted (NVIDIA's without a window, one thread: 8.7 ms a frame against 2.2 with the flush; in a window the same with or
without). The results are read a few frames later, each when `GL_QUERY_RESULT_AVAILABLE` says it's there (the render thread
once it's made a picture, the game's thread when it shows one), never waited for. The picture read back (`glReadPixels`) isn't
timed: the thread waits for it, and its copy is small. Each thread's context has queries of its own (they aren't shared), the
one-thread mode's one context all of them. One frame in 8 is timed on each thread and counted 8 times: a query for each of
the beach's some 250 calls a frame, every frame, cost the render thread 0.4-0.5 ms (68.5 fps against 1.0.19's 70.2 at 4x and
144 Hz); a frame in 8, flushed, costs nothing seen (70.1 fps and 14.3 ms against 1.0.19's 70.0 and 14.3, three runs each,
alternated), and the minute's line still averages over 400 timed frames. The log says it once at the start, `graphics: the
card's time measured by OpenGL's timer queries (64 bits), a frame in 8`; `unknown` (with a line saying why) when the driver
has no timer entry points or no bits (`GL_QUERY_COUNTER_BITS` 0), its results never come (8192 waiting: the line says how
old the oldest is, the frames read with another context current and the queries left out), or `$TWIN_GPU_TIME=0` (which
leaves the entry points as they are: for measuring the timing's own cost). The results are read in their order, so one that
never comes held back every one after it: #32's log at 3x and 144 Hz had 8219 waiting 4 s into the title, the user's at 8x
with SSAA 8357, and the card's time was unknown for the rest of the session (1.1.0). 1.2.0 left out a query whose result
wasn't there 120 frames after it was begun while the next one's was, and begins none on a frame its thread ends with another
context current (its results couldn't be read). That wasn't enough: on the beach in a 1280x720 window at unlimited frames on
two cores (perf.py's runs), 1.2.0 too, several queries in a row never came half a second into the start on the render thread,
the next ones waited behind them and the card's time was unknown from 10 s on (8200-8500 waiting), as in #32's 1.2.0 log.
Since 1.3.0 (THE LIST 67) a query not there after 120 frames is left out whatever the next one's (a result comes a frame or
two after its query): `graphics: a timer query's result not there N frames after it was begun (the render thread's): left
out`, once a thread; if most never come (more left out than read, past 8192), the time is unknown again (`graphics: N timer
queries' results never came, M did`). Why they never came wasn't found (a begin the driver refused is the likeliest);
`$TWIN_GPU_TIME_TEST=stuck` makes each thread's first query one such, and the log's check has it. The time is kept by kind
too (the render thread's draws, clears, copies and uploads, the window's thread's): `$TWIN_GL_CALLS` logs it. Measured on the beach in
a 1280x720 window at 144 Hz (RTX 4070 SUPER): at 4x the render thread 14 ms a frame, the card 2.2 ms; at 1x with one thread,
the game's thread 28 ms, the card 1.1 ms. Without a window (the checks) the card's time reads higher (2.2 ms at 1x against
1.1): an idle card's lower clocks, most likely.

The settings changed in play are lines too, `options: NAME WAS -> NOW` by options.txt's names, with what the values mean where
the number doesn't say it (`options: refresh_60hz 3 -> 1 (144 Hz -> 60 Hz)`, `options: vsync 1 -> 0 (on -> off)`),
`options: resolution 2x -> 4x the PS2's picture`, `options: display a 1280x720 window -> fullscreen` and the controls
(`options: jump's key Space -> K`, `... second key`, `pad button`, `second pad button`: keys by SDL's English names, pad buttons
by their places). Each is compared with what the log gave last (the start's lines, then the changes'), once the settings screen
is closed (a value stepped through on the way to another, or stepped back to, isn't a change), at the next frame for a change
from elsewhere (Alt+Enter), and at the session's end (the window closed with the settings screen open): the ui's
`LogSettingChanges` (options.cpp, resolution.cpp, bindings.cpp).

At the end, whichever way it ends: `session:` its time, its time played, the frames a second on average, the share of the
played seconds below 95% of the rate chosen (the refresh option's 50, 60, 120 or 144; none when unlimited), the slowest frame
and a frame's work (the graphics card's with it); then how it ended, with its time and where the game was: `quit: the window
closed`; `exit: the program ended (status N)` (Main's return, a test's end); `exit: the game's C library ended the program`
(ProgramExit: the retail C library's abort, a pure virtual call, gcc2.cpp's Terminate: the game never exits itself, so it's a
bug), with the stack after it; `exit: portable mode turned on`; `exit: the level sweep's run ended`. An error (Native::Fatal:
the disc image, SDL's video, the sound modules) writes the summary, then `[native] fatal: ...` as the last line (the message
box shows it first); a crash's report (lowmemory_win32.cpp) ends with the summary ("it ended there"); `abort()` (a shader that
doesn't compile, a failed check) writes `[native] fatal: the program aborted itself` and the summary.

Logs end up in public issues, so the profile folder in any path (the disc image's, the settings folder's, the mods', the
command line's) is written `%USERPROFILE%`, and the settings' folders in it by their own names first, `%APPDATA%` and
`%LOCALAPPDATA%` (`%APPDATA%\Crash Twinsanity\textures`): as the system's code page has them and as UTF-8, their short forms too,
in any case and with either slash. A folder named as the user or the computer is `%USERNAME%`, `%COMPUTERNAME%`. Native::Log and
Native::Fatal do it for every line; a crash's report names only modules and functions. Nothing names the computer, the user, a
serial number or an address. Paths made of parts joined with `/` (the settings folder is kept with `/`, as SDL's) are written
with `\` throughout (Native::LogPath: the texture pack's, the fonts', the mods', a portable disc image's).

## Debugging

- A crash prints the registers and the return addresses: `llvm-symbolizer --obj=build/native/cmake/CrashTwinsanity.exe -f -i
  0x10000000+OFFSET` gives the lines (native/tools/level_sweep.py does it for its logs).
- LLDB (LLVM-MinGW's) runs it: `lldb build\native\cmake\CrashTwinsanity.exe`.
- `$TWIN_DEBUG_HEAP`: a small allocation freed twice or not at an entry's start, and a free outside the game's pool, said with
  where it came from.
- `$TWIN_DISK_TRACE`: every block of the disk manager's pool (the chunks' models, textures, animations) handed out and let go of,
  with its handle, its size and the code that asked, and every chunk's SM2 and RM2 loaded and unloaded;
  `native/tools/disk_use.py LOG [--by-caller]` sums it up (the pool's use at each chunk and game state, the most, and what held it
  when it ran out). A pool out of room is in every log anyway ("disk: no room"): the retail code goes on with handle 0, another
  block's, and its owner's data is overwritten later (1.0.6's crashes after Dingodile).
- `$TWIN_PIXEL_WATCH=X,Y` (and `$TWIN_PIXEL_WATCH_VERTEXES`): every primitive drawn over a pixel of the PS2's picture, with its
  state and vertexes.
- `native/tools/level_sweep.py OUT` plays every level chunk headless (native/LEVEL_SWEEP.md); on Windows it isolates each run's
  settings (`APPDATA`) and names the functions of a crash's backtrace with llvm-symbolizer. `$TWIN_SWEEP_TRACE` logs its progress
  every 5 seconds.
- `$TWIN_PROFILE=FILE` (`$TWIN_PROFILE_DELAY`, default 15 s, `$TWIN_PROFILE_SECONDS`, default 20): a sampling profiler of every
  thread (src/platform/native/profiler_win32.cpp); `native/tools/profile_report.py FILE` gives each busy thread's functions
  (`--lines` by source line).
- `$TWIN_FIXED_TIME=1`: the test runs' time (debug.h), every frame a frame's time: with `$TWIN_DUMP_FRAMES=FOLDER:N` (every Nth
  picture as a PNG; `FOLDER:N1,N2,...` those pictures), two builds' pictures can be compared frame by frame; with
  `$TWINSANITY_AUDIO=frames` too, runs agree
  frame for frame (the sound in real time changes a run after some 700 frames). The pictures are at the render scale: with no
  `resolution.txt` in the run's settings, the default 2x (4x before version 1.0.4), so compare builds at the same scale.
  `$TWIN_FIXED_TIME=144,144,60`: the frames' rates in turn instead (a 144 Hz screen's with a slow frame now and then: the uneven
  frames past 60 Hz, the same every run). `$TWIN_WINDOW_SHOTS=FOLDER:N` (with a window) keeps the same pictures as the window
  shows them (`window_NNNNNN.png`: made the window's size, the overlay over them), numbered as the frames are: what the
  resolution, the filtering and the anti-aliasing do to the picture shown (check_windows.py's "window" line).
  `$TWIN_PAD_SCRIPT=FROM-TO:WHAT[,WHAT...][;...]` holds the pad that way from the pads' FROMth read to before their TOth (a game
  frame each from the start, so with `$TWIN_FIXED_TIME` the same frames every run; "FROM:WHAT" one read): `rx=N`, `ry=N`, `lx=N`,
  `ly=N` a stick's byte (0x80 the middle, up 0) or a button's name (cross, square, circle, triangle, l1, r1, l2, r2, l3, r3, start,
  select, up, down, left, right), each span logged as it starts. With `$TWIN_PAD_SCRIPT_TIME=1` FROM and TO are seconds of the game
  clock since play started, with fractions (`10-60:ly=255;11.5-11.8:cross`): the same moments at any frame rate, as THE LIST 29a's
  chases need (High Seas Hi-Jinks' walrus run at 60, 144 and 240 Hz). The beach's camera looking up at Crash and Aku Aku against the
  sky: `620-700:ry=255`; Crash walked into the first boss's arena (its cutscene from about frame 690): `TWIN_START_CHUNK=
  Levels\Earth\Hub\bossarea` with `500-800:ly=0`. With the pictures above, a real run (a window or fullscreen, three threads) is
  the same place every time: a bug seen there is measured in the window as shown and in the picture drawn at once.
- `$TWIN_FRAME_STATS`: each frame's time, VU1's work, the primitives, the render thread's time and the GPU renderer's batches, texture decodes, uploads, downloads and copies.
- `$TWIN_GL_CALLS`: after each performance line, the render thread's OpenGL calls a frame by kind and the most called, the GPU renderer's counts, the card's time by kind and by the GPU renderer's pass ("The performance measure (perf.py)", under "Speed").
- `$TWIN_RENDER_SPLIT=1` (THE LIST 74, graphics/rendersplit.cpp): after each performance line, the render thread's time,
  primitives and records a frame by kind of drawing (`render split by kind`: the VU1 program whose XGKICK sent the packets,
  named as renderer/vuprograms.cpp registers it, with its address in .vutext; the GIF's paths 2 and 3; the work queued) and by
  the renderer's frame bucket they were sent in (`render split by bucket`), the busiest first. Needs the render thread. With
  perf.py: `--env TWIN_RENDER_SPLIT=1` (compare/runs_b74/split.sh runs the beach, the Academy's hub and Amberly's fight).
- `$TWIN_FAST_RENDERER=0`, `1` or `verify` (THE LIST 74, graphics/fastrenderer.h): the renderer's kind over the setting
  (Display > Renderer > Renderer, `fast_renderer`, shown once the fast renderer knows a kind of drawing): accurate, fast, or both
  ways with their results compared.
- `$TWIN_FAST_KICKS=`VU1 programs' addresses in .vutext (hex, comma-separated, as `$TWIN_RENDER_SPLIT` names them: `2df700`),
  with `$TWIN_FAST_RENDERER=1` or `verify` (THE LIST 74 step 3, GRAPHICS.md "The fast renderer"): only those programs' kicks
  drawn in the GS's runs (a run of primitives with one state drawn by one call of the backend, a strip's tag drawn at once)
  instead of every kick, the fast mode's default (`all`: every kick); the pictures must stay the same.
- `$TWIN_FAST_PREPARE=all`, or programs' addresses as above, with `$TWIN_FAST_RENDERER=1` or `verify`: those programs handed to
  the test's stand-in for a prepared function (the program itself, interpreted, its kicks converted into a batch of the GS's
  writes) in place of any real one: fast draws the batches, verify draws the program and compares the stand-in's batch and VU1's
  state with it. Interpreted VU1 is slow (the beach at about 9 fps with `all`).
- Verify's lines (`$TWIN_FAST_RENDERER=verify`, the real prepared functions or the stand-in): "fast verify: N runs of prepared
  functions compared, D declined, M differences, P pending kicks' prepared forms checked" at the first run, every 16384 and at
  exit (a declined run is one the function left to the program, which then ran as in the fast mode; a pending kick's prepared
  form is the strip a function leaves with the kick it leaves pending, checked against the packet in memory when VU1 sends the
  kick), and the first 50 differences, each naming what differs: the GS's writes (from which entry: a function's strips as the
  writes they stand for), data memory, the VF or VI registers, ACC, Q P I or R, the flags, where it left, the kick left
  pending, the pipelines' state the next program's entry reads (the FMAC entries and the division in flight, the integer
  loads, the branch rule, the pending flags), or a pending kick's prepared form against its packet.
- `$TWIN_HW_NO_FAST_STRIPS=1`: the hardware renderer draws a strip's triangles one at a time (DrawOne, as a run's), a prepared
  strip's or a raw kick's in a run, instead of at once where each would be drawn as the last one was (HwGs::DrawStripLikeLast,
  THE LIST 75 steps 2 and 3).
- `$TWIN_HW_DECODE_TRACE=1`: each texture the hardware renderer decodes from GS memory's copy, to stderr (the log): its place,
  format and size, the region the draw asked for ("(whole)" when the strips' rule widened it), the stale tiles' box, their
  pages and count (`decode: #62858054 2c00/4 psm 27 1024x1024 levels 1, asked 0,0-33,257 (whole), stale 0,0-263,263, 37 pages,
  1089 tiles of 1089`). `$TWIN_HW_DECODE_WHOLE=1`: a texture read from a render target a strip at a time decoded whole, as before
  1.3.0 ("Fewer passes" under "Speed").
- `$TWIN_HW_STATE_CACHE=0`: every OpenGL call of the hardware renderer made, even one that changes nothing, as before 1.3.0
  ("Fewer calls" under "Speed"): to tell a drawing difference from the calls left out.
- `$TWIN_HW_LIST_BYTES=N`: the hardware renderer's command list played once it holds N bytes (and when a result is wanted, and
  at the frame's end) instead of after each batch ("The command list" under "Speed").
- `$TWIN_GPU_API=vulkan` or `opengl`: the graphics API over the setting (Display > Renderer > Graphics API, `graphics_api`,
  THE LIST 73; "The graphics API" under "Speed"), for a test: the window made for it, and the hardware renderer's lists played
  by its backend (hw/sdlgpu.cpp for Vulkan, "The Vulkan backend" and "The window on the backend" under "Speed"); without a
  window (headless) the renderer's alone. With it set nothing writes `graphics_start.txt` and the setting isn't changed. The
  other API when one can't make the window show the game, the window made again for it (the log says why). The overlay's offscreen
  shots ($TWIN_UI_SETTINGS_SHOTS) draw with the renderer's API too, Vulkan on a device of their own. `$TWIN_VULKAN_DEBUG=1`: SDL
  GPU's debug mode with Vulkan's validation
  layers (the Vulkan SDK installed), their messages in the log, the textures named ("texture 141 64x64 format 0 levels 7"); run
  it with `SDL_ASSERT=always_ignore`, or SDL's assert dialogs open on the screen. In debug mode SDL refuses a viewport past the
  target, so the draws' far bigger viewport makes those runs draw differently. `$TWIN_VULKAN_LIST_BYTES=N`: Vulkan's list played
  in pieces of N bytes (4 MB). `$TWIN_VULKAN_STALLS=1` (THE LIST 97): a wait on the card of a second or more logs the command
  buffers it was behind, when each was seen done and the textures released since (`graphics: Vulkan: the renderer's thread
  waited 4265 ms for the card (...); #13148 the renderer's, 291 draws, 5.2 MB uploaded, submitted 0.0 ms before the wait, seen
  done at +14 ms; since the last: 4100 textures released, 28422 levels, ...`), and the renderer's texture caches dropped and the
  spare textures kept are logged ("Vulkan's stops of 4-6 s", under "The log").
- `$TWIN_HW_BATCH_HASH=FIRST:LAST`: two backends compared batch by batch. Each batch flushed while the GS's primitive count is in
  that range is read back after it and its target's colour and depth hashed (`batchhash 166576 p35910: into 800 program 4009 ...
  colour 57c15f8b21f33875 depth 0 2048x2048`); each picture logs its count (`shown 300 primitives 35948`: picture 300's
  primitives end there, its number the dumped frames'). The primitive count is the GS's, the same whichever backend draws
  (the batch count and `$TWIN_HW_BATCH_LOG`'s picture count aren't). `$TWIN_HW_BATCH_DUMP=FOLDER` writes each read back raw
  (`PRIMITIVES_BP_colour_WxH.raw`, depth as floats), `$TWIN_HW_BATCH_LOG=range` logs those batches' keys too.
  compare/tools/batch_hashes.py lists the first batches that differ between two runs' logs, with the dumps how many pixels and
  by how much. It found 71's two drawing differences in minutes (a target filled from GS memory left black, because an array's
  elements were set by name, and a blend a unit off, the shader's blend instead of OpenGL's).
- `$TWIN_HW_SOURCE_TRACE=1`: each kind of texture the hardware renderer samples straight from a target (a buffer it drew)
  rather than decoding it from memory, logged once ("graphics: a 256x256 texture at 2800/4 (psm 0) read from the target ...");
  `$TWIN_HW_NO_TARGET_TEXTURES=1`: none, every texture decoded from memory's copy (an effect's buffer then at the PS2's
  resolution): to tell that way's drawing from the decoder's (GRAPHICS.md, "Ant Agony's walls and doors").
- `$TWIN_NEAR_PLAYER_PARTICLES=0`: the farther draw distances' particles by the played character cut off by the camera's
  distance as the original's are (THE LIST 98, NATIVE.md's "Draw distance"): to compare with the rule (cmp_options.py's A: and B:
  variables); `$TWIN_PARTICLE_TRACE` logs each system the rule keeps on, the first time.
- `$TWIN_RENDER_DISTANCE=far`: the draw distance's far plane its choice's even where the colour filter reads the depth
  (renderdistance.h, NATIVE.md's "Draw distance"): to see what it would change. The log's "render distance: the far plane Nx"
  lines say when the far plane moves.
- `$TWIN_GPU_TIME=0`: the graphics card's time not measured (the log's `the graphics card unknown`, the entry points
  OpenGL's own): to measure what the timing costs. `$TWIN_SETTINGS_LOG_TEST=1` (headless): the settings screen driven from the
  main menu through the log check's changes, then the program ends (settingsshots.cpp, beside `$TWIN_UI_SETTINGS_SHOTS`).
  `$TWIN_SETTINGS_PLAY_TEST=1` (with a level, a check_windows scenario): after 3 s of play F1 opens the settings over the game's
  pause and their back resumes play ("settings play test: pass"). `$TWIN_SETTINGS_MOUSE_TEST=1` (headless, THE LIST 107): the
  settings opened from the main menu on the Enhancements tab and the mouse driven as the window's events would (the wheel and
  the left button's release through the screen's event watch): the wheel turned down and up over a choice's arrow, the track
  clicked below the thumb, the thumb dragged past both ends, ten keys down, then on the audio tab the wheel over a slider and a
  click at a quarter of its bar; no value changed but the slider's, the row under the pointer highlighted, the keys' row in view
  ("settings mouse test: pass (...)" with the scrolls in reference pixels; check_windows' "settings mouse").
- `$TWIN_SCREEN_TEST=1` (a window's run on a PC with two screens or more, THE LIST 88): every 120 frames the Monitor row's change
  made as the settings make it (the last screen, fullscreen, the first screen, the last again, windowed, automatic), each logged
  as the settings' closing logs it ("options: screen ..." and "display: screen N of M"), 60 frames later where the window is
  ("screen test: step N settled, the window on screen 2 of 2 (NAME), at X,Y, WxH, fullscreen"), then "screen test: done". A
  monitor saved in resolution.txt (`screen=NAME`, `screen_order=N`) that isn't connected: "display: the screen chosen (...) isn't
  connected, so the main screen instead".
- The log (what a report needs is under "The log", above): each line `[native] [SECONDS]` from the start, the first the
  version's. Once the window is there it names the graphics card and its driver:
  OpenGL's renderer, vendor and version (the driver's own number in it: NVIDIA's "3.3.0 NVIDIA 617.14", AMD's "... Core Profile
  Context 24.10.1...", Intel's "... Build 31.0.101..."), then each adapter Windows has (graphics/adapters_win32.cpp, DXGI's: its
  name, PCI vendor and device, its driver's version as Device Manager shows it and its video memory). On a laptop with two cards
  the first line says which one draws. Since THE LIST 87 the program exports NvOptimusEnablement and
  AmdPowerXpressRequestHighPerformance, both 1 (adapters_win32.cpp), which NVIDIA's Optimus and AMD's switchable graphics read
  as it starts to put it on the dedicated card (#23's log had an Intel HD beside a GTX 1050 Ti). Windows' own choice for the
  program (Settings > System > Display > Graphics) still comes first. `llvm-objdump -p` lists the two in its export table. The game's trail (ui/trail.cpp) is always in it: the game controller's
  states by name with the player's place (read without changing the game's), the player's chunk as it changes, the chunk a level
  loads at, the memory card's writes and the movies opened. The detailed log (Options > Game, `detailed_log` in
  options.txt, or `$TWIN_DETAILED_LOG`) adds the player's place every 5 s of play, every sound played (game/sound.cpp, as
  `$TWIN_SOUND_CHECK` logs them), every file looked up on the disc, and the camera when the follow camera's view stands still
  (not moved nor turned) for 3 s of play while the player moves 2 units or more: a "camera:" line, then `$TWIN_CAMERA_TRACE`'s
  three (below), once until the view moves again, 20 times a session at most (#52's camera that stopped following Crash after
  the high path's training scene, never seen here: its causes told apart, the follow rig not stepped, the lens on another rig,
  the camera's instance in another chunk, a trigger's camera holding still). A crash's or a hang's stack names each function of the
  executable (its COFF symbol table, read from its file when the report needs it; C++'s names mangled, `llvm-cxxfilt` reads
  them) after its module and offset.
- The hang watch (lowmemory_win32.cpp): a thread of its own counts the game thread's frames (its waits for the vertical blank,
  and a movie's frames, paced by the sound instead).
  When none finishes for 20 s, it writes to the log the game's state, the game thread's stack (the thread stopped while it's
  read) and the player's chunk, then says when the thread goes on. A hang that's ended by closing the game keeps its log: each
  start moves the last `log.txt` to `log.previous.txt`. `$TWIN_TEST_HANG=SECONDS` stops the game thread that long once play
  starts (its test).
- `$TWIN_SOUND_CHECK` (and the detailed log): every sound the game plays, logged with its pitch (the sound's own, the scale,
  what's sent) and volumes; its samples where the SPU2 plays them from (their blocks, those with samples, a hash), compared with
  its first play (a sound whose memory changed under it is said "NOT as it first played"); what each voice put out once it
  stops (its samples, its peak, whether to the dry mix and to the reverb); every transfer to and from the sound memory; and
  STREAM.IRX's work on it (a bank's read and where to, its load, a sound freed, each core's reverb on and off with its area).
  `$TWIN_SOUND_CHECK` also logs the mix's peaks every second, the compaction's steps and the stream buffers' transfers.
  native/AUDIO.md has the bug they found. `$TWIN_SOUND_BLOCKS`: the
  module's blocks of sound memory every 10 s. `$TWIN_TEST_SOUND=0x22e,0x22d`: those sounds played every 3 s of play from 5 s in
  (`$TWIN_TEST_SOUND_DELAY`: from so many seconds).
- The level sweep's tools for places it can't start in or reach: `$TWIN_HOP=cryscave,escape` takes the player through its
  chunk's link to each of those chunks in turn, as the game's WarpToChunkLinkTowardsPlayer places an instance (half a unit in
  front of the link's load wall, where the link takes it; each chunk's links are logged as the player enters it, with their
  walls' middles, a `fly` step's way through). Other steps: `load:Levels\AltEarth\Core\corea` loads that chunk as
  `$TWIN_START_CHUNK` does, the chunks played before staying the renderer's history (check_windows.py's Ant Agony scenario:
  bossarea's sky, then corea); `goto:X:Y:Z` puts the player there (its physics then
  move it from its last place: a wall on a long way stops it, a gap drops it); `fly:X:Y:Z[:SPEED]` takes it there in a straight
  line, a little further each frame (10 units a second), so it doesn't fall on the way and the chunk's triggers and links see it
  go by: a cutscene it starts plays, a link takes it into the next chunk (the flight ends there, or when the player is another),
  as check_windows.py's Rooftop Rampage scenario crosses the bus chase from its start into roofcor2 as Nina; `moved:N` logs how
  far the player went since the last step and whether that's N units or more ("NOT": a player the scripted input doesn't
  move); `objects` logs the
  instances of its chunk (their objects' names and places); `on:mine` puts it 2 units above the nearest instance whose
  object's name holds "mine" (`on:mine:5`: 5 units) and `bring:mine` puts that instance where the player is instead;
  `states:crate` logs those instances' places, their context's flags, their agent's state flags and their part's bits (an
  outline crate's collision and triggers' signals off: #58); `damage:tnt` sends the nearest such instance an explosion's damage
  as the scripts' CreateDamage does, `knock:tnt` a rigid body's knock over a crate's breaking (its agent's Collided) and
  `trigger:tnt` runs its trigger event as a "!" crate's link does (#58's scenario); `idle`
  stops the scripted input and `input` starts it again; `wait5` waits 5 s (`wait1.9`: fractions too); `die` drops it to its death; `mask` gives it a mask as an Aku Aku crate does (the fourth in a row makes it invincible); `hurt` takes a hit point from it and starts 2 s of blinking, as a hit that isn't kept out does (#18's three masks scenario); `movie:H03_F` plays the game's movie whose file has that in its name, as the game asks for one in play, play going on after it (`$TWIN_MOVIE_GUARD=1` puts each piece of a movie's stream right before a page that can't be read, so that FFmpeg reading past a piece crashes every time: check_windows.py's #49 scenario); `credits` rolls the end credits as the game's end asks for them, the Iceberg Lab played after them (#20's scenario: 135 s of credits, not counted as play); with `$TWIN_SWEEP_GAME_TIME=1` the steps' waits and flights go by the game clock instead of real time, so a run with `$TWIN_FIXED_TIME=1` and `$TWINSANITY_AUDIO=frames` makes the same pictures every time (two builds', or two switches', compared frame by frame); `$TWIN_NO_SHADOWS` leaves the characters' shadows out, to compare what they darken; `scripts:director`
  logs the behaviour scripts of the instances whose object's name holds "director" (each runner's graphs, the state they're in,
  every state's bodies: condition, commands, jump) and `state:GRAPH:3` makes the runner that runs a graph whose name holds GRAPH
  jump to its state 3, as a body's jump would (a boss beaten without playing it: check_windows.py's Dingodile). Down a slide (Slip
  Slide Icecapades) the player's physics undo `goto` and a hop stops the slide: start at the level's start, let the scripted
  input get through its cutscene, then `idle` lets the slide go on by itself and `bring` puts things in its way.
  `$TWIN_TRIGGER_PLAN=list` lists the message triggers of the player's chunk (index, place, box, the clock units between two
  checks of its box and whether it's polled at all), then its camera triggers (the same, and the kinds of nodes their events go
  to; not steps of a plan); a list of steps (`4,die,4`)
  puts the player in a trigger or drops it to its death every 8 s of play (`$TWIN_TRIGGER_EVERY=SECONDS`; a cutscene a step
  starts plays out first), the lives kept up. `$TWIN_SWEEP_IDLE` gives no scripted
  input (cutscenes play out). `$TWIN_TEST_PICK=PATH` is the disc image dialog's pick (empty: cancelled).
  `$TWIN_SWEEP_SHOTS=FOLDER:SECONDS` (with a window, not `--headless`) saves the window every SECONDS of play as it's shown
  (`shot_000.png` on): the README's pictures came from it, a run's `resolution.txt` saying `scale=4`, `window=1600x900` and
  `fullscreen=0`.
- `$TWIN_SKIP_TEST=SECONDS` (ui/features.cpp; the skip option on in the run's options.txt, `skip_cutscenes=1`): in every scene
  that can be skipped a button pressed SECONDS after the scene's first poll and held for the hold's time (1 s) and half a second
  more; each skip logged with the scene's polls and the longest gap between two, each scene that wasn't skipped as the next
  begins. A level start plays none (the player starts in play): with `$TWIN_SWEEP_IDLE` and a trigger plan (`0,1,2,...`) the
  triggers start them. The hold is lost when the polls stop for more than 0.25 s (a new scene), so a gap near that is the thing
  to look for. What a skip does to end the scene is logged too ("skip: ...": NATIVE.md, "What a skip leaves");
  `$TWIN_SKIP_ACTOR_SECONDS` changes how long an actor may go on with the scene's behaviour (8 s).
- `$TWIN_SCENE_STATE` (ui/sweep.cpp, with the sweep): what a cutscene leaves behind, 1.5 s into the play after it
  (`=SECONDS`: that long): the player's locked inputs (`ffffffff` all), place and behaviours (each runner's levels' graphs and
  states), every instance whose object node takes its object from another's (an actor a scene didn't give back), every instance
  running a scene's graph (CUTSCENE or _CS in its name), and every agent of the last cutscene (cutsceneskip.cpp's: where it is,
  whether it's drawn, its runners); twice, the second a second after the first, both counted in game frames (60 a second, so two
  runs with `$TWIN_FIXED_TIME` take them at the same frame). A scene played out against the same skipped tells what the skip
  leaves undone; the angry skunk, played out, matches the PS2 game's memory after it (PCSX2 below).
- The cutscene skip's checks (cutsceneskip.cpp, NATIVE.md "The waits' skip"): `$TWIN_SKIP_ALL_SCENES=1` lets the waits' skip end
  every cutscene, checked or not; `$TWIN_SKIP_WAITS_FIRST=1` makes it go first where a skip rule is polled too; `$TWIN_HOP`'s
  `tell:NAME` (or `tell:NAME:MESSAGE`) sends user message MESSAGE (87) to the instances of the player's chunk whose object's name
  holds NAME, as a trigger tells them (a cutscene's director started where a test can't walk to its trigger).
  `$TWIN_SWEEP_STORY=N` starts the sweep with the story at area N (the open areas up to it), as a played game's progress is
  there: several directors start their cutscene only at a point of the story (`StoryAreaCondition`; the Psychetron room's two,
  the beehive, Cortex's pipe, the shed), and told at story 0 they do nothing. `$TWIN_SWEEP_PAIRING=3:0:1` starts it with the
  characters paired as a checkpoint would have them (3 the Rollerbrawl, Crash driving, Cortex riding): the ball's last chunk
  (l05brawl) rolls into Dingodile's hut by itself (THE LIST 90b). `$TWIN_SCENE_SCRIPTS=12:director:act_cortex` logs
  the behaviour scripts of the instances named like each NAME (any chunk), 12 s into every cutscene watched, as the hop's
  `scripts:` step does in play (the hop waits while a cutscene plays): what a cutscene that never ends waits for.
  `native/tools/check_cutscene_skips.py [NAME...] [--waits] [--rate 144|240]` plays each cutscene of its list watched and skipped (and skipped by
  the waits) from the same start and compares what each leaves; a scene's own variables (the story, a later press of the
  button for a part chained onto another's scene, an earlier scene state) are its list entry's fifth item. Its runs' folders and
  settings are absolute paths, and each run is given the disc image (`$TWIN_TEST_PICK`), so a missed settings folder can't open
  the disc image dialog.
- `$TWIN_SCRIPT_DUMP=6147,6149` (or `all`; levelrecipes.cpp): those behaviour scripts logged as the game reads them and as the
  level recipes edit them: each state (the start marked, its bits: the child script in the top half), each body's condition (its
  ID and parameter, ! inverted, its interval) and jump, each command's ID and argument words. commands.h and conditions.h name the
  IDs (their classes' `// N` comments).
- `$TWIN_FIXES_TEST=skips` (with `--headless --quick`; the skip option on): every level's scripts as the skip option has them,
  each skip's rule in a state that runs a scene (572 with a player, or Triangle) with what the scene does that the skip's path
  doesn't (inputs given back, actors given their own object, messages sent, the cutscene ended, warps, the playing character),
  and every scene no rule skips ("unskippable": not its runner's state, not its own states, not the state that runs its runner).
- `$TWIN_HINTS_TEST=1` (with `--quick`, ui/hintstest.cpp): the beach played a few seconds, then the pause menu: its hints
  checked as the menu hints option has them and logged as the prompts label them (`$TWIN_UI_PROMPTS=playstation|xbox|switch|
  keyboard` picks the controller), then the mouse clicks the pages' right and left arrows and back; "hints test: pass" and exit 0.
  A click whose step hasn't happened 5 s on is clicked again, once, and logged ("hints test: clicked again"; check_windows.py's
  line names such cases: a press the pause didn't take while a save's result was due or a menu appeared, or a click lost).
  The one-off "stuck (state 15, pause reason 1 or 9, step 3 or 4)" of 1.0.19's first release check (a page arrow's click not
  taken) was never seen again: 1.0.19's own program (no second click, no wait for a drawn menu) passed 80 cases four at a
  time (2026-10-30), then 200 more on 2026-10-31 with the processor loaded further (two at a time with six threads kept
  busy, four at a time with four: 32 fps on average, 4 to 49, where the failure was at about 48), and the later program 130.
  Its cause stays unknown; the second click and the wait cover both candidates.
  `$TWIN_HINTS_TEST=FOLDER` without `--headless` also saves the window as it's shown (the picture and the overlay:
  `NativeGraphicsShootWindow`) there: hints_pause.png and hints_levels.png. The test takes the mouse from its start
  (`ScriptMouseTakeOver`: the PC's mouse left out), and its runs need the mouse option on: with mouse=0, as check_windows'
  window runs had it from 46374b5, the menus drop its clicks and the four clicking cases stopped at step 3.
- `$TWIN_PARTICLE_TRACE=1`: each particle section's systems as they're read (their chunk, index, name, slot, blend, rate, life,
  sizes, first and last colours, velocity, gravity, cut-off and page) and its emitters (their chunk, place, system's name and
  index, position, switch and group): check_windows.py's #41 scenario.
- `$TWIN_CAMERA_TRACE=1` (the sweep): every second of play or of a scene, the camera shown (the game controller's state word:
  0 the player's follow camera, 3 the game's rig, 4 the cutscenes'), where its instance is, the player and how far apart they
  are (check_windows.py's #23 scenario: the follow camera back after the high path's skunk scene, 7 units from Crash), and how
  many degrees round from the player's back the camera is, seen from above (THE LIST 83's assisted camera); then the player's
  follow camera (sweep.cpp's LogCameraLines, read without changing the game's): whether its instance is the one shown, the
  chunks it and the player are in, the rig its lens shows (the follow rig, the game's, the cutscenes', another), the character
  the progress plays (FollowNode::Update stops following when it's another instance), where the rig is and the point it looks
  at, and the positioner's bits that hold it (keyed, holding still, looking only, the target still at the trigger's rate 0:
  after a `goto` the view stays put until Crash moves by himself, the retail rule, not a bug). The hop's `unfollow` step stops
  the follow camera as FollowNode::Update does for another character's instance: the view stands as #52's video shows and the
  detailed log's "camera:" lines come (check_windows.py's #52 scenario: the high path's training scene reached from HubA as
  that player came, the camera following Crash after it, then `unfollow`).
- `$TWIN_MOUSE_TRACE`: each click a menu takes, logged with how many lines and hints the frame before drew and how many are
  under it (mouse.cpp; a click after a frame that drew none of the menu waits up to half a second for one that does).
- `$TWIN_FPS_TEST=1` or `=FOLDER` (with `--quick` and a window, ui/fpstest.cpp): the beach played a few seconds, then the status
  button held (the HUD shows) and the FPS counter checked, then the pause menu and again: with the show FPS option on, what it
  shows has to be the game's own frames a second (counted by the test over the same 2 s, within 8%), and at 50 and 60 Hz the
  paced rate (within 5%: the game holds it); past 60 the rate the computer reaches is logged; off, nothing shown. With a folder the
  window is saved there as it's shown, whatever the checks found: fps_play.png (the HUD and the counter) and fps_pause.png (the
  menu's hints and the counter). "fps test: pass" and exit 0.
- `$TWIN_LOOK_TEST=1` (with `--quick`, ui/looktest.cpp): the beach played a few seconds, then the mouse moved right at a steady
  3000 points a second (mouse look holding it without a window, headless too) with the mouse sensitivity and camera speed at 5
  and 5, 10 and 5, 5 and 10, the follow camera's turn measured over a second of its clock: the stick's full deflection's 120
  degrees a second at the defaults (the game's fastest), six times that, twice; "look test: pass" and exit 0.
- `$TWIN_PHYSICS_TEST` (ui/physicstest.cpp): the player's moves measured by the game clock, to compare frame rates (with
  `$TWIN_FIXED_TIME`'s frames). `jumps` (with `--quick`): from where play stands after 3 s, the player put back there before each,
  a standing jump, a running jump, a slide jump, a jump and spin and a double jump the same way in the world (the camera's way, or
  `$TWIN_PHYSICS_WAY=DEGREES` round from it; the check takes -90, the beach's flat way; `$TWIN_PHYSICS_MOVES=013` only those,
  as for Crash and Cortex tied, who have no slide: tunnel01 with `$TWIN_PHYSICS_TIED`, way 0; `56` the double jump option's two,
  a double jump pressed in the fall and one after a slide jump, made only when named), each
  logged with its time in the air, its distance along the ground and its height. `watch`, or `step[:SECONDS]` (the stick pushed
  `$TWIN_PHYSICS_WAY`'s way first): every time the player leaves the ground, as it lands; with the level sweep to start a chunk
  (`$TWIN_SWEEP_LEVEL`, `$TWIN_SWEEP_IDLE`), `$TWIN_PHYSICS_ON=NAME` (the player put on an instance 2 s in: `cavern_lift17`),
  `$TWIN_PHYSICS_TIED` (Cortex tied to it) and `$TWIN_PHYSICS_SECONDS` (the watch's end). `$TWIN_PHYSICS_TRACE=FILE`: every
  frame's place, velocity, ground and movement node's velocity as CSV.
- `$TWIN_WATCH=NAME[,NAME...]` (with `$TWIN_HOP`): every frame, the live instances whose object's name holds a NAME (any case, any
  chunk), with the sweep's clock (the game clock's with `$TWIN_SWEEP_GAME_TIME`), their place and the player's: `watch: frame F t
  T NAME#ID at (X, Y, Z), the player at (X, Y, Z)`, ID the instance's address's low bits (two instances of one name told apart).
  THE LIST 29a measured its chases with it. `$TWIN_FRAME_STEPS=1`: what the game counts in frames (a rigid body's launch
  and ride steps, its drag, grip and press) counted a frame at a time past 60 Hz too, as retail did: the way to see what 29a fixed
  (WINDOWS.md "Frame rates past 60"). `$TWIN_CHASES_PAST_60=1`: Bandicoot Pursuit's and the walrus's chunks at the rate chosen
  past 60 Hz, not held at 60 (29c): the way to measure the chases for 29b.
- `$TWIN_PORTABLE_TEST=1` or `=FOLDER` (with `--headless`, ui/portabletest.cpp): from the main menu the settings opened,
  the Game tab's portable mode row stepped and its dialog followed with its own input: asked, left to yes and select copies,
  done, select closes the game as a player's would (exit 0; the folders are check_windows.py's to look at); refused (the folder can't be written to),
  select, "portable test: pass (refused ...)" and exit 0. With a folder the screen is saved there at each step
  (portable_ask.png, portable_copying.png, portable_done.png, portable_refused.png).

### The PS2 game beside the port (PCSX2)

To tell a port's bug from the game's own, the same place is played on the PS2's code in PCSX2 and its memory read by symbol:
`tools/run_pcsx2.py` drives PCSX2 through PINE (memory reads and writes, save states: `tools/pine.py`). On Windows it runs
local.json's `"pcsx2"`, which has to be a portable copy of the tests' own (`portable.ini` beside `pcsx2-qt.exe`, a copy of the
user's BIOS in its `bios`, its `inis/PCSX2.ini` with `[UI] SettingsVersion = 1` and `SetupWizardIncomplete = false`, else PCSX2
starts its settings again and its first run's wizard stops the boot, and `[EmuCore] EnablePINE = true`, `PINESlot = 28112`):
its cards and states are the run's, never the user's. PINE is a TCP port on Windows (127.0.0.1:28112).

    .venv\Scripts\python.exe tools\run_pcsx2.py retail "C:\path\to\Crash Twinsanity.iso" --quick --playing-snapshot
    .venv\Scripts\python.exe tools\run_pcsx2.py build\SLES_525.68.elf "C:\path\to\Crash Twinsanity.iso" --manual --press 40:cross

`retail` boots the disc's own executable, its symbols `symbol_addrs.txt`'s (splat's `D_XXXXXXXX` labels by their address):
`--watch`, `--goto`, `--string`, `--chunks`, `--snapshot` work on it. The PS2 build of the decompilation (`build/SLES_525.68.elf`
and its map, not byte for byte the retail one: its debug hooks move the addresses) adds `--press`, `--at-frame` and
`--backtrace`. The script's own docstring has every switch. Checked 2026-10-24: the retail game to the beach in 33 s through
PINE, its chunks read and a snapshot kept; the PS2 build's pad pressed through `g_TestPadButtons`.

To play a place of the retail game with no saved game, `--start-chunk 'Levels\Earth\Hub\highpath'` starts in that chunk as a new
game, as `$TWIN_START_CHUNK` does: at the logos the path is written below the executable (0xFF000, where nothing of the game
lives), the progress's start chunk made it with room for any later copy, the entry a new game's and the next state the level's
loading. `--write WHEN:SYMBOL[+OFFSET][>OFFSET...]:WORDS` writes words there (hex, or floats `-9f`; `>` follows a pointer;
WHEN `pN` is N seconds after the game first plays): the player moved into a trigger is
`--write 'p20:G_UnkInstanceContextRefCounter>0>8>0x30:-9f,-4f,-50f,1f,-9f,-4f,-50f,1f' --write 'p20:G_UnkInstanceContextRefCounter>0>8>0x60:0'`
(its place's matrix row and position, the place marked in step). `--dump-ram WHEN:FILE` keeps the EE's 32 MB, to read
afterwards (game structures by their PS2 offsets: the `CHECK_OFFSET`s). Checked 2026-10-25 (issue #9): Cortex chased by the bear
(`--start-chunk 'Levels\Earth\DocAmok\docamok3'`, its scene at the start, `--watch 'G_UnkPlayableCharObjInstCxt2>0xF0'`: the
player's locked inputs) has them all locked through the scene and none after it; the angry skunk (high path, the player moved
into its trigger, the memory kept after the scene, the skunk's object node found by its object's name) takes its own object
again (source node 0) with no behaviour running, as the port leaves it played out, and as the skip now leaves it.

To compare pictures, `--gs KEY=VALUE` sets the run's `[EmuCore/GS]` (PCSX2.ini's names: `Renderer=13` the software renderer,
`upscale_multiplier=8`, `LoadTextureReplacements=true` with a pack in the copy's `textures\SLES-52568\replacements`), and
`--screenshot T` (or `pT`, after the game first plays) keeps PCSX2's own screenshot at the internal resolution as drawn (4096 x
4096 at 8x, the port's own `$TWIN_DUMP_FRAMES` size; the states' pictures are 640x480 whatever the scale) as
`screenshot_[p]T.png`: PCSX2 takes its hotkeys only from the window in front, so its window comes to the front a moment (F8,
bound in the copy's PCSX2.ini) and the window before it gets it back; nothing is pressed if PCSX2's isn't in front. Checked
2026-10-31: the beach at `p9.2` (`--start-chunk 'Levels\Earth\Hub\beach'`) is the port's frame 600 there, Crash standing in
the same pose.

### The characters against PCSX2 (compare_models.py)

`native/tools/compare_models.py` puts the port's pictures beside the PS2 game's in PCSX2 (the tests' own copy) at the same places
and moments, and makes a local page of them: `..\compare\page\LABEL.html` (beside the working tree, in no repository: the
pictures are the game's; `--out` another folder; `--label` names a build's page and runs, `current` by default). Its moments:
the beach with the camera looking up at Crash and Aku Aku against the sky, and three of the first boss's cutscene in bossarea
(Crash walked into the arena): Cortex against the sky, Crash close up, the characters at the party. The same input in both: the
port's pad script ($TWIN_PAD_SCRIPT: `620-720:ry=255` on the beach, `500-800:ly=0` in bossarea) and in PCSX2 pad 1's stick held
through keys the test copy binds to it (`run_pcsx2.py --key p0.82:rdown:2.0`, `--key p0.8:lup:6.0`). The port runs at 50 Hz
there, as the PAL game in PCSX2 (the follow camera's smoothing goes a frame at a time: at 60 Hz the beach's camera turned
otherwise), and PCSX2's times are the game's own (`--game-time`: its global clock, `G_GlobalClock_`). PCSX2 plays each place
once at 1x, where it keeps the PS2's speed, writing a GS dump of the retail game at each moment (`run_pcsx2.py --gsdump T`,
4:3, and widescreen for the player's configuration); each configuration's PCSX2 picture is that dump drawn again with its
settings (`run_pcsx2.py replay`), so the picture and the commands are the same frame's whatever the settings cost (played at
8x with its screenshots, PCSX2 took a second and more for each, and the moments slid along the cutscene). Three
configurations:

- player: as the player sees it. The port fullscreen at the screen's size, three threads, with copies of the player's
  options, resolution and texture pack (taken once from `%APPDATA%\Crash Twinsanity` into `..\compare\user`, never written
  there; the FPS counter off), the window as shown; PCSX2 fullscreen with the rendering keys of the [EmuCore/GS] of the player's
  own PCSX2 (`C:\Program Files\PCSX2\inis\PCSX2.ini`, read only), the game's widescreen TV flag set as the port's option has
  it (`--write p0.1:D_0030AAC4:1b` in the run that dumps), its screenshot at the screen's size (`--fullscreen
  --screenshot-size 0`).
- ps2: the PS2's own picture. The port at 1x against PCSX2's software renderer at 1x.
- hw8: the hardware renderers at the port's scale. Both at 8x, the pictures drawn (4096 x 4096), the textures filtered as the
  game asks and 4:3 in both.

Aligned by the picture: the port's frames of each moment are drawn at 1x (4:3, and widescreen for the player's
configuration), and of all the place's PCSX2 pictures and the moment's frames the most alike pair (the whole picture's mean
difference at 192 pixels) is the moment's (where the cutscene falls in PCSX2's moments changes by up to a second from a run to
the next); the characters' crops' difference is the pose's likeness: a pair past 14 isn't valid (the page says so). The
port's frame is then drawn in each configuration ($TWIN_FIXED_TIME: the same frame every run; `$TWIN_DUMP_FRAMES` and
`$TWIN_WINDOW_SHOTS` take a list of frames, `FOLDER:N1,N2,...`). At a higher scale the port's picture lies half a PS2 pixel up
and left of PCSX2's (its samples start at the GS's sample point, PCSX2's at the PS2 pixels' centres: 4 pixels at 8x), a shift
of the whole picture that alone would make every outline a difference: PCSX2's picture is moved by the whole-pixel shift that
fits best before anything is measured, and the page says by how much.

For each moment and configuration the page has the two pictures at the same size, a flip (hover or click), the difference,
each character's crop enlarged with sharp pixels, the numbers (the mean difference, the pixels over 16 of 255, the outlines
matched within half a PS2 pixel, the steps of the PS2's pixel's size: edges exactly one PS2 pixel long on the PS2 pixels'
boundaries above the other grid positions', per 1000 PS2 pixels, 0 for a picture drawn at its own resolution; the textures'
sharpness) and every region the two differ in, largest first, with its crops. Then, at the GS's level:

- The same GS commands drawn by PCSX2 (player and hw8): the port's frame is also written as a PCSX2 GS dump
  (`$TWIN_GS_DUMP=FILE:N` or `FOLDER:N1,N2,...`, graphics/capture.cpp: the GS state as the frame before leaves it, local memory
  made current from the hardware renderer's targets, then every GIF packet of the three paths until the frame is made), played
  by PCSX2 with the configuration's settings (`run_pcsx2.py replay FILE.gs`): the game's work, VU1's and the commands being the
  port's in both pictures, whatever differs is the two renderers'.
- The port's GS commands against the retail game's (hw8): the retail game's GS dump the PCSX2 picture was drawn from
  (`run_pcsx2.py --gsdump T`, its hotkey GSDumpSingleFrame bound to F9 in the test copy), read with
  `native/tools/gsdump.py` (PCSX2 2.x's GS dump format: the header, the GS state of GSState::Freeze's version 9, the 8 KB of
  privileged registers, the transfers, vertical blanks and register packets; the GIF tags' PACKED, REGLIST and IMAGE loops
  decoded into primitives with their registers) and the two frames' draws grouped by how they're drawn (the primitive, the
  texture's format, the frame buffer, blending, FST, AA1, TEX1, ALPHA, TEST, the depth's mask; not the texture's address,
  as the texture cache's slots needn't be the same): the groups each has and the primitives each draws.

PCSX2's runs are kept in `..\compare\refs` (`--pcsx2-again` makes them again), the port's in `..\compare\runs\LABEL`; `--env K=V`
adds a variable to the port's runs. `check_windows.py --vs-pcsx2` runs the bench on the program checked (label `check`) and
passes a moment when its pair is valid, the port's PS2-sized steps are no more than PCSX2's (3 per 1000 over at most) and PCSX2
drawing the port's commands gives the port's picture (within 1.5 of 255 on average once the half pixel is fitted). It needs
numpy and Pillow in the .venv. `gsdump.py FILE.gs` sums a dump up; `gsdump.py compare PORT.gs RETAIL.gs` compares two.

`--scan` makes the page `scan` instead: Crash all round. On the beach the camera looks up at him (the beach's moment's input)
and is then turned round him by the right stick held for 18 s (`730-1630:rx=255`, PCSX2's `--key p3.02:rright:18.0`; a turn
takes about 150 frames at 50 Hz, six turns). PCSX2's GS dumps can't keep a time (each takes it a second and more of the
screen's time, the emulation slower meanwhile: asked a quarter of a second apart, they came late and after the stick was let
go), so twelve are asked a second apart, each a moment of its own, its pair the port's frame of the whole turning most like it:
the camera's angle, then the pose (his idles are the game's random ones: a pair can still differ by a pose, the same
commands' pair never does).

## Releasing

In a clone of the public repository with no changes of its own (a new script in native/tools aside):

    .venv\Scripts\python.exe native\tools\release_windows.py check [--sweep] [--no-linux]
    .venv\Scripts\python.exe native\tools\release_windows.py publish NOTES.md [--preview]

`check` pulls (and runs again when that brought a commit, so its checks are the pulled commit's), builds afresh
(`build_windows.py --configure`), packages (`package_windows.py`) and checks the zip, unpacked: the program's version against
native/CMakeLists.txt's; its company and copyright (Properties > Details) against native/windows.rc.in's (only our program has
them: the DLLs keep their authors'); nothing of a player's (portable.txt, a User folder, a disc image); the DLLs it imports
(Windows', SDL's and FFmpeg's only); its files against the last release's;
check_windows.py's checks, with the pictures compared with the last release's program; and that the program has no 32-byte block
of the game executable's data or VU microcode the last release's didn't (version 1.0.5 has 23 of the 3,832 that aren't
repetitive, none of the microcode: runs of small integers, the standard library's tables, powers of ten and digits, and file
names the decompilation's C++ spells out). Then, since 1.2.0, the Linux AppImage (native/LINUX.md, "In Docker"; Docker
Desktop has to be running): built afresh in Docker (`build_linux_docker.py --configure`) and checked there
(`check_linux.py`: it holds SDL3 and FFmpeg's two libraries and loads nothing else but what every Linux has, the glibc it needs,
its licences (the MIT code's in the program too), its version, and the beach played from it for 20 seconds, headless on Xvfb
with Mesa, the disc image mounted read-only), its
program searched for the game's blocks as the zip's is (none the Windows program of the same commit or the last release's
hasn't). Its lines start with `linux`; `--no-linux` releases the zip alone, when Docker isn't there. Since 1.4.0 the zip and
the AppImage are then scanned by Microsoft Defender (`MpCmdRun.exe -Scan -ScanType 3 -File FILE -DisableRemediation`: a zip's
files looked into, a detection listed and nothing quarantined; exit code 0 for none, 2 for a threat), its line naming the
security intelligence's version, for the README's "Is it safe?". It prints the commit and both files' SHA-256. `publish` makes the release (`vVERSION`; with `--preview` a preview, GitHub's pre-release, as every
release before 1.0.8 was) from NOTES.md with `{commit}`, `{sha256}` (the zip's) and `{sha256_linux}` (the AppImage's) filled
in, opens its discussion (Discussions > Announcements, where players can talk about it), uploads the zip and the AppImage,
downloads them back and compares their SHA-256, and beside each uploads `NAME.sha256` (sha256sum's line: the hash, two
spaces, the name; `sha256sum -c` checks a download in the same folder), downloaded back and compared too: only with the
commit and the files `check` checked, once that commit is on GitHub. GitHub's token is git's (`git credential fill`), never printed or kept.

No commit has the game's files: `native/tools/check_game_data.py` refuses the executable, the disc's and the archive's files,
disc images, splat's output, texture packs and PCSX2's states, cards and BIOS, by name, folder, first bytes or size. The
hook `.githooks/pre-commit` runs it on the files a commit adds or changes, before the commit is made (turned on once per clone
with `git config core.hooksPath .githooks`); the workflow `.github/workflows/game-data.yml` runs it on every push and pull
request, its action pinned by commit and kept up to date by Dependabot (`.github/dependabot.yml`); `release_windows.py check`
runs it on the clone first and stops if it finds one.

`package_windows.py` makes `build/native/dist/Crash Twinsanity/` and `Crash-Twinsanity-Windows.zip`: the program, the
DLLs, the fonts, the licences and native/PLAYING_WINDOWS.md as README.txt (a player's own files there, portable mode's and disc
images, are left alone and never zipped: "Portable mode", above). The program's icon is native/icon/AppIcon.ico
(16 to 256 pixels), our own drawing: two spiral eyes on an orange tile, drawn from shapes alone by `native/tools/draw_icon.py`
(`--preview sheet.png` shows every size; under 64 pixels the spirals become pupils). CMake rebuilds the resource when the icon
changes. `native/tools/make_icon.py` still makes one from the HUD's Crash head for the Mac build.

The zip has none of the game's files or data: the program reads the retail executable's data from the player's disc image
when it starts ("The game's data from the disc", above). It has the decompilation's C++ and the VU programs' translation,
compiled.
