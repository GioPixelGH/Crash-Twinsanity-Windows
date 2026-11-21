# Third-party notices

What this repository and its releases contain that isn't this port's own work, under which licence, and where the licence's
text is. Each release carries the same texts in its `licenses` folder, beside the program, with this list. The people behind
them are thanked in the README's [Credits](README.md#credits).

## In the program

| What | Licence | Its text |
| --- | --- | --- |
| [SDL 3](https://github.com/libsdl-org/SDL) (windows, input, sound) | zlib | `licenses/SDL3-LICENSE.txt` in the release (SDL isn't in this repository: the build takes it from SDL's own release) |
| [glslang 16.6.0](https://github.com/KhronosGroup/glslang) by the Khronos Group (the Vulkan renderer's shaders made from the OpenGL renderer's while the game runs) | 3-Clause BSD, and for some parts 2-Clause BSD, MIT and Apache 2.0 | `licenses/glslang-LICENSE.txt` in the release (glslang isn't in this repository: the build takes it from its own release) |
| [FFmpeg 9.0.2](https://ffmpeg.org/) (libavcodec and libavutil, built with only the MPEG-2 video decoder, for the game's movies) | LGPL 2.1 or later | [native/third_party/ffmpeg](native/third_party/ffmpeg) (how it's built, and the source it's built from) |
| [SMAA](https://github.com/iryoku/smaa) by Jorge Jimenez, Jose I. Echevarria, Belen Masia, Fernando Navarro and Diego Gutierrez (the SMAA anti-aliasing) | MIT | [native/third_party/smaa/LICENSE.txt](native/third_party/smaa/LICENSE.txt) |
| [stb_truetype](https://github.com/nothings/stb) by Sean Barrett (the options screen's text; one fix of ours, marked in the file) | public domain or MIT | [native/third_party/stb/LICENSE.txt](native/third_party/stb/LICENSE.txt) |
| [TwinsanityEvolved](https://github.com/menaman123/TwinsanityEvolved)'s engine, NativeEE (the disc image's CRC-32 and SHA-1, a text bank's language, the mods' zips and their decompression) | MIT | [native/third_party/twinsanityevolved/LICENSE-NativeEE.txt](native/third_party/twinsanityevolved/LICENSE-NativeEE.txt) |
| TwinsanityEvolved's region map tool (native/tools/region_map.py: where the North American disc's executable has each piece of the PAL one's data) | MIT | [native/third_party/twinsanityevolved/LICENSE-TwinsanityEvolved.txt](native/third_party/twinsanityevolved/LICENSE-TwinsanityEvolved.txt) |
| TwinsanityEvolved's cutscene skip and its census of the game's cutscenes, used with menaman123's permission ([TwinsanityEvolved#19](https://github.com/menaman123/TwinsanityEvolved/issues/19)) | MIT | [native/third_party/twinsanityevolved/LICENSE-TwinsanityEvolved.txt](native/third_party/twinsanityevolved/LICENSE-TwinsanityEvolved.txt) |

[native/third_party/twinsanityevolved/README.md](native/third_party/twinsanityevolved/README.md) lists each file taken from
TwinsanityEvolved and the file of ours it became; every one keeps its copyright notice.

## Used with their authors' permission

| What | Licence | The permission |
| --- | --- | --- |
| [Crash Twinsanity Improved](https://github.com/AlexMollard/Crash-Twinsanity-Improved) by AlexMollard, a mod of the PS2 game: the optional bug fixes (its `mod/elf_patches.txt` and `mod/levels/*.ops`, remade in C++ in `src/platform/native/fixes.cpp` and `src/platform/native/levelrecipes.cpp`) and the cutscene skip's prompt (its `mod/text.txt`) | none: all rights stay with its authors | AlexMollard's, on [Crash-Twinsanity-Improved#5](https://github.com/AlexMollard/Crash-Twinsanity-Improved/issues/5) (2026-11-10): "feel free to use anything you find in this repo" |

The 21:9 shape is TechieSaru's fix, found in the mod's PCSX2 patches: the 21:9 width (2.333) written where the game keeps
16:9's. TechieSaru is credited in the README and in the program's Credits page, and wasn't asked separately.

## Fonts

| What | Licence | Its text |
| --- | --- | --- |
| [Luckiest Guy](https://fonts.google.com/specimen/Luckiest+Guy) by Astigmatic | Apache 2.0 | [native/third_party/fonts/LICENSE-LuckiestGuy.txt](native/third_party/fonts/LICENSE-LuckiestGuy.txt) |
| [Fredoka](https://fonts.google.com/specimen/Fredoka) by the Fredoka Project Authors | SIL Open Font Licence 1.1 | [native/third_party/fonts/OFL-Fredoka.txt](native/third_party/fonts/OFL-Fredoka.txt) |

## The game's code

The game's code is [Smartkin's decompilation](https://github.com/Smartkin/twinsanity-reversed), on
[HAB3X's port for Mac](https://github.com/HAB3X/Crash-Twinsanity-Mac). Both are published without a licence, so all their
rights stay with their authors: the README's [Licence](README.md#licence) and [LICENSE](LICENSE) say how this repository
handles that. This repository's own changes are under the MIT licence ([LICENSE](LICENSE)).

No part of the game itself (its executable, data or disc image) is in this repository or in a release.
