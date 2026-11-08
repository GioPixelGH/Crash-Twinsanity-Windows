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

Comparing two builds' pictures: `$TWIN_FIXED_TIME=1` with `$TWINSANITY_AUDIO=frames` (the sound made in step with the frames)
gives the same frames run after run (with the sound in real time, a run's speed changes what the game does with it after a
few hundred frames).

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
`Platform::System::Language`), or the one Options > Gameplay > language names (options.txt's `language`, 1-5), from the game's
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
settings' sixth tab, settings.cpp's BuildMods): Import mods while there's something to; each mod a choice, off or on in its
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
- **Options > Gameplay > Portable mode** (yes/no, ui/settings.cpp; shown only on Windows) is the mode in use. Stepping it asks
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
`Native::LoadRetailData` (system.cpp) reads `SLES_525.68` from the player's disc image and `NativeLoadRetailData` (data.cpp) lays
it out.

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
hand on the PC's mouse turned the SMAA run's camera in 1.2.0's first release check); the look and hints tests script the
mouse headless. `--hints-shots FOLDER` runs only the hints' check, with a window, and saves the pause menu's pictures there;
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
variable.

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
is slow, in OpenGL; one whose processor is slow, in the GS.

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
with SSAA 8357, and the card's time was unknown for the rest of the session (1.1.0). Since 1.2.0 a query whose result isn't
there 120 frames after it was begun, while the next one's is, is left out (`graphics: a timer query's result not there N
frames after it was begun, the next one's there: left out`, once a thread), and none is begun on a frame its thread ends with
another context current (its results couldn't be read). The cause wasn't reproduced here (a query whose begin the driver
refused is the likeliest: its result never comes); `$TWIN_GPU_TIME_TEST=stuck` makes each thread's first query one such, and
the log's check has it. Measured on the beach in
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
  select, up, down, left, right), each span logged as it starts. The beach's camera looking up at Crash and Aku Aku against the
  sky: `620-700:ry=255`; Crash walked into the first boss's arena (its cutscene from about frame 690): `TWIN_START_CHUNK=
  Levels\Earth\Hub\bossarea` with `500-800:ly=0`. With the pictures above, a real run (a window or fullscreen, three threads) is
  the same place every time: a bug seen there is measured in the window as shown and in the picture drawn at once.
- `$TWIN_FRAME_STATS`: each frame's time, VU1's work, the primitives, the render thread's time and the GPU renderer's batches, texture decodes, uploads, downloads and copies.
- `$TWIN_HW_SOURCE_TRACE=1`: each kind of texture the hardware renderer samples straight from a target (a buffer it drew)
  rather than decoding it from memory, logged once ("graphics: a 256x256 texture at 2800/4 (psm 0) read from the target ...");
  `$TWIN_HW_NO_TARGET_TEXTURES=1`: none, every texture decoded from memory's copy (an effect's buffer then at the PS2's
  resolution): to tell that way's drawing from the decoder's (GRAPHICS.md, "Ant Agony's walls and doors").
- `$TWIN_RENDER_DISTANCE=far`: the draw distance's far plane its choice's even where the colour filter reads the depth
  (renderdistance.h, NATIVE.md's "Draw distance"): to see what it would change. The log's "render distance: the far plane Nx"
  lines say when the far plane moves.
- `$TWIN_GPU_TIME=0`: the graphics card's time not measured (the log's `the graphics card unknown`, the entry points
  OpenGL's own): to measure what the timing costs. `$TWIN_SETTINGS_LOG_TEST=1` (headless): the settings screen driven from the
  main menu through the log check's changes, then the program ends (settingsshots.cpp, beside `$TWIN_UI_SETTINGS_SHOTS`).
- The log (what a report needs is under "The log", above): each line `[native] [SECONDS]` from the start, the first the
  version's. Once the window is there it names the graphics card and its driver:
  OpenGL's renderer, vendor and version (the driver's own number in it: NVIDIA's "3.3.0 NVIDIA 617.14", AMD's "... Core Profile
  Context 24.10.1...", Intel's "... Build 31.0.101..."), then each adapter Windows has (graphics/adapters_win32.cpp, DXGI's: its
  name, PCI vendor and device, its driver's version as Device Manager shows it and its video memory). The game's trail (ui/trail.cpp) is always in it: the game controller's
  states by name with the player's place (read without changing the game's), the player's chunk as it changes, the chunk a level
  loads at, the memory card's writes and the movies opened. The detailed log (Options > Gameplay, `detailed_log` in
  options.txt, or `$TWIN_DETAILED_LOG`) adds the player's place every 5 s of play, every sound played (game/sound.cpp, as
  `$TWIN_SOUND_CHECK` logs them) and every file looked up on the disc. A crash's or a hang's stack names each function of the
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
  object's name holds "mine" (`on:mine:5`: 5 units) and `bring:mine` puts that instance where the player is instead; `idle`
  stops the scripted input and `input` starts it again; `wait5` waits 5 s; `die` drops it to its death; `mask` gives it a mask as an Aku Aku crate does (the fourth in a row makes it invincible); with `$TWIN_SWEEP_GAME_TIME=1` the steps' waits and flights go by the game clock instead of real time, so a run with `$TWIN_FIXED_TIME=1` and `$TWINSANITY_AUDIO=frames` makes the same pictures every time (two builds', or two switches', compared frame by frame); `$TWIN_NO_SHADOWS` leaves the characters' shadows out, to compare what they darken; `scripts:director`
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
  `native/tools/check_cutscene_skips.py [NAME...] [--waits]` plays each cutscene of its list watched and skipped (and skipped by
  the waits) from the same start and compares what each leaves; its runs' folders and settings are absolute paths, and each run
  is given the disc image (`$TWIN_TEST_PICK`), so a missed settings folder can't open the disc image dialog.
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
  `NativeGraphicsShootWindow`) there: hints_pause.png and hints_levels.png.
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
  as for Crash and Cortex tied, who have no slide: tunnel01 with `$TWIN_PHYSICS_TIED`, way 0), each
  logged with its time in the air, its distance along the ground and its height. `watch`, or `step[:SECONDS]` (the stick pushed
  `$TWIN_PHYSICS_WAY`'s way first): every time the player leaves the ground, as it lands; with the level sweep to start a chunk
  (`$TWIN_SWEEP_LEVEL`, `$TWIN_SWEEP_IDLE`), `$TWIN_PHYSICS_ON=NAME` (the player put on an instance 2 s in: `cavern_lift17`),
  `$TWIN_PHYSICS_TIED` (Cortex tied to it) and `$TWIN_PHYSICS_SECONDS` (the watch's end). `$TWIN_PHYSICS_TRACE=FILE`: every
  frame's place, velocity, ground and movement node's velocity as CSV.
- `$TWIN_PORTABLE_TEST=1` or `=FOLDER` (with `--headless`, ui/portabletest.cpp): from the main menu the settings opened,
  Gameplay's portable mode row stepped and its dialog followed with its own input: asked, left to yes and select copies,
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
hasn't). Its lines start with `linux`; `--no-linux` releases the zip alone, when Docker isn't there. It prints the commit and
both files' SHA-256. `publish` makes the release (`vVERSION`; with `--preview` a preview, GitHub's pre-release, as every
release before 1.0.8 was) from NOTES.md with `{commit}`, `{sha256}` (the zip's) and `{sha256_linux}` (the AppImage's) filled
in, opens its discussion (Discussions > Announcements, where players can talk about it), uploads the zip and the AppImage,
downloads them back and compares their SHA-256: only with the commit and the files `check` checked, once that commit is on
GitHub. GitHub's token is git's (`git credential fill`), never printed or kept.

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
