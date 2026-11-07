# The native graphics (macOS, Linux)

`src/platform/native/graphics/` is `Platform::Graphics` for the native build. It doesn't redraw the game its own way: it keeps the
PS2 renderer's own code, which builds DMA chains of VIF codes, VU1 data and GS packets, and runs those packets on an emulation of
the hardware they were written for. Every picture is what the game's packets, the game's VU1 microcode and the GS's rules make
of the game's data; nothing (layouts, colours, textures, effects) is authored here.

```
game C++ ──Platform::Graphics──▶ renderer/ (the PS2 renderer's packet code)
                                   │ DMA chains (VIF1, GIF, VIF0)
                                   ▼
                    ee/dma ─▶ ee/vif ─▶ ee/vu (VU1: the game's .vutext programs) ─XGKICK─▶ ee/gif ─▶ gs/
                              (UNPACK, MPG, MSCAL, DIRECT ─────────────────────────────────▶)       │
                                                                                     PCRTC read-out ▼
                                                                         display.cpp ─▶ SDL3 window (GL 3.3)
```

## The parts

| Path | What |
|---|---|
| `gs/gs.h`, `gs/gs.cpp` | The GS: its registers (both contexts), the vertex queue and kicks (XYZ2/XYZF2 draw, XYZ3/XYZF3 don't), PACKED's Q, host to local and local to local transfers, the CLUT buffer's loads (CSM1 and CSM2, CLD 0-5, CSA), the PCRTC's two circuits merged by PMODE |
| `gs/gsmemory.cpp` | The 4 MB of local memory in the GS's page/block/column layouts for PSMCT32/24/16/16S, PSMT8/4/8H/4HL/4HH and PSMZ32/24/16/16S (checked against PCSX2's tables for every pixel of 256x128) |
| `gs/gsdraw.cpp` | The rasteriser: points, lines, triangles (and strips, fans) and sprites, sampled at whole pixels with the top-left rule in 12.4 fixed point less XYOFFSET, clipped by SCISSOR; the pixel pipeline in the GS's order: depth test, texture (TEX0/TEX1/CLAMP/MIPTBP, nearest or bilinear with 4 bit weights, mipmaps by LOD = log2(1/Q) << L + K or K, TEXA for 24/16 bit texels, the CLUT), TFX (modulate, decal, highlight, highlight2, TCC), alpha test (and AFAIL), fog, destination alpha test, the (A - B) * C >> 7 + D blend (PABE), dithering of 16 bit frames (DIMX), COLCLAMP, FBA, FBMSK/ZMSK |
| `ee/vu.h`, `ee/vu.cpp` | VU0 and VU1 in micro mode, interpreted: the FMAC/FDIV/EFU pipelines' timing the programs are scheduled around (FMAC stalls, flags 4 cycles late, Q after DIV/SQRT's 7 cycles and RSQRT's 13, P after each EFU latency), branch delay slots, a branch reading the old value of an integer register the instruction before wrote, the same-cycle upper/lower rule, the E bit's delay slot; VU floats (denormals 0, overflow clamped, rounding toward zero); and VU0's macro mode instructions the renderer's culling and decals use |
| `ee/vif.h`, `ee/vif.cpp` | VIF0/VIF1: UNPACK (every VN/VL, USN, masks, modes, skipping and filling write cycles, TOPS), MPG, STCYCL, OFFSET/BASE/ITOP and the double buffer's swap at MSCAL/MSCALF/MSCNT, STMASK/STROW/STCOL, DIRECT to path 2 |
| `ee/gif.h`, `ee/gif.cpp` | The GIF's three paths: tags, PACKED (A+D, NOP, the registers' packed forms), REGLIST, IMAGE |
| `ee/dma.h`, `ee/dma.cpp` | Source chain mode (CNT, NEXT, REF, REFS, REFE, CALL, RET, END, the two level stack), with TTE for the VIFs |
| `ee/address.h`, `ee/address.cpp` | The DMA tags' 32 bit addresses for the host's 64 bit pointers: each 64 MB window of host memory gets one of 63 slots |
| `hardware.h`, `hardware.cpp` | The emulated hardware, and the chains sent on its channels |
| `renderer/` | The PS2 renderer (`src/platform/ps2/renderer/`) forked: see below |
| `graphics.cpp` | `Platform::Graphics`' start-up, display set-up and presentation (the PS2's `graphics.cpp`) |
| `display.h`, `display.cpp` | The vertical blank's pacing (50 Hz PAL, 59.94 Hz NTSC) and the PCRTC's picture shown in the window: OpenGL 3.3 core, entry points loaded through SDL, the picture at the TV's shape (4:3, 16:9 with the game's widescreen setting), letterboxed |
| `window.h` | `NativeGraphicsAttachWindow(SDL_Window*)`: the platform hands its window over |
| `resolution.h`, `gs/gsscale.cpp` | The resolution setting: `NativeGraphicsSetResolution(internalScale, windowWidth, windowHeight, fullscreen)`, `NativeGraphicsGetResolution(...)` / `NativeGraphicsCurrentResolution()`. The internal scale (1-4) draws every primitive twice: as the PS2 draws it into local memory, and with its places scaled into shadow buffers of scale x scale samples a pixel beside each frame and depth buffer (see "Drawing at a higher resolution"). The window part sizes the window or makes it full screen; the picture keeps the TV's shape letterboxed. Nothing is saved: the settings' side keeps the values |
| `presenter.h` | The UI's presenter settings: `NativeGraphicsSetOverlay(draw)` (a callback after each picture, the window's framebuffer and full viewport, GL 3.3 core current: the UI's screens at the window's resolution), `NativeGraphicsPictureRect` (the picture's place in window points, after the letterbox), `NativeGraphicsSetPresentFilter(linear)`, `NativeGraphicsSetPostFilter(0 none, 1 CRT)` drawing with the UI's `NativeUiCrtShaderSource()` (weak here: none) and its uniforms (`picture`, `pictureSize`, `viewSize`, `time`, then `NativeUiCrtShaderUniforms`). None of them touches the PS2's picture |
| `glyphs.h`, `glyphs.cpp` | The UI's glyph images: `NativeGraphicsSetGlyphImage(code, rgba, width, height)` (nullptr: the font's own glyph). Each font's glyph table gives the code's rectangle in its page (UV in sixteenths of a texel: U, V the first corner, U + width, V - height the other: the pages are upside down); the GS draws a UV sprite of exactly that rectangle from the image instead, through the same pipeline (colour, blend, filter), at every internal scale. Harness: `--glyph CHAR` (a test ring) |
| `hw/` | The hardware renderer (the default; the harness's `--hardware 1`): `gl.h` GL 3.3 loader, `hwgs.h/.cpp` the GS's primitives drawn by OpenGL into render targets at the internal scale, kept in step with local memory page by page (see the file's head comment); what GL can't draw exactly goes to the software GS. `gs/backend.h` is the GS front end's interface to it |
| `settings.h` | The UI's graphics settings: `NativeGraphicsSetRenderer(0 software, 1 hardware)` (default hardware; `$TWIN_GS=software`), `NativeGraphicsSetDither(0 none, 1 PS2 pixels, 2 fine, 3 none and full colour: the Dithering option off)`, `NativeGraphicsSetTextureFiltering(0 the game's, 1 bilinear, 2 trilinear + 16x anisotropic)`, `NativeGraphicsSetTextureUpscale(0-2)` (not yet: it refuses anything but off), `NativeGraphicsSetTexturePack(bool)` (PCSX2's packs, see below), `NativeGraphicsSetAntiAliasing(0 off, 1 FXAA, 2 and 3 supersampling 2x and 4x: the internal scale times 2 or 4, up to 8; from HAB3X's 90d3287)`. A picture larger than the window (supersampling's, or a resolution past the screen's: 8x's 4096 rows on a 1440 one) is shown as the mean of the texels each window pixel covers (display.cpp's DownsampleSource) whatever the anti-aliasing and filtering options: a single bilinear read there skipped texels, the edges' steps back (1.1.0 and before with anti-aliasing off or FXAA), `NativeGraphicsSetVSync(bool)`. The internal resolution is `NativeGraphicsSetResolution`'s: 1-8 (software 1-4), 0 = match the window |
| `gs/workers.h`, `gs/workers.cpp` | Worker threads big primitives' rows are shared out to (`TWIN_GS_THREADS` sets how many cores; rows are independent, the pixels come out the same) |
| `movie.h`, `movie.cpp` | For the native movie player: a decoded picture put in the GS where the PS2's player puts it, and the PS2 player's drawing packet |

## The renderer's fork

Each of `src/platform/ps2/renderer/*.cpp` has its copy in `renderer/`, the same code but for:

- DMA addresses: `Address()` is `Ee::DmaAddress`, and the few places that did arithmetic on a pointer's 32 bit value (aligning the
  DMA regions, the instance blocks' room) do it on the host pointer; addresses kept in `u32` fields (a shader's registers, a blend
  part's packets) go back to pointers through `Ee::HostAddress`.
- Hardware access: the DMA channels (`dma.cpp`) run their chains on the emulation; `LinkRenderBuckets` sends the frame's chain to
  the emulated VIF1; VU0's macro mode asm (`culling.cpp`'s box clipping and tests, `particles.cpp`'s decal aging, VCALLMS) calls
  the emulated VU0; `SendToVu0`'s copier writes VU0's memory directly (the copier only moves the quadwords).
- Layouts: a texture's file header (0x60 bytes) is read over the start of `Texture`, whose slot pointer moved past it; objects the
  PS2 sized by its own layouts (shaders, shader animations, materials) take the native sizes; `MaterialStorage` is 0xA0 natively
  (`include/platform/graphics.h`, under `TWIN_NATIVE`).
- PS2SDK's `libgs.h` layouts the packets use are `renderer/gsregs.h`.
- The VU programs' code is taken from the `.vutext` blob (`D_002D9D90`) by address.
- Retail objects the PS2 code writes past the split's label (the frame chain's head at `G_MainGifDmaTag`, VU0's program sets at
  `G_UnkDmaRelated`, the display settings at `G_FrameBufferBasePointer`) and `.bss` objects whose types hold pointers are defined
  natively at their native sizes; the rest of the retail data is the native build's converter's.

## Drawing at a higher resolution

At an internal scale N above 1, local memory stays the PS2's (everything the game's packets read from it, transfers, textures,
palettes, the copies the effects make, is what the PS2 has), and beside each buffer a primitive draws into (frame or depth) a shadow
keeps N x N samples a pixel:

- a primitive is drawn into local memory as always, then again into the shadows with its places (XYOFFSET and the scissor too)
  N times as far apart and the samples centred on the point the PS2 samples a pixel at; its texture coordinates, colours, depths,
  fog and level of detail are interpolated the same way, so it's the same pipeline with more samples (the dither matrix is
  indexed by the PS2's pixel, so the pattern scales with the picture)
- a texture read from a buffer that has a shadow of the same layout (colour 32/24, 16, 16S; depth 32/24, 16, 16S) reads the
  shadow's samples (the frame copied to the shown buffer, the screen copies, the half-size buffers); a texture read in another
  format (an 8 bit view of the depth for the colour filter's palette) reads local memory, at the PS2's resolution
- a shadow takes local memory's pixels again for every page something else wrote (a transfer, drawing in another format), each
  sample of a pixel the same
- the PCRTC reads the shown buffer's shadow: the picture is N times the size

Downscaled by averaging, the 2x and 4x pictures of the legal screen are within 0.6 of the 1x one (mean, per channel). Lines and
points stay a sample wide. The cost is N x N times the pixels plus the 1x drawing (2x: about 5 times the time).

## Shared code edits

| File | Edit |
|---|---|
| `include/platform/graphics.h` | `MaterialStorage` 0xA0 under `TWIN_NATIVE` (0x70 on the PS2) |
| `src/game/movie.cpp` | `MoviePlayer::Start`: when the movie doesn't open, the renderer's buckets go back to its own DMA buffers (`ResetBuckets(false)`). The retail code leaves them in the movie's buffers, a tenth of the size, which the title's and levels' frames overrun (chain 0 into chain 1's bucket heads: VIF1 then reads packet data as codes). No PS2 movie fails to open |

The PS2 build stays the same executable (checked with `tools/build.py` and `cmp` after each edit).

## Where the native build differs from the PS2

| Where | The PS2 | Native | Effect |
|---|---|---|---|
| When chains run | The DMA sends a frame's chain while the game builds the next | Run at once when it's sent | None: the game waits for each before it touches what the chain reads |
| XGKICK | The GIF reads VU1's memory over the cycles after the XGKICK while the program goes on | The packet read at the next XGKICK or when the program stops (`Vu::Kick`, `FlushKick`), with what the program wrote into it by then | None. Until 2026-11-01 read at the XGKICK: the skinned meshes' program writes its packet's last two vertices' colours 2 and 5 cycles after its XGKICK, and every animated character (Crash, Cortex, the cutscenes' cast) had two vertices in 38 coloured as the batch before's, the sharp-edged patches players saw (`$TWIN_VU1_KICK_AT_ONCE=1` reads at the XGKICK as before) |
| VU floats | VU arithmetic | Host IEEE single precision with round toward zero, denormals and overflow handled as the VU does (PCSX2's model) | The last bit of some results can differ (the VU's multiplier isn't exactly IEEE) |
| Rasterisation | The GS's DDA | Edge functions with the GS's sampling and fill rules; attributes interpolated in double precision, colours with 7 fraction bits, texture coordinates 16.16 with 4 bit bilinear weights | A pixel's colour or texel can differ by one step at some edges |
| Anti-aliasing (PRIM's AA1) | Coverage alpha on edges | Not done | The game only sets it through shader type 0's settings |
| SendToVu0 | VU0's copier program takes eight quadwords at a time | Copied straight into VU0's memory | None |
| The display | Interlaced fields; the two circuits read the same frame a line apart and are blended 50/50 | The whole frame shown progressively; the circuits merged with their offset counted in frame lines (an offset of one field line is none, as PCSX2 shows it) | No interlacing flicker or deflicker blur |
| Scaling to the window | The TV | Linear (or nearest with `TWIN_DISPLAY_FILTER=nearest`) | Softer or blockier than a TV |

## The harness

```sh
cmake -S native -B build/native/cmake -G Ninja && cmake --build build/native/cmake     # makes build/native/data.cpp too
cmake -S native/graphics-harness -B build/native/harness -G Ninja && cmake --build build/native/harness
build/native/harness/graphics-harness build/native/harness/out [--frames N] [--text TEXT] [--picture Language\Legal\English.psm]
```

Run from the repository's folder (`local.json` names the disc image). It starts the game's renderer and readers as `Main` and
`StartGame` do, reads the game's font and a screen picture through the game's own readers, queues them through the game's
overlay, and writes each frame's PCRTC picture (`frameNN.png`) and the frame drawn in GS memory (`drawnNN.png`).
`TWIN_GS_TRACE=1` prints every primitive the GS draws with its registers. `TWIN_FRAME_STATS=1` prints each shown frame's time, VU1's and VU0's instructions, the primitives and their bounding boxes' pixels.

Comparisons with PCSX2 (`tools/run_pcsx2.py --manual --snapshot T`, its 640x480 snapshots):

| Screen | Native | PCSX2 | Mean difference (per channel, after scaling) |
|---|---|---|---|
| Legal screen | `frame02.png` of the harness | `snapshot_022.0s.png` | 0.9, 0.5, 0.6 |

## Not done yet

- Speed: the harness's legal screen takes about 17 ms a frame at 1x, 72 ms at 2x and 300 ms at 4x (M-series Mac, 7 worker
  threads; rows are handed out a few at a time to whichever thread is free). 2x and above are too slow for 50 Hz yet;
  the scaled pass shades with the same specialised span code as 1x, so the cost is the samples themselves.
- The 3D path runs the same way but hasn't been checked against PCSX2 yet (the native game doesn't reach a level yet).

## The hardware renderer

`hw/hwgs.cpp` draws the GS's primitives with OpenGL 3.3 core into render targets at the internal scale, the way PCSX2's
hardware renderers do; the software GS stays the reference (`$TWIN_GS=software`, the harness without `--hardware`), and the
capture/replay compares them (the harness's `--replay FILE --hardware 1 [--scale N]` writes `replayNNN.png`, local memory's
picture at the PS2's resolution, and `replayNNN_gpu.png`, the GPU's at the scale).

- Targets: one for each frame and depth buffer drawn into (base, width, layout: 32 and 24 bit colour share one, as do Z32 and
  Z24), as tall as what's drawn. A PS2 pixel is a block of scale x scale pixels whose first sample is the GS's sample point.
  Buffers narrower than the screen (the effects' half resolution buffers) stay at the PS2's resolution (native scaling); one
  the game draws a scene into, its depth written, is the scale's with its depth buffer (below, "Scenes drawn into narrow
  buffers").
- Local memory and the targets: each 8 KB page records where its newest content is; a target is brought up to date from local
  memory before the GPU uses it, local memory from a target (at the PS2's resolution) before the CPU side reads or writes it
  (transfers, the CLUT, texture decoding, the display read, a software draw).
- Textures: a target's own buffer in its own layout is sampled from the target where the target drew it (its `drawn` pages,
  not memory's content loaded into it) and the texture doesn't repeat past its edge (the target's edge clamps); any other is
  decoded by the software GS's decoder (every format, the CLUT, TEXA) and uploaded by 8x8 tiles as the draws reach them
  ("Ant Agony's walls and doors", below).
- The pixel pipeline in the shader as the GS defines it (TFX, fog, alpha test, FBA, 16 bit frames' dither and truncation; with the Dithering option off a 16 bit frame's dithered draws, the frame's copy into
  the buffer shown, keep their 8 bits a channel: BitFullColour);
  the alpha test's AFAIL by a second pass; depth as a 32 bit float written from the shader; blending by GL's dual source
  blending when it can make the GS's blend exactly, else in the shader against a copy of the target (batches split where
  primitives overlap): partial frame masks, the destination's alpha as the factor, FIX over 0x80, COLCLAMP off, PABE and
  16 bit frames; the destination alpha test by a stencil pass when the draw can't change the alpha bit, else in the shader.
- The PCRTC: the shown buffer's target (both circuits merged on the GPU as the PCRTC merges them) is the picture.
- Left to the software GS: a frame in a Z format (the game's depth buffer copy into a Z32 frame); a target GL can't make at
  the scale (HAB3X's 148af1e): past the GPU's biggest texture, past the draws' viewport (12288 pixels into a target: a
  2048-row target at 6x, a 640-wide frame at 8x is 5120), or no memory for it (GL_OUT_OF_MEMORY). Its framebuffer is left
  incomplete (a draw into one is a GL error; macOS's GL aborts on it), its draws go to the software GS, and it's never
  shown or read as a texture. The log names each ("the target ... can't be drawn into"). The options' scales: 1x to 4x,
  6x and 8x.
- Exactness the shadow volumes (the beach's character shadows) need: vertexes on the rasteriser's sixteenth of a pixel grid
  and a viewport far bigger than the target (no snapping, no clipping: a sliver of a primitive keeps its pixels); wrapping
  additive blends' parts summed as their signed values in 256ths (exact in the half float sums); a 32 bit depth buffer with
  its values in the top half kept reversed (exact there); depth truncated as the GS truncates it. 1x: the shadow pass's
  stencil counts match the software GS's but for one texel (its depth copy's).
- Upscaled effects reading textures made at the PS2's resolution: a draw whose texture is decoded from memory read back from
  an upscaled target, and which tests (without writing) an upscaled depth buffer, makes its depth test and its texture read
  at each block's first sample (the GS's sample point), as the texture was made there. Without it the beach's depth haze
  (a palette lookup of the depth's copy) drew a bright line where the depth crosses its threshold: a circle around Crash.
  That haze is the game's colour filter, drawn at the scale since (next): this way it was the PS2's pixels' and stepped every
  outline against far things.
- The game's colour filter at the scale (BitDepthIndex, DepthIndexColour): a draw reading 8 bit indexes (PSMT8H) through a 32 bit
  palette from pages that hold the copies of an upscaled depth target (the draws into a 16 bit colour buffer whose texture is
  that target's memory in a 16 bit depth layout: their pages kept, any other drawing or upload there forgets them), tested
  GREATER or GEQUAL against that depth target without writing it, is drawn with no texture: each sample takes the depth's bits
  8-15 at the sample itself (the depth target's float copy, as the native depth test reads it) through the CLUT (a texture of
  its 512 halves, the 8 bit indexes' CSA added), and is tested against its own depth by GL. At 1x it isn't used (the old way
  is exact there). What it rests on, the index being the depth's bits 8-15 at the same pixel, is the software GS's
  (TWIN_FILTER_PROBE: every filtered pixel; check_windows.py's "colour filter" line runs it). $TWIN_HW_DEPTH_INDEX=0 draws
  the filter as before, at the PS2's pixels. Measured (2026-11-01): the beach looking up at Crash and Aku Aku at 4x, steps of
  the PS2's pixel 19.2 -> 1.0 per 1000 PS2 pixels; check_windows' 25 pictures (4x): 20 change, 23,000 to 25,000 pixels each
  (0.55%), along the outlines but for 0 to 13 a picture at depth edges between like colours (a dark leaf over the olive
  behind, leaves over leaves, an outline just past the picture's top); the same 20 against PCSX2 drawing the same GS commands
  at 4x: mean difference 0.73 -> 0.60, pixels over 16 0.64% -> 0.32%, steps 11 -> 0.8 (PCSX2's 0.5 to 1.0).
- Scenes drawn into narrow buffers (TargetFor's nextScene_): a draw writing its depth into a buffer narrower than the screen
  draws a scene there (bossarea's sky, at half the PS2's resolution into 2800/4, then laid over the frame), not an effect: the
  buffer and its depth buffer are made at the scale (made again if native scaling made them at the PS2's resolution; a buffer
  drawn with a depth buffer of another scale has the lower one made the scale's), as PCSX2 draws them. The effects' buffers
  (the copies of the frame, the shadow volumes, which test the depth without writing it) stay at the PS2's resolution. A
  texture read with the nearest texels at UV coordinates starts at texel 0 (its region was the bilinear filter's, a texel
  before), so the sprites laying the sky read it from its buffer. Logged once ("a buffer narrower than the screen drawn as a
  scene"); check_windows.py's "scene buffers" line (bossarea at 4x: steps of the PS2's pixel 15.0 -> 0.7).
  $TWIN_HW_SMALL_SCENES=0 draws them as before.
- Mipmaps' level of detail at the scale (LodAtScale, the shader's lodBias): the GS works a mipmapped texture's level out from
  Q, (log2(1/|Q|) << L) + K, for the PS2's pixels; drawn at Nx every pixel took the level a pixel N times larger takes, the
  small levels the PS2 shows at 1x: textures softer than PCSX2's, which picks its level at the resolution it draws. Now
  log2(N) less (none at 1x), the level the PS2 drawing at that resolution would pick; a constant level of detail (LCM)
  stays the game's. (First taken for the characters' black lines' cause, wrongly: Cortex's face's texture is mipmapped but
  its K puts it at level 0 there; their cause is the next.) $TWIN_HW_LOD_AT_SCALE=0 picks the PS2's pixels' level as before.
- Primitives thinner than a PS2 pixel (HwGs::Draw's rectangle, ThinPrimitives): the rectangle a primitive draws in was the
  GS's pixels it covers at 1x, from the ceiling of its start, and a primitive with none (thinner than a pixel, between two of
  the GS's sample points) was left out at any scale. At 1x right (the PS2 draws none of it); at a higher scale it covers some
  of the scale's samples, and with it left out what's behind it shows: the characters' thin black and dark red lines along
  their parts' edges (Cortex's nose's side and cheeks while he talks, #15's screenshot, Crash's eyes; their faces' meshes
  have triangles a fraction of a pixel wide along those edges, over the black moustache and hair and the mouth's red behind),
  coming and going with the animation and the camera, at every scale above 1x, the pack's or not; PCSX2, drawing at its
  scale, never showed them. Now, above 1x, the rectangle reaches every pixel the primitive's box starts in (at most a pixel
  more on its left and top). Found 2026-11-01: the port's own GS commands of a frame with the line drawn by PCSX2 at 8x had
  none; the line's triangles (a 0.9 square pixel one from x 239.44 to 239.88) covered no integer x.
  $TWIN_HW_THIN_PRIMITIVES=0 leaves them out as before.
- Known differences: GL's float blending and filtering (1/255 steps against the GS's truncations); 16 bit frames blended under a
  stencil DATE keep 8 bits until they're read.
- Not a difference: the Iceberg Lab's coloured lights tint the characters' bright parts, Crash's chin and chest pale green
  (levels\ice\hub\labext) and Cortex's face green (psycho's cutscene), as the retail game does (#15: its executable in
  PCSX2 2.9, Crash in the same idle pose, the patch's mean colour 124, 190, 148 against the port's 123, 191, 149; the PS2
  build's Cortex at least as green). check_windows.py's "skinned light" keeps it so.
- The characters' models and textures at a higher scale (2026-10-31, against PCSX2 2.9 at the beach's start,
  `run_pcsx2.py --screenshot p9.2`, Crash in the port's frame 600's pose): at 8x its hardware renderer's picture and the port's
  were 3.7 of 255 apart on average around Crash, on the outline's pixels, with the player's HD texture pack (its 18 character
  textures) as without; at 1x its software renderer and the port's (hardware or software) have Crash's mean colour within 0.7
  and 5 apart on average (the dither's pattern). Their polygons are the PS2's: the game never sets PRIM's AA1 (the GS's
  antialiased edges: none of 3.6 million primitives on the beach, the Iceberg Lab and psycho's cutscene) and none of 555
  million vertexes is out of the GS's range (Q zero, ST or Q not finite, ST/Q past 64 widths: `TWIN_GS_VERTEX_CHECK`; the only
  far places, labext's, are past the scissor). The outline's differences were not all the pose's: against the sand they're
  the half PS2 pixel the whole picture lies off PCSX2's, against far things the colour filter's blocks (below).
- A difference: the characters' shadows (the beach's, the Iceberg Lab's: shadow volumes drawn into a half resolution buffer,
  256 x 256, tested against a depth made from the scene's at the PS2's resolution, then laid over the frame filtered) stay at
  the PS2's resolution under native scaling: at 1x as the PS2's (PCSX2's software renderer has the same soft 2 x 2 blocks), at
  8x that shadow made larger, where PCSX2's hardware renderer (its native scaling off by default) draws the chain at its
  scale, a sharp outline. Drawing it at the scale needs the depth copy made on the GPU (it's read from local memory now:
  scaled up from there the volumes' test leaves streaks, `TWIN_HW_NATIVE_SMALL=0`).
- Checked (1.2.0, THE LIST 39): TwinsanityEvolved 0.9.1 removed a dark Crash-shaped shadow under the 10th Dimension's water
  while he drowns, its own renderer's (its stencil lost the casters' mark where the water, which writes no depth, was drawn over
  Crash). Ours draws what the GS draws: Crash drowned in alta (`$TWIN_HOP=idle,wait2,fly:100:2:-70:20` with
  `$TWIN_SWEEP_GAME_TIME`, two runs identical frame for frame), the same frame with the characters' shadows and without
  (`$TWIN_NO_SHADOWS`): while he sinks the only difference is 17 x 23 PS2 pixels at the picture's bottom centre (where his
  splash is), 25 of 255 darker a channel; the frame's GS dump with and without them played by PCSX2 differs on the same 17 x 23
  pixels by the same 25. The PS2's GS draws those commands as we do; nothing to change.

### Ant Agony's walls and doors (#32, THE LIST 16): the cause (2026-11-07)

NormanTheFox's screenshots (1.1.0 at 3x, FXAA, dithering off, a GTX 1650) and the user's own of 2026-11-07 (the 1.2.0 test
copy at 3x, crashlogs/issue32/antagony_utente_2026-11-07): every metal door's middle panel streaked horizontally in colours with
a zigzag, the floors smeared, the lava cracks in blocks; right as the level starts, wrong once Crash walks on and never right
again; PCSX2 right throughout. A first measurement (corea started on its own, Crash put in front of the first door at frame
1060) found nothing: the defect needs play before Ant Agony.

The cause, the hardware renderer's: Ant Agony's walls, floors and doors are 32 bit textures (256x64, 256x128 and 256x256)
that the game uploads every frame to 2800/4, where it also draws the characters' shadows (a 16 bit S buffer). A texture whose
address, width and colour class are a target's was sampled straight from that target (a buffer drawn on the GPU). bossarea's
sky is drawn at 2800/4 as 32 bit colour ("Scenes drawn into narrow buffers", above) and that target stays for the rest of the
session; after it, Ant Agony's textures were read from it. Their pages were loaded from memory into it first, the right
texels (some 100,000 reads checked against local memory, all alike), but the target's edge clamps while the walls' and doors'
textures repeat past their edges, and the test meant for that ("a repeating texture whose coordinates stay inside it")
compared the region after it had been widened to the whole texture, so it always passed: each row's edge texel streaked
across the repeats. Those that stayed inside were sampled at the target's scale, blocks of 3x3 replicated texels (the
cracks' steps) instead of the GS's filter. The test comes with HAB3X's renderer (the import, cabe032): 1.0.19 has it, and
with 1.1.0's five drawing switches off the broken frames are the same. Not the texture pack (there it replaces only the HUD's
textures). In the players' sessions the target came from earlier play; what made their level's start right isn't measured.

Measured (compare/tools/antagony.py: the same run drawn by the hardware renderer at 3x and by the software GS at 1x,
`$TWIN_FIXED_TIME`, compared on the PS2's pixels in blocks of 8 x 8): bossarea for 5 s, then corea loaded in the same run (the
hop's `load:` step), Crash walked by the pad script. Before the fix, 31 to 173 blocks more than 40 of 255 apart in every
frame to the run's end, the worst 105 to 157; with no texture read from a target (`$TWIN_HW_NO_TARGET_TEXTURES`) none over 40;
corea started on its own, the worst about 50 (Crash's shadow, the edges) and no wall. The port's GS dump of a broken frame
(2000) replayed by PCSX2's software renderer: the same picture from the old build's dump and the fixed one's (the commands
right), the old frame 3.89 of 255 from it (1.98% of the pixels more than 16 apart), the fixed one 2.86 (0.82%).

The fix (hwgs.cpp): a target is a texture's source only where it drew (`Target::drawn`: set by MarkDrawn, cleared where
memory's content is loaded into it, where the CPU writes and where another target draws) and, for a repeating texture, only
when its coordinates stay inside it (the region before it's widened). Elsewhere the texture is decoded from memory's copy, as
when no target is there. The same run after it: the worst 23 to 58, as corea started on its own. check_windows.py's scenario
"Ant Agony's walls after bossarea's sky" (bossarea, then corea by the hop's load step, `$TWIN_HW_SOURCE_TRACE`): the sky still
read from its buffer, the walls never (it fails on the old test).

### The characters' stepped edges at a higher scale: the causes (2026-11-01)

Players saw every character's outline stepped at a high resolution (#15, #19, #24; the player's own fullscreen 2560x1440 at
8x): blocks the size of a PS2 pixel, darker or the wrong colour, where a character stands against far things (the sky, far
rocks). Reproduced on the player's PC in real runs ($TWIN_PAD_SCRIPT: the window as shown and the picture drawn at the same
frames) and measured on the bench (compare_models.py; WINDOWS.md, "The characters against PCSX2") against the retail game in
PCSX2 2.9 at the beach with the camera looking up and in the first boss's cutscene, level by level. Steps of the PS2's pixel's
size below are edges exactly a PS2 pixel long on the PS2 pixels' grid, per 1000 PS2 pixels over the grid's other positions:

- The presentation: not it. The window is the drawn picture made its size (within 0.5 of 255 on average at 8x, fullscreen or
  windowed, one thread or three, the player's settings or the defaults). At a higher scale the whole picture lies half a PS2
  pixel up and left of PCSX2's (our first sample of a PS2 pixel is the GS's sample point, PCSX2's are the pixels' centres): a
  shift of everything, no step; the bench fits it before measuring.
- The drawing: the game's colour filter (renderer/effects.cpp: CopyDepthBuffer copies the depth buffer as 16 bit depth texels
  in strips into a 16 bit buffer, 2800/8 and 3000/8 for its upper and lower halves; FilterColours reads that buffer as 8 bit
  indexes, PSMT8H, through the chunk's palette and blends the colour, (Cd - Cs) * As + Cs, over the pixels whose depth is under
  0x10000, the far ones). Each index is the depth's bits 8-15 at the same pixel: the software GS's every filtered pixel on the
  beach (`TWIN_FILTER_PROBE`: 262144 of 262144, the camera looking up). The hardware renderer made the copy from local
  memory, read back from the upscaled depth at each block's first sample, and tested the filter's depth there too ("Upscaled
  effects reading textures made
  at the PS2's resolution", above): the filter was the PS2's pixels' whatever the scale. Where a near thing's block met a far
  one's, far samples took the near one's colour and near samples the far one's: the blocks players saw; and the sky, whose
  depth changes across it, was coloured in blocks. On
  the port's own GS commands drawn by both at 8x (its frame written as a GS dump, PCSX2 replaying it): steps 20.0 at the beach,
  24.2 with Cortex against the sky, 27.9 at the party, 2.0 with Crash close up indoors (no far pixels), PCSX2 1.9 to 2.5; once
  the half pixel is fitted the pictures are otherwise 0.3 to 1.1 of 255 apart. The cause of the stepped edges; drawn now
  at the scale ("The game's colour filter at the scale", above).
- The drawing, a second one, found once the filter was the scale's (the cutscene still had steps 16.6 with Cortex and 8.8 at
  the party against PCSX2's 1.9 to 2.5, on the clouds, the far island and the horizon): bossarea's sky. The game draws it at
  half the PS2's resolution, with its depth, into a 256 wide buffer (2800/4) and lays it over the frame with sprites reading
  the nearest texels; native scaling kept that buffer at the PS2's resolution, and the sprites read it decoded from memory
  (the region worked out for a bilinear filter started a texel before 0, so the repeating texture's whole 1024 rows, more than
  the buffer). The sky was blocks of two PS2 pixels at any scale, the party's stepped clouds; drawn now at the scale ("Scenes
  drawn into narrow buffers", above): steps 1.8 and 2.2.
- Other passes on the characters: none at the PS2's resolution. Their draws test the depth GEQUAL; some meshes have a second
  blended pass over the same strips that doesn't write the depth: exact here, both passes writing and comparing the depth the
  shader computes from the same vertexes (Crash close up: 0.3 of 255 from PCSX2 on the same commands, no flicker).
- The characters' shadows (the shadow volumes in 2800/4 and 2c00/4, kept at the PS2's resolution by the native scaling): a
  difference of their own, not the outlines'. On the beach's start at 8x on the same commands, around Crash's feet 1.5 of 255
  from PCSX2 (whose shadow's edge is sharp, ours soft and half the PS2's resolution); drawn at the scale
  (`TWIN_HW_NATIVE_SMALL=0`) 2.1, the volumes' test against the depth copy made from memory leaving streaks. Making them
  PCSX2's needs the depth copy made on the GPU from the upscaled depth target (as the colour filter's index now is).
- The geometry: the port's GS commands are the retail game's. In the first boss's cutscene the two frames draw the same ways,
  85 and 85, 89 and 90, 125 and 125 draws, their primitives within 7 of 18,000 to 34,000 (one 4-primitive strip's alpha test
  reference differs with the test off), no antialiased (AA1) primitive in either; at the beach the counts are a few in a
  hundred apart (the idle pose and a seagull).
- The texture pack, dithering, SSAA and the window's size change none of it: the same steps with the defaults at 4x.
- The characters' sharp-edged patches (found after the edges with `compare_models.py --scan`, Crash all round: the player saw
  them in the port only, since 1.0, and PCSX2 drew them too from the port's own GS commands, so they were in the commands): on
  Crash's snout two yellow triangles, on his chest a pale blue strip, on his arm a light rectangle; Cortex and the cutscenes'
  characters alike. The port's and the retail game's GS dumps, strip by strip: the same strips, the same ADC bits, the same
  triangles (2,970 of Crash's), but in nearly every 38 vertex strip the last two vertices' colours (and alpha, 127 for 119)
  were another batch's. VU1's skinned meshes' program (XGKICK at 0x34e) writes those two colours, its packet's quadwords 110
  and 113 of 115, 2 and 5 cycles after its XGKICK (`$TWIN_VU_KICK_WRITES` with `TWIN_VU1=interpret` says each such write):
  the PS2's GIF reads them, still on its way through the packet; the port read the packet at the XGKICK. Read now when the
  GIF has it (the table above, "XGKICK"): in the first boss's cutscene every mesh's colours the retail game's (the skinned
  ones' strips with colours more than 8 apart: Cortex 133 of 138 -> 0, Crash 118 of 118 -> 0, the party's three 118, 29 and
  27 -> 0), the interpreter and the translated programs the same (`TWIN_VU1=verify`); the only program writing a packet after
  its XGKICK there and on the beach.

Replays (beach, flyover; 1x GPU picture against the software GS, mean per channel): beach 0.87-0.92, flyover 1.1.

Debugging switches: `TWIN_GS_DUMP_AT=PRIM,BP,BW,PSM,W,H[;...]` (a buffer's values from local memory just before a primitive,
into `$TWIN_HW_DUMP_DIR`), `TWIN_FILTER_PROBE` (with `TWIN_GS=software`: the colour filter's first three draws, how many of
the pixels it draws on have the depth's bits 8-15 as their index), `TWIN_HW_TRACE_PRIM=N` (a primitive's batch state and vertexes), `TWIN_HW_CONVERSION_TRACE=A,B`,
`TWIN_HW_DISPLAY_TRACE`, `TWIN_HW_TRACK=BP,X,Y` (a target pixel after each batch), `TWIN_HW_BATCH_LOG=N` (the batches drawn
for picture N, a line each: their target and its scale, the texture's target if any, filters, blend, depth), `TWIN_GS_VERTEX_CHECK`
(vertexes a GPU could take differently, counted), `TWIN_HW_STATS` (render passes by cause, conversions, copies), `TWIN_SCALE=N` (a headless run's internal scale), `TWIN_HW_MAX_TARGET=N` (targets with a side past N pixels refused: the software GS draws them), and the old behaviours:
`TWIN_HW_VERTEX_OFFSET=n` (256ths), `TWIN_HW_SMALL_VIEWPORT`, `TWIN_HW_NO_WRAPADD`, `TWIN_HW_NO_STENCIL_DATE`,
`TWIN_HW_NO_WIDE_LOAD`, `TWIN_HW_STAGED_UPLOADS`, `TWIN_HW_NO_SNAP`, `TWIN_HW_NO_NATIVE_DEPTH`, `TWIN_HW_DEPTH_INDEX=0` (the
colour filter at the PS2's pixels), `TWIN_HW_SMALL_SCENES=0` (scenes drawn into narrow buffers at the PS2's resolution),
`TWIN_VU1_KICK_AT_ONCE=1` (VU1's packets read at the XGKICK), `TWIN_HW_LOD_AT_SCALE=0` (mipmaps' levels the 1x ones),
`TWIN_HW_THIN_PRIMITIVES=0` (primitives thinner than a PS2 pixel left out); with the interpreter (`TWIN_VU1=interpret`),
`TWIN_VU_KICK_WRITES` (each instruction that writes into a packet after its XGKICK, once: where and how many cycles after).

### Replacement textures (PCSX2's packs)

`hw/replace.cpp`: `NativeGraphicsSetTexturePack(true)` reads `<settings folder>/textures/<serial>/replacements/**` (or
`$TWIN_TEXTURES_DIR/<serial>/replacements`; serials SLES-52568, SLUS-20909 and SLPM-65801, as the packs share the PAL disc's
hashes), PNG (SDL) and DDS (uncompressed, BC1-3; BC7 not yet), named as PCSX2 names them (native/research/HD_TEXTURES.md:
XXH3-64 of the texture's GS blocks or texels, the palette's hash, the packed TEX0/TEXA word; level 0 only, as with PCSX2's
mipmapping off). A replaced texture is sampled with the original's normalised coordinates, its mipmaps generated.
The UI's glyph images (`glyphs.h`: the menus' buttons) are neither replaced nor dumped: TEX0 still names the font they're drawn
over, whose replacement (the pack's whole font sheet) was drawn squeezed into each button.
`hw/xxhash.h` is xxHash 0.8.3 (BSD 2-Clause, Yann Collet). `$TWIN_HW_DUMP_TEXTURES=folder` writes the textures the draws read
whole under the same names (a pack's starting point). Checked against PCSX2 2.8.2's own dump of the PAL game (Metal renderer,
mipmapping off): 50 names alike, every texture of local memory our replays dumped that PCSX2's run reached (the one name not
alike is the frame read as a 1024x1024 texture, which PCSX2 takes from its render target). No pack is bundled.
