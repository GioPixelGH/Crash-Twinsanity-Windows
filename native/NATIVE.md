# The native build (macOS, Linux)

The game's C++ built for the host (Apple silicon or x86-64 macOS, Linux) instead of the PS2, with a platform layer of its own in
`src/platform/native/`. `TWIN_NATIVE` is defined for it. Every change the native build needs in the shared code is under
`TWIN_NATIVE` or uses a type that is the PS2's own on the PS2 (`uiptr`, `siptr`: PS2SDK's pointer-sized integers, 32 bit there),
so the PS2 build stays the same executable: check it with `tools/build.py` and a comparison with the last PS2 build.

## How the native build differs from the PS2's

The aim is the retail game's behaviour exactly. Where the host can't do what the PS2 did, the difference is listed here: nothing
is approximated without an entry.

| Where | The PS2 | The native build | Effect |
|---|---|---|---|
| Struct layouts | 32 bit pointers, the retail offsets (`CHECK_SIZE`, `CHECK_OFFSET`, `CHECK_LAYOUT`) | 64 bit pointers: structs holding pointers are bigger, the checks are off | None on behaviour; data the game reads from files isn't laid over pointer-holding structs |
| The retail code's null pointer reads and writes (docs/RETAIL_BUGS.md, "Low addresses"; several reached in normal play) | The kernel's RAM is at 0: reads return it, writes change it | `src/platform/native/lowmemory.cpp`: a load or store faulting below 1 MB is decoded (arm64) and done on `g_NativeLowMemory`, then the game goes on. Jumps through null crash as on the PS2. `--selftest-lowmemory` checks it | The values read are zeros unless `build/native/ps2_low_memory.bin` (a dump of the PS2's first MB, from PCSX2) is there. x86-64 macOS and Linux do it by running the faulting instruction again with its base register moved into the buffer under the processor's trap flag (`lowmemory.cpp`); Windows by `lowmemory_win32.cpp` |
| `AgentLabPacket` slot reads with no data (`include/game/agentlab.h`) | Reads the byte at the slot's index as an address: the kernel's memory | Reads `g_NativeLowMemory` directly | As above |
| `MemoryAllocateAligned` / `FreeHeap` (`src/game/memory.cpp`) | The word in front of an aligned allocation holds the block's address | It holds the distance back to the block | None (the address doesn't fit 32 bits) |
| `CompareHandles` (`src/game/triggernodes.cpp`) | The difference of two addresses | Their order (-1, 0, 1) | None: only the sign is used |
| Objects' frame spread (`ObjectNode`, `GameOGI`: `(address >> 8) & mask`) | The heap address's bits | The host address's low 32 bits | Which frame an object's periodic work falls on differs; how often it happens doesn't |
| Pointers as 32 bit values (`RunAgentEvent`'s originator, collision hit faces, `ResourceIdList::countOrMore`, `CharacterAgent::standingStamp`, the second pad) | `u32` | `uiptr` (64 bit) | None |
| `CopyQuadword` (`src/game/eventcallers.cpp`) | LQ/SQ, the addresses' low 4 bits dropped | A 16 byte copy at the addresses with their low 4 bits dropped | None |
| Small allocations (`SmallAllocate`, `src/game/memory.cpp`) | Sizes rounded up to 4 bytes: entries of 4, 8, 12... | Rounded up to 8: a free entry holds a 64 bit link | Which size class an allocation lands in; nothing the game sees |
| Objects allocated by a fixed size (`GameChunkManagerSize`, `ObjectPlaceSize`, `ProjectileNodeSize`, the shadows' model and mesh, `UnusedBlockSize`) | Their PS2 size | `NativeObjectBytes`: twice it (only pointers grow, so nothing can need more) | More memory used |
| The pools' space (`Platform::Memory::PoolSpace`) | 32 MB less the executable and the C library's reserve (~28 MB) | Four times that (twice ran out loading a level) | The game's heap runs out later than on the PS2 |
| The disk manager's pool (`src/game/memory.cpp`: the chunks' models, textures, skins and animations) | 0x10A3D70 bytes (16.6 MB) | Twice that; the heap manager's pool stays what it was | More memory used. The chunks a link brings in are read and taken apart faster than the PS2's drive and processor could, before the game lets go of the ones left behind (4 s after it stops wanting them): from Dingodile's room into the boiler room's lock that needed 17.1 MB at once, and a full pool gives the reader another block's handle, whose data is then overwritten (the textures drawn wrong, then a crash) |
| The C library's `malloc`/`free` (`RetailLibc::Malloc`) | Sony's dlmalloc on sbrk | The host's (16 byte aligned) | None: the game's own heap manager works inside its pools as on the PS2 |
| The console's language | The PS2's OSD setting | The machine's preferred languages (the first the game has, else English), or Options > Gameplay > Text language. Since 1.2.0 the voices' language is a setting of its own (Voice language, `speech_language`: as the texts by default, or English, French, German, Italian, Spanish): `SpeechLanguage()` (include/game/language.h; languages.cpp's `NativeSpeechLanguage`, read once at the start) picks the voice bank (CRASH6\<language>.MB), the levels' voices (their code sections' voice subsection, `CodeItemGetReader`, `VoicesOf`, `SoundById`) and the movies' sound channel; the texts, the title pictures and the credits stay the text language's. A text bank's language is recognised by what it says (TwinsanityEvolved's table, `languageprobes.h`, MIT): a translated disc that keeps Portuguese in the English slot (#25) shows "português" for that choice, and the log says `languages: ... the English texts are in portuguese` | The language the game starts in follows the computer; texts and voices can differ (Portuguese texts with English voices, English texts with French voices) |
| Two `u32` globals splat ended in a pad (`G_DynamicSceneryClockIndex`, `G_SizeOfByteArrayAt_0x3D3F28_0x18`) | Unreferenced bytes followed them | The bytes after them aren't brought over | None: nothing reads past a single `u32` |
| `ProjectileObjectNode` (`src/game/projectiles.cpp`) | Its members overlay the node base's runners, frame start and frame move (the shooting commands write its state through `frameMove`) | The base's part is whole and the projectile's members follow it; the commands find them through `NativeProjectileState` | None seen: the prototype's `Reset` wrote the overlaid fields before `Construct` set them all |
| `PickupObjectNode` (`include/game/pickups.h`), and the stand-in's `PickupNodeTimes` (`src/game/instancefactory.cpp`) | Its members (bits, trails, body, spin phase, velocity, wait start) overlay the node base's runners, frame start and frame move | The base's part is whole and the pickup's members follow it, as the projectile's; the stand-in writes the wait start through the pickup's own class | None seen: the prototype's construction wrote the overlaid fields before `Initialise` set them. With the base's part at the PS2's size natively, those writes landed on the spin phase and the beach crashed reading its spin matrix |
| `g_SaveBuffer` (`src/game/savemanager.cpp`) | A bank file's 0xF400 bytes are read and written from `D_0030BF28`, whose own label is 0x1948 bytes: the save runs over the BSS after it (`D_0030D870` and on, other code's scratch) | A buffer of the bank's size of its own | The native data doesn't put that BSS behind it: saving ran over the heap manager and crashed |
| `GameContext::instanceFactory` | 0x28 bytes for the factory made in place | `NativeObjectBytes` of it | None |
| Saves | Memory cards in two ports (libmc), checked at boot and polled while autosave is on | The game's own save manager (`src/platform/ps2/saves.cpp`) on `src/platform/native/memorycard.cpp`: port 1's card always in and formatted (8 MB), its files a folder in the app's settings (`~/Library/Application Support/Crash Twinsanity/memorycard`), port 2 empty; formatting never wipes it; the space its files take is counted once and again after a file is made or written, a directory made or an entry deleted (the save manager asks for it every frame it's idle: walking the folder each time cost the game's thread 0.2-0.4 ms a frame). The flow is a computer's, after the Xbox version's console-storage one (native/XBOX_SAVES.md, section 3), under `TWIN_NATIVE` in the game's code: no check at boot (`gamecontroller.cpp`, N1), no polling while autosave is on (N2), "not enough space" is the NoRoom screen (N4, N5: also when the save can't be made), back/cancel ends a save at once with no "cancel save?" (N6); and, the native build's own: no "create a save file?" question (a save is made at once and the slots shown), NoRoom offers "continue without saving" (new game) / "cancel" / "retry" (retry measures again), a load from a save with no game in its slots says "no save data". Kept from the PS2 on purpose: retry after a failed save measures again (the Xbox's made the save afresh, wiping the other slots, N11), the 2 s each message stays (the Xbox's 3 s is its certification rule), the save's layout (N14), no Dashboard item (N10), the size in KB. Every message is the PC's wording (native/PC_TEXT.md). `--selftest-saves` checks a save's round trip, `--selftest-ui` the flow's changes on the game's save code | The card can't be pulled out; no screen mentions a card |
| The console's texts | The language files' lines about the PlayStation 2, its memory cards, slots, controller ports and DUALSHOCK 2 | Those lines (140: 28 in each of the five languages) replaced with a computer's wording when the game splits a text file into lines (`ReadTextFile`, `src/platform/native/ui/pctext.cpp`); every original and replacement is in native/PC_TEXT.md. `--selftest-ui` checks them against the disc, `--dump-texts` prints every line as the native build has them | The texts say "computer", "controller", "save data" |
| The graphic options | Centre screen, widescreen | Also "anti-aliasing" (off, FXAA, SMAA, SSAA 2x and 4x: SMAA is iryoku's SMAA 1x, MIT, at the high preset, run on the picture at its own size before the window shows it, `graphics/smaa.cpp`, `anti_aliasing=4`), "anisotropic filtering" (off, the default, then 2x to 16x: the game's bilinear mipmapped textures whose level of detail goes by the distance sampled with `textureGrad`, the gradients scaled to the GS's level, so the card's anisotropy samples along the texture's long axis at the GS's level; the GS's own `textureLod` gives the card no gradients to be anisotropic with, so 1.1.0's unused filtering mode 2 asked for 16x to no effect; `hwgs.cpp`'s ANISO, `anisotropic_filtering`; 0.15-0.4% of the beach's pixels change at 16x, the far trees' and statues' textures: the sand isn't mipmapped by the game), "dithering" (as the PS2, the default: the frame copied into a 16 bit picture with the GS's 4x4 dither, 5 bits a channel, src/platform/ps2/renderer/frame.cpp; or off: the hardware renderer keeps that copy's full colour, about 4 of 255 brighter as the truncation's floor goes; at 1x as the PS2 the beach measured within 0.6 of 255 of PCSX2's defaults), "resolution" (the render scale, 1x, 2x, 3x, 4x, 6x or 8x the PS2's picture) and "display" (a window of 960x720, 1280x960, 1600x1200, 1920x1440, 1280x720 or 1920x1080, or fullscreen at the display's size; the picture keeps the TV's shape, letterboxed). 3x and up say they're slower, 6x and 8x that they need a strong graphics card (the software rasteriser, which stops at 4x: about 5 times 1x's drawing time at 2x, 13 at 4x): the game's own choice items, their texts past the code file's lines (0x100 on, native/PC_TEXT.md). A choice is applied once it settles (since 1.2.0, THE LIST 41: the settings screen's Resolution row applies the scale stepped to when the row is left, the screen closed, or 0.75 s after the last step, not at each step, as stepping left from 1x wraps round to 8x: #39's 451 MB card drew 8x and back; `PendingScale`, settings.cpp) (`NativeGraphicsSetResolution`, `graphics/resolution.h`) and kept in the app's settings (`resolution.txt`), applied again at start-up (`src/main.cpp`; not with `--headless`, so tests keep the 1x picture); with none saved, the defaults: 2x in a 1280x720 window (`DefaultResolution`, also the Display tab's reset; the window is made at that size). `src/platform/native/ui/resolution.cpp` | More items on the page |
| Draw distance | Models past their level of detail's farthest distance aren't drawn (grass and small things pop in as the player nears); the camera lens's far plane at 1500 | The draw distance option (Display tab; HAB3X's 17abe53, `renderdistance.h`): short, original (the default), medium, long or unlimited, `render_distance` in options.txt as codes (0 original, 1 short, 2 medium, 3 long, 4 unlimited) the menu orders from the shortest. The level of detail is chosen as if the camera were 0.6, 1, 2, 3 or (unlimited) 4 times nearer (`LodMeshAt`, `LodDistance`): the switches to its simpler meshes, its nearest and its farthest distances move out together (the data keeps them squared, so the squared distance is divided by the square; HAB3X's scaled only the farthest, its square by the factor), and unlimited has no cut-off. The far plane takes the same scale (`RenderView::SetProjection`, `SetViewFrustum`), only while no screen effect reads the depth (`ScreenEffectsReadDepth`, renderer/effects.cpp): it moves every pixel's depth by a constant (about +840 of the depth's 2^24 at 4x), and the colour filter colours the pixels past 25 units by their depth's bits 8-15 through the chunk's palette: at 4x up to 2/128 more or less of its colour in the middle distance, 12/128 in the last fifth before 1500 (the beach's frame with it forced, `$TWIN_RENDER_DISTANCE=far`: 177,316 pixels of the upper half a 5-bit step apart, 2 of the lower). Every palette but palette 6 (of 0-7; all its alphas 0x80: no change) reads the depth, and the filter is on in play, so the far plane mostly stays the game's (the log's "render distance: the far plane Nx" says when it moves). The projection is made again when that scale changes (the game makes it only when the lens changes) | More is drawn: on the beach unlimited 131,811 primitives a frame against 65,142 original (+102%: the detailed meshes four times as far and no cut-off; the cut-off alone was +46%, as HAB3X measured, 94,000 against 65,000), heavier on slow PCs (#23, #12); short draws less |
| Quitting | The console's power or reset button | The window's close button, Alt+F4 (Cmd+Q), or Options > Gameplay > Quit to desktop (#33, 1.2.0: a yes/no question first, no chosen first; yes logs the settings changed and ends the session as the window's close does, `quit: to the desktop from the options`). The same page's Credits shows the README's credits in a dialog (settingsview.cpp's `Credits`) | None: the saves and settings are written as they're made |
| The knee slide's steering | The PAL disc's 50 Hz frames (180 clock units) end the slide's steering (1350 units) on half a frame | Since 1.2.0 (THE LIST 36) the steering's last frame turns over at least half a frame's time left (`CrouchController::Slide`): a frame ending a sliver before the steering does (an uneven frame's advance: TwinsanityEvolved 0.9.3's 60 Hz frames are 149 units, its last steering frame 9 units short of the end, a rate many times the angle and the slide swung far past the stick's direction) turns as a 50 Hz frame would. Measured here first (`$TWIN_SLIDE_TRACE`: each steering frame and the facing's angle from the slide's direction as it ends), the beach, a slide steered toward the stick held to the side: at a fixed 60 Hz our frames are whole (150 units: the last has a whole frame left) and the slide ends 28.9° short of its direction where 50 Hz ends 18.5° short (the same input, half a frame left), no swing past it; one slide in real time (a window): 0.025 s left, 33.5° short, none either. So the change only guards against a sliver; `$TWIN_SLIDE_STEERING=retail` keeps the retail turn | None at 50 Hz or on whole 60 Hz frames |
| The network | None | None but the update check (since 1.2.0, THE LIST 9b; `updatecheck.cpp`, Windows only, `update_check`, OFF by default): turned on, each start asks GitHub once, on a thread of its own, for the port's latest release (an HTTPS GET of api.github.com/repos/GioPixelGH/Crash-Twinsanity-Windows/releases/latest through WinHTTP: no cookies, no credentials, a User-Agent of the program's name and version); a newer version is said in the main menu ("version 1.3.0 is out: options, gameplay.") and Options > Gameplay offers its page, opened in the browser only when asked (only a page of the port's own releases). Nothing is ever downloaded, replaced or run; no answer is a log line. `$TWIN_UPDATE_TEST=FILE` reads FILE as GitHub's answer (check_windows' "update check" line: no network) | None with it off |
| The disc | The PS2's drive (fileio, MultiStream on the I/O processor) | The ISO image read in place (`--iso`, `$TWINSANITY_ISO` or local.json's `disc_image`); streamed reads finish when asked for | None on what's read; reads never wait |

The build also generates weak stand-ins (`native/tools/make_stubs.py`) for every function nothing defines yet: each logs "not implemented yet" the first time it's called (`--quiet-stubs` hides them) and returns 0. A real definition replaces one at link time. Stand-ins are scaffolding, not behaviour: the log is the list of what's left. What the system's libraries define is never stood in for (the script reads which functions they define), and a stand-in for one of the C library's functions stops the build: it would take the real function's place (issue #27, native/LINUX.md).

Floating point: the R5900's FPU isn't IEEE (no infinities or denormals, results rounded towards zero) and VU0's results are its
own. The native build uses the host's IEEE floats for now; where the decomp keeps VU0's exact results (`Platform::Math`) the
native side has to reproduce them.

## Controls (keyboard and mouse)

The defaults (`src/platform/native/ui/bindings.cpp`, `Infos`; rebound on the options' Controls tab, kept in `bindings.txt`):

| Action | The PS2's | Key | Alt key | Controller |
| --- | --- | --- | --- | --- |
| Move | Left stick | W A S D (full deflection; a diagonal 1/&radic;2 on each axis, as a stick's) | - | Left stick (fixed) |
| Look and camera | Right stick | The mouse's movement (mouse look), and the arrows | - | Right stick (fixed) |
| Jump | Cross | Space | - | South (A / cross) |
| Spin | Square | Left mouse button | J | West (X / square) |
| Crouch, skid, slide | Circle | Left Shift | K | East (B / circle) |
| Status (HUD) | Triangle | Tab | I | North (Y / triangle) |
| Shoulder left / right | L1 and L2 / R1 and R2 | Q / E | - | LB and LT / RB and RT |
| Pause | Start | Esc | Enter | Start |

- Any of the mouse's buttons (left, right, middle, 4, 5) and the wheel's turns up and down can be bound to a key cell (a turn holds
  its key for one of the pad's reads). The mouse's buttons are keys only in play: in the menus the left button picks and the
  right goes back. A binding taken by another action swaps with it. A `bindings.txt` from before this scheme (no `version=2`)
  keeps its pad buttons and takes the new keys.
- Mouse look (`src/platform/native/ui/mouselook.cpp`; the Controls tab's "Mouse look", on by default, "Mouse sensitivity" 1-10,
  default 5, and "Invert mouse Y", off). It applies only while the game is played: GameController's `StatePlaying`, the game
  running (not paused), no cutscene letterbox showing, the options shut and the window in front. Then the window holds the mouse
  in SDL's relative mode with the cursor hidden; it lets go in the pause and game menus, the options, cutscenes and movies, and
  when the window loses the focus, so the menus' cursor works. Each of the pad's reads turns the motion since the last read into
  the right stick: the speed (points a second) over the full-deflection speed (2400 / sensitivity: 480 points a second at 5), at
  least 0.2 once the mouse moves (the game's own dead zone swallows less), and 1 for a flick. With no motion an axis falls back to
  the middle within 50 ms, so it never drifts. A gamepad's right stick and the mouse are both read and the larger deflection on
  each axis wins; the arrows override both. The camera's invert X applies to the mouse too.
- The camera's fastest turn (issue #6): the follow camera turns at its input times 120 degrees a second across and 60 up and down
  (`g_RigYawInputSpeed`, `g_RigPitchInputSpeed`), its input the stick's deflection, so a stick (or a mouse as one) can't turn it
  faster than that; the sensitivity only made the mouse reach it sooner (at 10, from 240 points a second). Past a sensitivity of 5
  the mouse now pushes on past the full deflection, up to 2 at 6 and 6 at 10 (720 degrees a second across): the stick's byte
  stays at its end and the part past it goes to the game's turn (`ScaleCameraTurn`, from `FollowCameraRig::Frame` under
  TWIN_NATIVE: the yaw's and pitch's inputs times it). Up to 5 nothing changed. The camera speed setting (Accessibility, 1-10, 5
  the game's) is a multiplier on that same turn, for the gamepad's stick, the mouse and the arrows (it used to scale the
  gamepad's stick alone, which tops out at the same 120 degrees). `$TWIN_LOOK_TEST` measures it in the game (WINDOWS.md).
- Prompts: with the keyboard in use, a key on a mouse button shows as a keycap with the mouse drawn on it and that button lit
  (`mouseart.cpp`): the options' footer and descriptions, the Controls table (the mouse beside its name), and the game's own hints
  (their glyph drawn as the mouse through `NativeGraphicsSetGlyphImage`, as the Xbox's art is).
- The game's menus' hints (the Controls tab's "Menu hints", `OptionMenuHints`, `menu_hints` in options.txt): "original" (the
  default) leaves OLEG's own hints as the game draws them (select, back, cancel, next and the pause's pages' arrows, "< L1 L2"
  and "R1 R2 >"), their glyphs the controller's as every text's are: a PlayStation pad the game's glyphs, an Xbox or a Nintendo
  pad its buttons (L2 and R2 by the shoulders' second button, the triggers) drawn in the glyphs' places, the Xbox disc's own
  glyphs when there is one (`xboxart.cpp`), else drawn in the game's glyphs' style (`buttonart.cpp`: the dark disc with its
  letter in the pad's colour, the grey bent key with its name), the keyboard its keys (the menus' select and back
  Enter and Esc; one key on both L1 and L2 written once). "Bar" hides them and draws the native bar of prompts instead
  (`DrawGameMenuChrome`, settingsview.cpp). The mouse clicks the game's own: select, back, and an arrow turns the pause's pages
  as L1 and R1 do (mouse.cpp).
- `--selftest-ui` checks the defaults, WASD's diagonal, the mouse keys' names, mouse look's push (a flick saturates, a slow move
  pushes a little, back to the middle with no motion, Y inverted, a flick past the full deflection at 6 and 10), the camera speed
  on the game's turn and the skip below.

Cutscenes and movies: "hold any button to skip" (the cutscene skip option, `OptionSkipCutscenes`, is the master switch;
`ui/features.cpp`). Any key, mouse button or pad button (the triggers too) held 1 s, timed rather than counted in frames,
skips (0.1 s, a press, up to 1.0.12: a button pressed by chance as a scene started skipped it). It counts only if pressed
after the scene began: what was held as it began (running or jumping into it) is ignored until it's let go. A scene is the run of polls from the skip condition (572, `NoOpCutsceneSkippedCondition`) or a movie's frames
(`MovieSkipHeld`); a 0.25 s gap starts the next. The skip is reported for 0.15 s, then whatever is still held is ignored again,
so the scene after isn't skipped too. The prompt is the game's own letterbox text (the AgentLab texts' line 0x18, "hold any
button to skip" in the five languages; the level recipes show it, "Bug fixes (optional)" below). While a button is held, the
overlay draws a bar under it that fills over the second. The game's own skippable scenes (a Triangle rule,
`IsTrianglePressedCondition`) still answer triangle as on the PS2.

What a skip leaves (`src/platform/native/sceneskip.cpp`, issue #9). A skip's rule jumps the director's level out of the state
that runs the scene, and the scene's levels are dropped where they are: the retail game never took these jumps (572 returned
0), and the developers' skip paths go to the director's ending without what the rest of the scene does. Four things of it broke
the game, so as the rule jumps (`NativeFixes::SceneSkipped`, called from `ExecuteState` and `ExecuteInterrupt` before the
levels are unwound) the rest of the scene, each of its levels from the state it's in, is read for them:

- the player's inputs it gives back (its last `SetPlayerInput` giving them all) and the character it gives the player (its last
  `SwitchCharacter`): given back, switched (Rooftop Rampage from roofcor2 left Cortex locked). Cortex chased by the bear (Doc
  Amok) kept them locked, the bear catching him every time; the PS2 game in PCSX2 has them locked during the scene and all given
  back at its end.
- the user messages 207 it sends ("it's over": what the actors' scene behaviours wait for last): its bodies that send one run
  their designators' commands and their sends, and an actor still playing the scene is sent another whenever it waits for one.
- its messages to the director's linked objects (`SendUserMessage` to every linked object, a field's, an object's or an
  index's: the scene's props), sent as it skips, in the scene's order. The Academy hub's first scene (sch_hub,
  COM_SCHOOL_HUB_CUTSCENE_H03A) extends its spiral staircase with message 4: skipped, it stayed down and the Boiler Room
  couldn't be reached till a death reloaded the hub (NitorikoSkye on #9, 1.0.17). Rooftop Rampage's scene sends its three
  drones 87 and 287: skipped, two of them never came for Cortex. Played out and skipped, they now run the same behaviours
  after it (the staircase its own, the drones theirs).
- the actors it gives back their own object (its `StopTargetBehaviour` and `RestoreOwnObject`, their designators read after
  those commands; the actors a director made run its object's behaviours, `RunSlotBehaviourOnLinked`'s source): given back
  (`GiveBackFrame`, each frame) once each runs none of the director's behaviours, or 8 s on (the player's own character 1 s on)
  with the scene's behaviours it still runs stopped as when one finishes and no longer busy. The angry skunk (Jungle Bungle's
  high path) stayed the director: its own slots read the director's object's 4 models and past them, so it was drawn as Crash, or
  the game crashed on what was read as an OGI ("What Windows needed" in WINDOWS.md); the PS2 game leaves it with its own object
  and its behaviour finished. Crash in the Iceberg Lab's scene (labext) stayed in its last animation with no controls for some
  10 s; now the scene's 207s warp him to his end mark, where the scene played out leaves him, and he's given back.
  An actor that asks for a slot of its object (`RunScriptSlot`, at its scene behaviour's end: a character's idle is slot 12) is
  given back then (`NativeFixes::SlotAsked`), so the slot is its own object's: the scene played out has given it back by then,
  and taking the director's object it got the director's. Rooftop Rampage's scene in roofcor2 (COM_CUTSCENE_H03D, 1.0.17): its
  207 makes Cortex draw his gun and ask for slot 12, the director's being Nina's scene behaviour (COM_NINA_CS_H03D), which
  warped him to Nina's mark and made his controls follow its animations (`SetPlayerInput`'s motion driven: no pad read); 1 s on
  the skip stopped it there, and Cortex couldn't move. As a player reaches it, Nina arrives from the bus chase and the scene
  makes Cortex the player; started in roofcor2 he's the player already, and it was the same. Now he runs his own idle
  (COM_CORTEX_IDLE) at once with his own object, the controls on the pad, as the scene played out leaves him and as the PS2 game
  does (PCSX2: the player Cortex with his own object and COM_CORTEX_IDLE, the controls reading the pad, nothing locked; Nina with
  her own object). The skip paths' own behaviours, which the director starts on its actors (`RunSlotBehaviourOnLinked`: Crash's
  skip script on Jungle Bungle's high path), aren't the actor's asking and run as before.

What the scene leaves as it is stays so (the original leaves some actors taking the director's object, or a behaviour running:
the Dingodile hut's Cortex), and the skip path's own steps run as before (the developers' skip scripts, the level recipes' 244
handlers: Crash in the walrus chase gives himself the controls back on 244); an actor the skip path takes over and leaves doing
nothing is given back after 1 s (the training skunk of N. Sanity's spin lesson, left by the developers' skip script). Every skip
logs what it did ("skip: ..."). Checked with check_windows.py's scenarios (the skunk attacked at once, the skunk given back, the
bear, Rooftop Rampage's scene reached as Nina with Cortex moving after it, the Academy's staircase extended) and the skippable
scenes of the 16 levels whose scenes the triggers start, each played out and skipped, what each left 10 s on compared
(`$TWIN_SCENE_STATE`, WINDOWS.md; the comparison drops the states' numbers, so it can't see a prop that never moved: the
staircase was found by a player). A test that starts in the middle of the story may not have a played game's state: started in
roofcor2, the player is Cortex already, and the scene's switch from Nina is never seen; the scenario reaches it through the bus
chase as a player does (`fly` steps).

The scenes no rule skips (`TWIN_FIXES_TEST=skips` lists them): the developers wrote no skip for them (no condition 572 or
Triangle rule in the scene, the state that runs it or the one that runs that), so the mod's way of restoring skips can't reach
them (the waits' skip below ends most of them now): Cavern Catastrophe's five (COM_CAVERN_CUTSCENE_A, B, C, L02D_A, L02D_B), the training scene F (Jungle
Bungle), the Tiki Mon's entrance (N. Sanity hub), Henchmania's intro and N. Brio's and N. Tropy's wins, the Mecha-Bandicoot
training scene (E3), Totem Hokum's scene E, Dingodile's victory, the Slip Slide shed (COM_MOULIN_CUTSCENE_L05B), the Iceberg
Lab's inside (H02B, which never plays), the boiler room's Cortex pipe scene, the Psychetron's Evil Crash cuddle (H04A), the bus
chase, Evil Crash's house (H04C), the blimp leaving the Iceberg Lab, and the final boss's four (intro, two rounds' ends, end);
and the two the mod leaves unskippable on purpose (the party arena, the totem's fall). The movies all skip.

The waits' skip (THE LIST 28 and 22; `src/platform/native/cutsceneskip.cpp`, TwinsanityEvolved's by menaman123's permission,
menaman123/TwinsanityEvolved#19, its MIT licence among the release's). Every in-game cutscene is scripted: a director's behaviour
runs `CutsceneStart`, a timeline of waits, its actors' behaviours and `CutsceneEnd`. Held as the skip option has it (any button,
1 s; the game's "hold any button to skip" in the letterbox), the skip ends the cutscene's timed waits one after another at the
frames they end at when watched: every frame, the soonest wait among the cutscene's agents (a packet's delay, an interpolation's or
a turn's end, a time condition, an animation a condition waits for, an untimed move's end stepped ahead and put back:
`SkippedMoveEndFrame`) is found, and the frames before it are passed for the agents alone (their time a frame on, their own
updates run: moves, animations, physics, the triggers they cross; not their scripts nor anything else's), so the scripts reach
their end in a few frames and leave what they leave watched. The agents are the director, every instance a cutscene agent starts a
behaviour on or messages (behaviourmodule.cpp, commandsfocus.cpp, objectnode.cpp, commandsattach.cpp), what a trigger tells
because an agent crossed it (messagetriggers.cpp) and the player; their sounds and the speech track aren't played
(`CutsceneMusicReady` answers ready), the screen is black meanwhile (gamecontroller.cpp). Where a skip rule is polled (condition
572, or the triangle in a cutscene) the scene stays the rule's, unless the table says the waits' skip goes first.

Which cutscenes it ends is a table (`cutsceneskiptable.h`, made by `native/tools/cutscene_skip_table.py` from TwinsanityEvolved's
census of the disc's 51 scripted cutscenes, each by its site and the graphs only it runs): only those checked here; the rest play
as watched (the log names them once: "plays as watched"), the title's flyby and the two level transitions never.
`native/tools/check_cutscene_skips.py --waits` plays each cutscene three times from the same start (the level sweep's, the
director told its trigger's message where a test can't walk there: `$TWIN_HOP`'s `tell:` step), watched, skipped as the option
does it and skipped by the waits, and compares what each leaves (`$TWIN_SCENE_STATE`: the player, the actors taking another's
object or running a scene, every agent of the cutscene, where it is and what its runners run, 0.5 s into the play after it and a
second later: what changed between the two goes on by itself and is compared by its graphs alone). Checked 2026-11-08 and skipped
by the waits, those no rule skipped: Cavern Catastrophe's cavern director (its scene in cavbridg; started in cortthro or
antfight it plays none, and started in escape or cryscave the game stops at once, watched too: those starts lack what play
brings), the Tiki Mon, Henchmania's fight's start,
the falling totem, inside the Iceberg Lab, Evil Crash's cuddle, the bus chase, Evil Crash's house,
the party arena before the Mecha-Bandicoot fight, the airship to the Iceberg Lab, the crow's nest. Where a rule skipped, it left
a difference in 13 (an actor still the director's for seconds, Crash stopped elsewhere in Classroom Chaos: THE LIST 22): the emu
farmer, the totem on the bear ride, Dingodile's fight's intro, the Psychetron, Uka Uka's fight's start, the Iceberg Lab's exit,
the Academy's hub, the tied totem, the walrus, Classroom Chaos' start, the angry skunk, the penguin, the training on the beach;
and later Dingodile's hut (Dingodile and the snowball left elsewhere; by the waits only Cortex of l05brawl differs, falling
through its floor in both runs: the test starts in gpa01 without the ball's arrival, as TwinsanityEvolved notes) and Rooftop
Rampage's start (the blimp left shown and flying); and in 6 a polled rule never skipped (the triangle polled in the cutscene for
something else): the sentry's look, the dorm room, Rockslide Rumble's start, the bell tower, Aku Aku on the beach, the core's
entrance. In those 21 the waits' skip goes first, and ends them as watched. The rule's skip stays where it ends the cutscene as
watched too: the bear (THE LIST 22's other scene), Slip Slide's start, Uka Uka's defeat. Not skippable yet: the training on the high path (Coco's creature
0.9 units off), Henchmania's start and the treasure room (an actor in another graph after), the Cortex pipe (the skip stalls and
gives the scene back), the Mecha-Bandicoot fight's start, the final boss's, and those the director's message doesn't start (the
Psychetron room's two, the shed, the beehive, the play fight, the Cortex follow): their rules skip them as before, or they play
whole.

## Native bugs found by the level sweep

The level sweep (native/LEVEL_SWEEP.md, `native/tools/level_sweep.py`) starts every level chunk headless and plays it. What it
found that the native build does differently from the PS2, and the fixes:

| Bug | Why | Fix |
| --- | --- | --- |
| VU0 programs called with another set loaded (77 of the first 86 chunks aborted while loading: "another microcode set's code is loaded at the microprogram 0xAB8") | The chunks' loading (`BackgroundWork`, after the frame's draw left VU0's culling set loaded) makes the chunk's instances; a character's follow camera restarts and casts its view (`FollowCameraRig::Restart`, `CastView`), whose collision test calls the standard set's ray-triangle microprogram (0xAB8). On the PS2 VU0 then runs the culling set's code at those addresses and the test returns whatever that gives (a retail quirk: only the camera's first placement reads it) | `Vu0CallMicroprogram` (`math/vu0.cpp`) runs the translated program the call means (the right test) and says so once a program, instead of aborting |
| Loading a save from the main menu crashed (SIGSEGV at pc 0 in `StateBody::Destroy`) | Loading the level unloads the title's chunk (the beach), destroying its scripts; condition 95's (`HeadYawAboveLimitCondition`) vtable is split across two labels in the retail data (`vt_Cond95_Unknown` holds its first entry, `D_00300030` the rest), so the native data took one entry and its destructor slot read past it (null). Any chunk unload that destroys a condition 95 crashed the same way | `include/game/conditions.h`: the vtable declared with its 5 entries, so the data converter takes in the label after it (the PS2 build unchanged). A scan of `.rodata` finds no other vtable split this way. Checked with `$TWIN_SAVE_TEST`: save from the pause menu, then load from the main menu and play |
| Texture packs switched on in the options could crash the renderer | `ReplacementsSetEnabled` (game thread) rescanned the pack, clearing the maps the hardware renderer's `FindReplacement` (presenter thread) reads and whose images it holds pointers to | `graphics/hw/replace.cpp`: the pack scanned once (the first time it's on) and never freed, the switch an atomic, the maps under a lock |

## Building and running

    native/tools/build_ffmpeg.sh
    cmake -S native -B build/native/cmake -G Ninja && cmake --build build/native/cmake
    "build/native/cmake/Crash Twinsanity" --iso "/path/to/Crash Twinsanity (Europe).iso"

`build_ffmpeg.sh` builds the movies' FFmpeg (libavcodec and libavutil, the MPEG-2 decoder only, LGPL 2.1) into
`build/native/ffmpeg/<os>-<arch>` from FFmpeg's checked source release; CMake links it (`-DTWIN_FFMPEG_DIR` picks another), else
the system's with a warning, since Homebrew's is GPL 3 and an app carrying it can't be shared under the LGPL. The libraries are
linked dynamically, so whoever has the app can swap them for their own build, which is what the LGPL's relinking clause asks:
`native/tools/bundle_app.py` puts them in `Contents/Frameworks` with their README and licence in
`Contents/Resources/licenses/ffmpeg`, and native/third_party/ffmpeg/README.md says how to replace them.

Full debug info (`-g`) makes Apple clang 21 drop the asm labels of explicit specializations' members: the build uses line tables.
Watch for `(address + A - 1) & ~(A - 1)` with a 32 bit `A`: its mask clears an address's top half (use `~static_cast<uiptr>(A - 1)`).

## Tools

- `native/tools/find_narrow_pointer_casts.py`: every place a 32 bit integer is made a pointer (clang doesn't warn about
  `reinterpret_cast<T*>(u32)`), from clang's AST. Run it after pulling upstream changes: it should print nothing.
- `native/tools/datastats.py`: the data the native build brings over from the split (`asm/data/`), by section.

## Game options (1.2.0)

Changes to the experience with the original content only, each off by default (options.txt's keys; Options > Gameplay), none
leaving a save the game couldn't have made itself (`src/platform/native/gameoptions.h`):

- Infinite lives (`infinite_lives`, THE LIST 6; TwinsanityEvolved's idea): the played character's death script's -1 life
  (`AddLivesCommand::Execute`) costs nothing: the lives panel shows as on any death, the count as it was, and a checkpoint
  restart never ends in a game over for want of lives (`RestartFromCheckpoint`). Lives given still count. The log says each
  death that cost none. Checked: Crash walked into the beach's sea (`TWIN_PAD_SCRIPT=650-1100:lx=255`), the count stays 5.
- The stronger Aku Aku mask (`strong_mask`, #30): with one or two masks an explosion (TNT, Nitro, a bomb: `HitExplosion`)
  takes the played character's mask as any hit does (one hit point: hit points are 1 + the masks) instead of dealing 100, and
  the invincibility of three masks or the blink after a hit keeps explosions out (`CharacterAgent::Contact`; the bug fixes'
  community change does that last part for everyone). Not under the bug fixes: the original game means explosions to kill.
- The slide jump (`slide_jump`, #11, #35): original (0: the long jump only once the slide's steering is over,
  `CrouchController::Slide`'s `mayJump`), any moment (1: from its first moment too) or easier (2: any moment, the jump pressed
  within a quarter second of getting up from the slide still the slide's (`JumpController::Grounded`), steered in the air at a
  jump's turn rate (`JumpController::Start`)); its height and length the slide jump's own. The log says each slide jump the
  option allowed. Checked with the pad script: a cross 3 frames into the slide does nothing at 0 and jumps at 1 and 2; one 26
  frames in jumps at 2.
- The drop shadow (`drop_shadow`, #11, #35): while the played character is off the ground, a round shadow straight down under
  it (a cylinder's volume of 0.5, 20 down, as the game's own shadow circles: `CastDropShadow` in `shadows.cpp`), so the spot it
  lands on is marked whatever the lighting's slant; drawn as the game's shadows are, on the surfaces that take them. Checked:
  Crash's fall onto the beach at the start shows the disc under him.
- Shadows on crates (`shadows_on_crates`, THE LIST 38; TwinsanityEvolved 0.9.3's idea): the crates' opaque materials made
  shadow receivers (the community mod's change, also in the bug fixes: `MaterialRead`), from a level's next loading. Checked:
  the beach's 47 crate shaders change (`TWIN_FIXES_TRACE` names them).
- The speedrun timer (`speedrun_timer`): real time from New Game in the game's own font at the top (`speedrun.cpp`, the game's
  text queue as "press start"), a split at each level's end (play in another of the 16 levels, `g_AreaLevels`): the level and
  its time shown under the timer for 5 s and logged; the credits stop it; the title resets it.
- The level select (`level_select`; HAB3X's 944aa1c made an option): Gameplay then has a level row and "start the level"
  (from the main menu: elsewhere a line says so). The level starts from its first chunk as a new game does, the story and the
  open areas raised to it; its play's saves go to a card of its own (`memorycard-level-select` in the settings folder,
  `Native::UseMemoryCard`) until the game is back at the title, so the player's saves never get a progress no played game has.
  `$TWIN_LEVEL_SELECT_TEST=N` starts level N from the main menu (the checks).

check_windows' "game options" line runs the first, the slide jump, the speedrun timer, the shadows on crates and the level
select's cases.

## Bug fixes (optional)

The "bug fixes" toggle on the settings' Gameplay tab (`OptionRetailFixes`, off by default) turns on fixes of retail bugs, in the
game's code under `TWIN_NATIVE` and asked for through `NativeFixes::RetailFixes()` (`src/platform/native/fixes.h`). With it off
the game is retail's. Each fix comes from the community mod
[Crash-Twinsanity-Improved](https://github.com/AlexMollard/Crash-Twinsanity-Improved) (its wiki's "What the mod changes", its
`mod/elf_patches.txt`, `mod/levels/*.ops` and PCSX2 patches) or from docs/RETAIL_BUGS.md where the intended behaviour is evident.
Level data is changed only in memory, on what's read from the player's disc. `$TWIN_RETAIL_FIXES=0|1` overrides the option and
`$TWIN_FIXES_TEST=NAME` runs a check in the game (with `--headless --quick`, `src/platform/native/fixestest.cpp`); the checks
pass for either setting of the option (fixed with it on, retail's result with it off). `$TWIN_FIXES_TRACE` logs the scripts the
recipes edit.

| Fix | The bug | Source | Code | Verified |
|---|---|---|---|---|
| Aku Aku's invincibility survives explosions | The damage handler lets through any hit of 50 or more while invincible; TNT, Nitro and bombs deal 100, so three masks (or the 2 s after a hit) didn't save Crash from them | The mod's "Aku Aku invincibility" (`elf_patches.txt`, 0x137590-0x137604) | `CharacterAgent::Contact` (`characterstate.cpp`): invincibility also keeps out hits of kind `HitExplosion`; other big hits (instant deaths) still go through | `TWIN_FIXES_TEST=aku`: an invincible Crash hit by a 100-damage explosion keeps his 2 hit points with the fix, dies without it; a generic 100 hit kills either way |
| No invisible Crash after a cutscene | A cutscene taking or giving back the player resets the hurt blink's state machine without ending it: one time in five the character stays hidden (and invincible) through the scene and after | The mod's "Invisible Crash after a cutscene" (stub at 0x116EE0 on the store at 0x131844) | `CharacterAgent::Reset`: a running (previous mode invincible) or requested (mode invincible) blink is ended first: visible, the part's `invulnerable` off | `TWIN_FIXES_TEST=reset`: a hidden, blinking Crash is visible and not invincible after the reset with the fix, hidden and invincible without |
| The masks' invincibility particles in the next area | With three masks (Aku Aku's, or Uka Uka's as Cortex) the invincibility's three trails emit from the character through emitters the mask's controller keeps itself (`MaskController::trailSlots`), not among the character's own trails, which the game moves into the next chunk with it (`ParticleTrails::ChangeChunk`): after a chunk change they stay in the old one, the character's place (in the new chunk's frame) seen through the old chunk's puts them past their cut-off from the camera, and the particles stop while the mask stays on, in every level (#18: on the PS2 too, as NormanTheFox reports; THE LIST 15) | #18 | `MaskController::StepInvincible` (`nodecontrollers.cpp`): an emitter whose chunk isn't the character's is given the character's chunk, as ChangeChunk does, kept the game's loose kind (as `StartEmitter` makes it) so a reset after a death lets it go as before | check_windows' scenario "The mask's invincibility trails into another chunk (#18)": huba to hubb with four masks (`$TWIN_HOP`'s `mask` step), `$TWIN_MASK_TRACE`: retail's emitters stay in huba, off, 147-221 units from the camera; fixed, in hubb, on, 8 units away (7.8 before the change) |
| Two masks' stars gone after a cutscene | With two masks (Aku Aku's or Uka Uka's) the mask shines with stars, its boost trail. While the game watches a cutscene the mask's controller hides the mask (`MaskController::Frame`: `Hide` destroys its node's trails, the boost trail among them) and after it shows the mask again without them: two masks look like one until the hit points change (#18, NormanTheFox; THE LIST 15). On the PS2 too: the retail PAL game in PCSX2, the high path's skunk scene with two masks, the EE's memory before, during and after it: the mask's node had its trail before, none during (hidden) and none after (shown, still two masks) | #18 | `MaskController::Frame` (`nodecontrollers.cpp`): shown again in that state, the boost trail is put back as `Boost` puts it (its node's trails destroyed first, so one made while hidden isn't doubled) | check_windows' scenario "The two masks' stars back after a cutscene (#18)": the high path's skunk scene with two masks, `$TWIN_MASK_TRACE`'s line as the mask is shown again: retail "its stars off", fixed "its stars on" |
| Evil Crash follows his route in Bandicoot Pursuit | A route's step a script aims at "straight ahead" (`SetFocusPosition`'s option: the step's distance along the agent's facing) lands off the step whenever the agent doesn't face it closely, and a route that waits for the agent to be inside the step's circle never goes on: Evil Crash, placed facing away from his first step (71.5, 2.65, -91.69, radius 1), is sent 1.53 off it and stands at (71.6, 2.6, -93.2) for good once the chase starts, so Cortex runs on alone (THE LIST 37). On the PS2 too: the retail PAL game in PCSX2 (`run_pcsx2.py retail --start-chunk` altdoc, the player moved by PINE writes as below, the EE's memory dumped 24, 34 and 44 s later) has an instance's place at (71.75 to 71.78, 2.45 to 2.58, -93.2) all along | TwinsanityEvolved 0.9.3 (the finding and its method; our code) | `SetFocusPositionCommand::Execute` (`commandsfocus.cpp`): a straight-ahead point farther from a current or next step than half its radius gives way to the step as the designator put it | check_windows' scenario "Bandicoot Pursuit's Evil Crash follows Cortex under Bug fixes" (Crash put on the checkpoint crate at 75.3, 2.6, -100.8, then on the pad at 62.49, 1.6, -100.8; `$TWIN_ROUTE_TRACE` logs such steps): retail stands at 71.6, fixed he's at 52.5, 3.8 behind Cortex, 4 s after the pad |
| Shadows on crates | The characters' shadow only darkens materials whose shader has the settings' byte 24 (the decomp's `noFba`); most crate materials don't have it, so Crash's shadow never fell on a crate | The mod's "Shadows on crates" (wiki engine/graphics; `tools/materials.py`) | `MaterialSectionReaderRead` (`graphicstables.cpp`) hands the material item's bytes to `NativeFixes::MaterialRead`: every opaque shader (blending 0) of a material named `CRATE_`, `CRATES_`, `AKUCRATE_`, `AKUCRATES_` or `life_crate` gets byte 24 set, as the mod's tool (a material that doesn't read to its end exactly is left alone) | `TWIN_FIXES_TEST=recipes` counts them on the disc: 580 shaders in 89 levels, the mod's build's own count. Applied when a level loads (a change of the option shows at the next load). Not checked on screen |
| A beaten boss stays beaten | The beaten Tiki Mon (Totem Hokum) keeps its contact damage: walking into the wreck costs a mask, or a life and the fight again | The mod's `mod/levels/harmless_after_defeat.ops` | The level recipes (below): `appendcmds 4015 8 0 1511 7 0`, COM_EARTH_WORM_START's lone `SetAgent(64)` appended to COM_TIKI_MON_ACTIVATED's last state | `TWIN_FIXES_TEST=recipes`: the op made on hubd.rm2's script (634 bytes to 642: one command of one argument), the copied commands the same in the four levels that have script 1511. Not played to the boss |
| WalkController::AskTurn | In the 45-135 degree band the turn is eased by `abs(wanted - 45)/90` for both signs, so turning one way is eased 0.5-1 and the other 1-1.5 | RETAIL_BUGS.md (C, reached: walking) | `characterwalk.cpp`: the share is taken of the turn's size | `TWIN_FIXES_TEST=units`: a 90 degree stick turn gives 614400 and -1024000 retail, 614400 and -614400 fixed |
| CharacterAgent::KeepPushedBody | The 4-unit reach is tested on the way to the body after it's made unit long, so a pushed body is kept at any distance ahead | RETAIL_BUGS.md (C, reached) | `charactermovement.cpp`: the length taken before | Code review: the test now sees the distance |
| HumiliskateVehicle::TouchesHull | The contact point is scaled by -rx, -ry and -rz in turn, every component by all three radii | RETAIL_BUGS.md (C, reached: Humiliskate) | `humiliskate.cpp`: each component by its own radius | Code review |
| LaySkidMark | The near end's depth is tested twice, the far end never, so strips are laid with their far end deep under the surface | RETAIL_BUGS.md (C, reached: Humiliskate, Rollerbrawl) | `skidmarks.cpp`: the far end tested too | Code review |
| Cond141-146 (the axis distances) | They measure from the instance to the point of its axis nearest the target: the distance along the axis, not off it (the "horizontal distance" ones give the vertical) | RETAIL_BUGS.md (P, reached: 145 three uses, 146 five) | `conditionchecks.cpp` `DistanceAlongAxisSquared`: from the target to that point | Code review |
| RollerbrawlYaw | Past 50 u/s the ball's speed is replaced by the most yaw speed in radians (3.49), so the camera swings at ~37 deg/s instead of ~200 when fastest | RETAIL_BUGS.md (C, reached: Rollerbrawl) | `followrig.cpp`: the speed kept at 50 | `TWIN_FIXES_TEST=units`: the yaw speed at 100 u/s is 6774 retail, 36408 (the 50 u/s one) fixed |
| MoveAim (Cmd591), aim "both" | `(second - first) * 0.5`: half the offset, not the point between | RETAIL_BUGS.md (C, reached: 19 of 388 uses) | `commandscamera.cpp`: `(first + second) * 0.5` | Code review |
| ReleaseInstanceTrack | The track's values (0x14 bytes and a heap header) are nulled without being freed: every cutscene leaks them per instance track | RETAIL_BUGS.md (C, reached) | `videocontroller.cpp`: freed first | Code review |

The cutscene skip's own option (`OptionSkipCutscenes`, off by default: the PS2 game couldn't skip its cutscenes) also makes the mod's level recipes that restore the
skips the retail game cut, with its prompt:

| Change | Source | Code | Verified |
|---|---|---|---|
| The 16 scenes whose skip needed their level's scripts rebuilt (Aku Aku crate, beach training, angry skunk, Rooftop Rampage in both its levels, the Academy hub, the treasure room, Slip Slide Icecapades, Iceberg Lab, Classroom Chaos, the core intro, Madame Amberly's bell tower, both Psychetron room scenes, the dorm room, the walrus chase, Rockslide Rumble) | The mod's `mod/levels/*.ops` | `src/platform/native/levelrecipes.cpp` with the mod's recipe files verbatim (`levelrecipes.inc`): `LoadScript` hands each graph's bytes to `NativeFixes::ScriptRead`, which takes them apart as the mod's `twinsdump edit` does, makes the ops (`addbody`, `copybody`, `clearbodies`, `appendcmds`, `delcmd`, `movebody`, `settarget`, `setarg`) in the recipes' order, and has the game read the result into the same graph. A script whose ops copy from one read after it (the walrus chase, Classroom Chaos) is edited once that one is read; a script that doesn't fit an op is left as the disc has it (logged) | `TWIN_FIXES_TEST=recipes`: every one of the disc's 20,559 scripts (135 levels) taken apart and put back byte for byte; every recipe made on its level (0 failures). In the game, the beach's five are edited as it loads (`TWIN_FIXES_TRACE`). Not played through |
| "Hold any button to skip" in the letterbox while a scene can be skipped (the mod's "hold triangle to skip", made any button: "Controls (keyboard and mouse)" above) | The mod's `skip_prompt.ops` and `mod/text.txt` | The same: `BottomTextDisplay` of the AgentLab texts' line 0x18 on every jump into a state that plays a cutscene and has a Triangle rule, `BottomTextClear` on the jumps out (`skipprompt auto`, made for every script read rather than for the mod's list of levels; the self-test finds that this adds it only to COM_CORTEX_DOCAMOK_EARTH_PHASE2 in 38 more levels, a scene that plays in Doc Amok's alone), plus the mod's seven named ones; line 0x18 (the unused "zzz") is "hold any button to skip" in each language (`pctext.cpp`, native/PC_TEXT.md) | `--selftest-ui` (the line matches the disc, the font has the characters); the self-test above; `TWIN_FIXES_TEST=prompt` with `--snapshots` shows a letterbox with the recipes' command (then "HOLD I TO SKIP": the line named triangle's glyph before it said any button) |

The 21:9 option (`OptionUltrawide`, "ultrawide" in options.txt, off): with the game's widescreen on, the widescreen picture is
21:9, TechieSaru's fix in the mod's PCSX2 patches ([Widescreen 21:9 Ultrawide]): 0x40155554 (2.333) in place of 16:9's
0x3FE38E39 where the renderer loads it (the view's projection in `SetRendererView` and `DrawRendererScene`, the 2D's squeeze in
`FitPlaceToScreen`/`FitSizeToScreen`, `renderer.cpp`), and every movie pillarboxed to the middle three quarters (the patch's nop at
0x2AF408, `MoviePlayer::Draw`). Checked with `--snapshots`: the beach's opening shows a wider view squeezed into the same picture.
The option is the Display tab's "Widescreen" choice 21:9 (4:3, 16:9, 21:9, 32:9; 21:9 is widescreen on and `OptionUltrawide`); the presenter (`ViewRect`, `graphics/display.cpp`) still shapes the picture 16:9:
`NativeFixes::Ultrawide()` tells both.

32:9 (since 1.2.0, THE LIST 35, after TwinsanityEvolved 0.9.3's idea; `OptionSuperUltrawide`, "super_ultrawide", off): the same
way at 32/9 (`WideTvAspect`: the projection and the 2D's squeeze), the window's picture 32:9 (`ViewRect`). The widgets anchored to
the screen's sides (the menus' hints, the loading screen's logo) are kept to the middle 16:9's sides (`FitPlaceToScreen`: their
anchor moved in by 16/32), as a 16:9 TV shows them; the pause page and the HUD, anchored to the middle, stay there at their 16:9
size; the FPS counter sits in the middle 16:9's corner; each movie keeps its own shape in the middle (a 16:9 one half the width,
a 4:3 one three eighths: `MoviePlayer::Draw`). Checked in a 1920x540 window: the beach shows more of the world at the sides, the
pause page and its hints where 16:9 has them.

Already in the port before these: the skip itself (condition 572, now any button held), movies answering it too, the faster loading
and 60 Hz (their own options), widescreen at start-up, and movies at their own rate (the native player paces them by their
sequence header, not by vertical blanks).

Not done, and why:

| Item | Why |
|---|---|
| JumpController::Rise (the knee drop's wait for room) | Where the wait should end isn't evident: the bit is never cleared anywhere else, so keeping it makes the rise retry the knee drop every frame and never reach its fall while there's no room |
| JumpController FallFrame (the per-kind falling gravities) | P: the per-kind gravities never lasted more than a frame, so every fall in the game was tuned on property 1; applying them changes every jump's arc |
| WalkIntoBody (the sphere's sideways push) | The push's size is never computed (it adds `f20 = 0` times nothing): what it should be isn't known |
| OverlappingRangesSpan, StepMovement, CollideRigidBodies | No visible effect known (collision cells, sleeping bodies) or a change to every rigid body's response the game was tuned with |
| The save checksum | Fixing it changes the save format: saves made each way wouldn't load in the other |
| String growth, DiskSizeClass, FindMinimum | No visible effect (heap churn, a size class, convergence speed) |
| The P entries (PoseJoint's squash, Solve's crush test, MakePhysicsBody, ClipSegmentToPlanes, UpdateCutsceneVolumes) and the latent, unreached and dead ones | Possibly intended, or nothing in the game's data reaches them |
| Party arena skip, totem falling skip | Not shipped by the mod (work in progress, and left unskippable on purpose) |

Unconditionally (`TWIN_NATIVE`, not the option): where retail reads memory it never set, the native build gives a defined value,
as the native stack and heap leftovers aren't the PS2's (and one read was out of bounds):

| Where | Retail | Natively |
|---|---|---|
| `PlayCameraTrackFrame` (`cutscenetracks.cpp`) | At the end frame (a skipped cutscene) the cuts are read 0xFFFF frames past their data; a value above 0 there drops the end's values | No cut read at the end frame: the end's values are played |
| `LookController::Construct` (`characterlook.cpp`) | `restSeconds` as the heap had it | 0 |
| `ConstructSkidMarks` (`skidmarks.cpp`) | The last point and the strip's two colours as the heap had them | 0 |
| Cmd48 DropAttachedObject, the attachments' launch (`commandsattach.cpp`, `attachments.cpp`) | A velocity of the stack's leftovers | None |
| Particle trail kind 5 (`particletrails.cpp`) | Compares a vector it never sets | None |
