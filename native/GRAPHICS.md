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
| `settings.h` | The UI's graphics settings: `NativeGraphicsSetRenderer(0 software, 1 hardware)` (default hardware; `$TWIN_GS=software`), `NativeGraphicsSetDither(0 none, 1 PS2 pixels, 2 fine)`, `NativeGraphicsSetTextureFiltering(0 the game's, 1 bilinear, 2 trilinear + 16x anisotropic)`, `NativeGraphicsSetTextureUpscale(0-2)` (not yet: it refuses anything but off), `NativeGraphicsSetTexturePack(bool)` (PCSX2's packs, see below), `NativeGraphicsSetAntiAliasing(0 off, 1 FXAA; 2 MSAA returns false)`, `NativeGraphicsSetVSync(bool)`. The internal resolution is `NativeGraphicsSetResolution`'s: 1-8 (software 1-4), 0 = match the window |
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
| XGKICK | The GIF reads VU1's memory over the next cycles | The whole packet at once | None for programs that double buffer their output (the game's do) |
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
  Buffers narrower than the screen (the effects' half resolution buffers) stay at the PS2's resolution (native scaling).
- Local memory and the targets: each 8 KB page records where its newest content is; a target is brought up to date from local
  memory before the GPU uses it, local memory from a target (at the PS2's resolution) before the CPU side reads or writes it
  (transfers, the CLUT, texture decoding, the display read, a software draw).
- Textures: a target's own buffer in its own layout is sampled from the target; any other is decoded by the software GS's decoder
  (every format, the CLUT, TEXA) and uploaded by 8x8 tiles as the draws reach them.
- The pixel pipeline in the shader as the GS defines it (TFX, fog, alpha test, FBA, 16 bit frames' dither and truncation);
  the alpha test's AFAIL by a second pass; depth as a 32 bit float written from the shader; blending by GL's dual source
  blending when it can make the GS's blend exactly, else in the shader against a copy of the target (batches split where
  primitives overlap): partial frame masks, the destination's alpha as the factor, FIX over 0x80, COLCLAMP off, PABE and
  16 bit frames; the destination alpha test by a stencil pass when the draw can't change the alpha bit, else in the shader.
- The PCRTC: the shown buffer's target (both circuits merged on the GPU as the PCRTC merges them) is the picture.
- Left to the software GS: a frame in a Z format (the game's depth buffer copy into a Z32 frame).
- Exactness the shadow volumes (the beach's character shadows) need: vertexes on the rasteriser's sixteenth of a pixel grid
  and a viewport far bigger than the target (no snapping, no clipping: a sliver of a primitive keeps its pixels); wrapping
  additive blends' parts summed as their signed values in 256ths (exact in the half float sums); a 32 bit depth buffer with
  its values in the top half kept reversed (exact there); depth truncated as the GS truncates it. 1x: the shadow pass's
  stencil counts match the software GS's but for one texel (its depth copy's).
- Upscaled effects reading textures made at the PS2's resolution: a draw whose texture is decoded from memory read back from
  an upscaled target, and which tests (without writing) an upscaled depth buffer, makes its depth test and its texture read
  at each block's first sample (the GS's sample point), as the texture was made there. Without it the beach's depth haze
  (a palette lookup of the depth's copy) drew a bright line where the depth crosses its threshold: a circle around Crash.
- Known differences: GL's float blending and filtering (1/255 steps against the GS's truncations); 16 bit frames blended under a
  stencil DATE keep 8 bits until they're read.

Replays (beach, flyover; 1x GPU picture against the software GS, mean per channel): beach 0.87-0.92, flyover 1.1.

Debugging switches: `TWIN_GS_DUMP_AT=PRIM,BP,BW,PSM,W,H[;...]` (a buffer's values from local memory just before a primitive,
into `$TWIN_HW_DUMP_DIR`), `TWIN_HW_TRACE_PRIM=N` (a primitive's batch state and vertexes), `TWIN_HW_CONVERSION_TRACE=A,B`,
`TWIN_HW_DISPLAY_TRACE`, `TWIN_HW_TRACK=BP,X,Y` (a target pixel after each batch), `TWIN_HW_STATS` (render passes by cause, conversions, copies), and the old behaviours:
`TWIN_HW_VERTEX_OFFSET=n` (256ths), `TWIN_HW_SMALL_VIEWPORT`, `TWIN_HW_NO_WRAPADD`, `TWIN_HW_NO_STENCIL_DATE`,
`TWIN_HW_NO_WIDE_LOAD`, `TWIN_HW_STAGED_UPLOADS`, `TWIN_HW_NO_SNAP`, `TWIN_HW_NO_NATIVE_DEPTH`.

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
