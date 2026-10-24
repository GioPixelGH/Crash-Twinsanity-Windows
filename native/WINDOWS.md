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
| `Gs::DrawPrimitive`'s state copy, the batch key (gsdraw.cpp, hwgs.cpp) | Copied inline (the C library's memcpy was 5% of the thread); the key copied only when it changes | - |
| The pixels' addresses (gs/gsaddress.h) | Inline, in a header of their own (the transfers worked one out per pixel through a call into gsmemory.cpp) | - |
| The compiler's target | x86-64-v2 (SSE4.2, POPCNT: what Windows 11 asks of a processor): floor and ceil inline; no FMA, so the same results | - |
| `HwGs::DecodeOnGpu` (hwgs.cpp) | A texture looked up again only when a register was written or the CLUT changed (`Gs::stateGeneration`); a complete one checked a page at a time against the pages' changes rather than a tile at a time | `$TWIN_HW_NO_TEXTURE_GENERATION`, `$TWIN_HW_NO_PAGE_CHECK` |
| `HwGs::Draw` (hwgs.cpp) | A mesh's next primitive drawn as the last one was while nothing it depended on changed (`DrawLikeLast`: the GS's state, the CLUT, the targets, the settings, the texture good as it was): no key, targets or texture worked out again, only its pages made current and its vertexes added. A quarter of the render thread's time | `$TWIN_HW_NO_FAST_DRAW` |
| `WaitForVBlank` (display.cpp) | `SDL_DelayPrecise` | - |

ThinLTO over the emulation's files (`-DTWIN_EMULATION_LTO=ON`) was measured no faster, and is off.

The threads: VIF1 and VU1 run on a thread of their own between the game's and the render thread (renderthread.h, three stages,
the default): the game's thread queues VIF1's words, the GIF's transfers and the GS's work in their order, the VU stage runs
VIF1 and VU1 and hands it all on to the render thread's queue, so the GS gets the same data in the same order. Where the scenes
are big the game's thread went from about 17.5 ms a frame to 9 (VU1 was half of it): the Totem's beach, the school's boiler room
and rooftops went from 57 to 60 frames a second. `$TWIN_RENDER_THREAD=gs` puts VIF1 and VU1 back on the game's thread.
`$TWIN_FRAME_STATS`' `game` is the game's thread's work between its waits for the vertical blank.

Comparing two builds' pictures: `$TWIN_FIXED_TIME=1` with `$TWINSANITY_AUDIO=frames` (the sound made in step with the frames)
gives the same frames run after run (with the sound in real time, a run's speed changes what the game does with it after a
few hundred frames).

## Languages

The PAL game has five: English, French, German, Italian and Spanish, each with its texts (the archive's Language folders), its
voices (CRASH6\<LANGUAGE>.MB and .MH, loaded once at start-up) and its sound in the story's movies (a channel each). The PS2
took the language from its own settings once, at start-up; the native build takes Windows' (system.cpp's
`Platform::System::Language`), or the one Options > Gameplay > language names (options.txt's `language`, 1-5), from the game's
next start.

## Mods

A file in the settings folder's `mods` folder (`%APPDATA%\Crash Twinsanity\mods`) at the path of one of the game's files is read
instead of it, in any case, whether it's one of the disc's (`mods\FMV\H01_A.PSS`, the music's banks) or one of its archive's
(CRASH6\CRASH.BD: `mods\Levels\Earth\Hub\beach.rm2`, `mods\Language\AgentLab\English.txt`, the fonts, the startup
files...). The log says each one used (`[native] mods: ...`). `CrashTwinsanity.exe --list-files` lists the
archive's files and `--extract=PATH` copies one (or one of the disc's) into the mods folder to be changed (commands.cpp);
`native/tools/iso_extract.py IMAGE [--archive] --list` and `iso_extract.py IMAGE [--archive] PATH OUT` do the same without the
game. The disc.cpp side: a
disc file's mod is given a place past 16 GB (a GB each, its sectors still 32 bit for the I/O processor), an archive file's one
past the image's end within 4 GB of the archive's start (filestream.cpp's `FileStreamOpen` takes its start and size), and
`Native::Disc::Read` reads them from the files. The game's own code does what it would with the original (a file it doesn't
ask for can't be added: the archive's table is the game's list).

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
  (a dialog over the settings, the controller's or the mouse's), refused at once when the folder can't be written to, then
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
Academy's hub), each failing on a crash and on the log's signs of memory gone wrong (the disk manager's pool out of room, DMA
reading memory that isn't there); the pause menu's hints (its `HINTS`: the game's own with a PlayStation pad's, an Xbox pad's,
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
(`build/native/dist/`, or `--reference PROGRAM`); with `--sweep`, every level chunk, its outcome
against `native/sweep/results_windows.json` (a run that went on and one that ended in a game over count alike: the scripted
input decides which). Some 5 minutes, 25 more with the sweep. Each run has a settings folder of its own; the disc image is
local.json's. `--hints-shots FOLDER` runs only the hints' check, with a window, and saves the pause menu's pictures there;
`--physics` only the physics' (a minute or so); `--portable` only portable mode's (under a minute; the dialog's pictures in
`build/native/check/portable/shots/`); `--fps-shots FOLDER` only the FPS counter's (its `FPS_CASES`, `$TWIN_FPS_TEST`,
below: at 60 Hz, 50 Hz, 144 Hz without v-sync, and off; a minute or so), with a window, as the counter counts the pictures shown
(the self-test checks its sums and its place without one), and saves the window's pictures there.

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
  picture as a PNG), two builds' pictures can be compared frame by frame; with `$TWINSANITY_AUDIO=frames` too, runs agree
  frame for frame (the sound in real time changes a run after some 700 frames). The pictures are at the render scale: with no
  `resolution.txt` in the run's settings, the default 2x (4x before version 1.0.4), so compare builds at the same scale.
  `$TWIN_FIXED_TIME=144,144,60`: the frames' rates in turn instead (a 144 Hz screen's with a slow frame now and then: the uneven
  frames past 60 Hz, the same every run).
- `$TWIN_FRAME_STATS`: each frame's time, VU1's work, the primitives, the render thread's time and the GPU renderer's batches, texture decodes, uploads, downloads and copies.
- The log: each line `[native] [SECONDS]` from the start. The first names the version (`Crash Twinsanity 1.0.12`, from
  native/CMakeLists.txt: only entry.cpp is compiled with it). Once the window is there it names the graphics card and its driver:
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
  front of the link's load wall, where the link takes it). Other steps: `goto:X:Y:Z` puts the player there; `objects` logs the
  instances of its chunk (their objects' names and places); `on:mine` puts it 2 units above the nearest instance whose
  object's name holds "mine" (`on:mine:5`: 5 units) and `bring:mine` puts that instance where the player is instead; `idle`
  stops the scripted input and `input` starts it again; `wait5` waits 5 s; `die` drops it to its death; `scripts:director`
  logs the behaviour scripts of the instances whose object's name holds "director" (each runner's graphs, the state they're in,
  every state's bodies: condition, commands, jump) and `state:GRAPH:3` makes the runner that runs a graph whose name holds GRAPH
  jump to its state 3, as a body's jump would (a boss beaten without playing it: check_windows.py's Dingodile). Down a slide (Slip
  Slide Icecapades) the player's physics undo `goto` and a hop stops the slide: start at the level's start, let the scripted
  input get through its cutscene, then `idle` lets the slide go on by itself and `bring` puts things in its way.
  `$TWIN_TRIGGER_PLAN=list` lists the message triggers of the player's chunk (index, place, box, the clock units between two
  checks of its box and whether it's polled at all), then its camera triggers (the same, and the kinds of nodes their events go
  to; not steps of a plan); a list of steps (`4,die,4`)
  puts the player in a trigger or drops it to its death every 8 s, the lives kept up. `$TWIN_SWEEP_IDLE` gives no scripted
  input (cutscenes play out). `$TWIN_TEST_PICK=PATH` is the disc image dialog's pick (empty: cancelled).
  `$TWIN_SWEEP_SHOTS=FOLDER:SECONDS` (with a window, not `--headless`) saves the window every SECONDS of play as it's shown
  (`shot_000.png` on): the README's pictures came from it, a run's `resolution.txt` saying `scale=4`, `window=1600x900` and
  `fullscreen=0`.
- `$TWIN_SKIP_TEST=SECONDS` (ui/features.cpp; the skip option on in the run's options.txt, `skip_cutscenes=1`): in every scene
  that can be skipped a button pressed SECONDS after the scene's first poll and held for the hold's time (1 s) and half a second
  more; each skip logged with the scene's polls and the longest gap between two, each scene that wasn't skipped as the next
  begins. A level start plays none (the player starts in play): with `$TWIN_SWEEP_IDLE` and a trigger plan (`0,1,2,...`) the
  triggers start them. The hold is lost when the polls stop for more than 0.25 s (a new scene), so a gap near that is the thing
  to look for.
- `$TWIN_HINTS_TEST=1` (with `--quick`, ui/hintstest.cpp): the beach played a few seconds, then the pause menu: its hints
  checked as the menu hints option has them and logged as the prompts label them (`$TWIN_UI_PROMPTS=playstation|xbox|switch|
  keyboard` picks the controller), then the mouse clicks the pages' right and left arrows and back; "hints test: pass" and exit 0.
  `$TWIN_HINTS_TEST=FOLDER` without `--headless` also saves the window as it's shown (the picture and the overlay:
  `NativeGraphicsShootWindow`) there: hints_pause.png and hints_levels.png.
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
  Gameplay's portable mode row stepped and its dialog followed with its own input: asked, select copies, done, select closes the
  game as a player's would (exit 0; the folders are check_windows.py's to look at); refused (the folder can't be written to),
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

## Releasing

In a clone of the public repository with no changes of its own (a new script in native/tools aside):

    .venv\Scripts\python.exe native\tools\release_windows.py check [--sweep]
    .venv\Scripts\python.exe native\tools\release_windows.py publish NOTES.md [--preview]

`check` pulls (and runs again when that brought a commit, so its checks are the pulled commit's), builds afresh
(`build_windows.py --configure`), packages (`package_windows.py`) and checks the zip, unpacked: the program's version against
native/CMakeLists.txt's; its company and copyright (Properties > Details) against native/windows.rc.in's (only our program has
them: the DLLs keep their authors'); nothing of a player's (portable.txt, a User folder, a disc image); the DLLs it imports
(Windows', SDL's and FFmpeg's only); its files against the last release's;
check_windows.py's checks, with the pictures compared with the last release's program; and that the program has no 32-byte block
of the game executable's data or VU microcode the last release's didn't (version 1.0.5 has 23 of the 3,832 that aren't
repetitive, none of the microcode: runs of small integers, the standard library's tables, powers of ten and digits, and file
names the decompilation's C++ spells out). It prints the commit and the zip's SHA-256. `publish` makes the release (`vVERSION`;
with `--preview` a preview, GitHub's pre-release, as every release before 1.0.8 was) from NOTES.md with `{commit}` and
`{sha256}` filled in, uploads the zip, downloads it back and compares its SHA-256: only with the commit and the zip `check`
checked, once that commit is on GitHub. GitHub's token is git's (`git credential fill`), never printed or kept.

`package_windows.py` makes `build/native/dist/Crash Twinsanity/` and `Crash-Twinsanity-Windows.zip`: the program, the
DLLs, the fonts, the licences and native/PLAYING_WINDOWS.md as README.txt (a player's own files there, portable mode's and disc
images, are left alone and never zipped: "Portable mode", above). The program's icon is native/icon/AppIcon.ico
(16 to 256 pixels), our own drawing: two spiral eyes on an orange tile, drawn from shapes alone by `native/tools/draw_icon.py`
(`--preview sheet.png` shows every size; under 64 pixels the spirals become pupils). CMake rebuilds the resource when the icon
changes. `native/tools/make_icon.py` still makes one from the HUD's Crash head for the Mac build.

The zip has none of the game's files or data: the program reads the retail executable's data from the player's disc image
when it starts ("The game's data from the disc", above). It has the decompilation's C++ and the VU programs' translation,
compiled.
