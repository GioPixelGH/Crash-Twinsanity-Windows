# TwinsanityEvolved's code in this port

[TwinsanityEvolved](https://github.com/menaman123/TwinsanityEvolved) by menaman123 is another PC port of Crash Twinsanity. Parts
of it are under the MIT licence: its own code (platform/, tools/, docs/ and the build files: LICENSE-TwinsanityEvolved.txt)
and its engine, NativeEE (engine/: LICENSE-NativeEE.txt). Its game/ tree carries the decompilation's terms and isn't MIT.

What this port takes from it, each file keeping its copyright notice:

| Ours | Theirs | Licence |
|---|---|---|
| src/platform/native/discimagehash.h, .cpp (the disc image's CRC-32 and SHA-1) | engine/app/host/disc_image_hash.h, .cpp | NativeEE's MIT |
| src/platform/native/ui/languageprobes.h (a text bank's language by its words) | engine/app/host/language_probes.h | NativeEE's MIT |
| src/platform/native/modzip.h, .cpp (the mods' zips read: stored and deflated entries, ZIP64, every entry's CRC-32 checked; its file layer and CRC-32 ours, modzipio.h) | engine/app/host/mod_import/zip_reader.h, .cpp | NativeEE's MIT |
| src/platform/native/modinflate.h, .cpp (DEFLATE decoded for the zips) | engine/app/host/mod_import/inflate.h, .cpp | NativeEE's MIT |

With menaman123's permission (menaman123/TwinsanityEvolved#19, 2026-11-06: "Sure! Sounds good to me!"), three files of its
game/ tree and fixtures/ are used here under its MIT licence (LICENSE-TwinsanityEvolved.txt), and only these three:

| Ours | Theirs | Licence |
|---|---|---|
| src/platform/native/cutsceneskip.h, .cpp (the cutscene skip of the scenes no skip rule ends: their waits ended in their order; the changes for this port marked "Ours") | game/include/game/cutsceneskip.h, game/src/game/cutsceneskip.cpp | TwinsanityEvolved's MIT, by permission |
| cutscene_skip_pal.json here (the census of the disc's scripted cutscenes, each by its site and its graphs: native/tools/cutscene_skip_table.py makes src/platform/native/cutsceneskiptable.h of it, with this port's own checks) | fixtures/judges/cutscene_skip_pal.json | TwinsanityEvolved's MIT, by permission |

The rest the skip needs in the game's code (the agents noted where behaviours start and messages go, the frames passed in
motion.cpp, the skip button in the game controller) is this port's own, after the method its cutsceneskip.h describes.
