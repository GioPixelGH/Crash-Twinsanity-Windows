# Modding Crash Twinsanity for Windows

This guide is for people who make mods for Crash Twinsanity and for players who install them in this port. It covers what
the port reads, the game's files, the community's tools for them, the limits, and how to share a mod. Playing with mods is
also described, more briefly, in the players' guide ([PLAYING_WINDOWS.md](PLAYING_WINDOWS.md#mods)).

The port runs the PAL disc's game (SLES-52568) from your own disc image. A mod changes the game's data files: levels,
texts, pictures, sounds, music, movies. It can't change the game's program (see [Limits](#limits)).

## Your first mod

This example changes the main menu's "new game" into "new game, modded". It takes a few minutes and shows the whole path.

1. Copy the English texts out of your disc image. From PowerShell or the Command Prompt, in the game's folder:

       .\CrashTwinsanity.exe --extract=Language/Code/English.txt

   The file lands in `%APPDATA%\Crash Twinsanity\mods\Language\Code\English.txt` (or in the `User` folder beside the game,
   in portable mode).
2. Make the mod a folder of its own: create `%APPDATA%\Crash Twinsanity\mods\My first mod\`, and in it a `layer1` folder.
   Move the `Language` folder from step 1 into `layer1`, so the file is at
   `mods\My first mod\layer1\Language\Code\English.txt`.
3. In `mods\My first mod\`, create `ModCrateInfo.txt` with these lines:

       Name=My first mod
       Description=The main menu's first choice changed
       Author=You
       Version=v1
       Game=CrashTS

4. Open `English.txt` in a text editor. Each line is one text. Line 11 is `new game`: change it to `new game, modded` and
   save, keeping the file's encoding (Windows-1252) and its line ends.
5. Start the game. The main menu says "New game, modded". Your saves now go on a memory card of the mods' own (see
   [Saves](#saves)): your own saves are back as soon as no mod is on.

The log (`log.txt` in the settings folder) says which mod each file came from:
`mods: the archive's /LANGUAGE/CODE/ENGLISH.TXT from ...\mods\My first mod\layer1\Language\Code\English.txt (My first mod)`.
To play the original again, turn the mod off in **Options > Mods**, or delete its folder.

What you made is a **Mod Crate**, the format of the community's Crate Mod Loader: zip the `My first mod` folder and it can
be shared, installed in this port, or built into a disc image by Crate Mod Loader for the PS2 and PCSX2.

## What the port reads

Everything is in the settings folder's `mods` folder: `%APPDATA%\Crash Twinsanity\mods` (paste it in Explorer's address
bar; **Options > Mods > Open the mods folder** opens it). Each mod is a folder of its own in it:

- **A Mod Crate**: `ModCrateInfo.txt` (its `Name`, `Description`, `Author`, `Version`, and `Game=CrashTS`), `layer0\` with
  files of the disc (`layer0\CRASH6\MUSIC.MB`, `layer0\FMV\H01_A.PSS`) and `layer1\` with files of the game's archive
  (`layer1\Levels\Earth\Hub\beach.rm2`, `layer1\Language\Code\English.txt`). A crate unzipped into a folder one level down
  (`mods\Name\Name\ModCrateInfo.txt`) is found too. A crate made for another game (`Game=` something else) is refused.
- **A folder of files laid out as the game's**: the disc's (`CRASH6\`, `FMV\`) and the archive's (`Levels\`, `Language\`,
  `Startup\`, `Extras\`) side by side: `mods\My mod\Levels\Earth\Hub\beach.rm2`.
- **Files straight in the mods folder** at their path (`mods\Levels\Earth\Hub\beach.rm2`), as before version 1.2.0: they
  count as one more mod, shown as "Files in the mods folder".

Names are matched in any case. A file a mod adds to the archive (a new level, `layer1\Levels\gpa01.rm2`) is added to the
game's list of files, as Crate Mod Loader does, so the mod's own levels can load it.

### Zips and changed disc images

A mod's `.zip`, or a whole changed disc image (`.iso`), can go straight in the mods folder: **Options > Mods > Import mods**
(or `.\CrashTwinsanity.exe --import-mods`) makes each a mod folder of the same name.

- A zip is unpacked as it is (a single folder at its top is left out, so a crate's `ModCrateInfo.txt` ends up in the mod's
  folder).
- A disc image is compared with your own disc, file by file and, for the archive (`CRASH6\CRASH.BD`), entry by entry: only
  what differs is kept, laid out as a Mod Crate. A 4 GB image becomes the few hundred megabytes the mod changes, and it can
  be combined with other mods.

The zip or image can be deleted once imported. A mod shipped as a patch (`.xdelta`, `.ppf`, `.bps`) isn't applied by the
port: apply it to a copy of your disc image with a patcher for that format, then import the resulting image. An import
takes a minute or two for a disc image; the imported mods are listed in **Options > Mods** at once and used from the next
start of the game.

### Order and switches

Where two mods change the same file, the first one wins. **Options > Mods** lists every mod: each is off, or on in its place
(`on · 1` is the first). A new mod comes first and on. The choices are kept in `mods.txt` in the settings folder (one line
a mod: `on` or `off`, a tab, the mod's folder name; `*` stands for the files straight in the mods folder), and they take
effect at the next start: the mods are looked at once, when the game starts.

### Saves

While any mod is on, the saves go on a memory card of the mods' own: a folder `memorycard-mods-` followed by the mods'
names (`memorycard-mods-My first mod`) in the settings folder, so a mod's levels and progress never end up in your own saves.
Each set of mods has its own card; with every mod off, the game uses your own card again.

### The log

At each start the log lists every mod (on, off or refused, its disc and archive files, how many are new to the archive),
then each file as it's used, and the card the saves go on. A bug report about a mod should include the log.

## The game's files

The disc (PAL, SLES-52568):

| File | What it is |
|---|---|
| `SLES_525.68` | The game's program (the PS2's). This port has its own: a mod's changed program isn't used |
| `SYSTEM.CNF` | What the PS2 boots |
| `CRASH6\CRASH.BH`, `CRASH6\CRASH.BD` | The game's archive: its table (each file's name, start and size) and its data. Almost everything the game loads is a file in it (below) |
| `CRASH6\MUSIC.MH`, `CRASH6\MUSIC.MB` | The music: its header and its bank |
| `CRASH6\ENGLISH.MH` / `.MB` (and `FRENCH`, `GERMAN`, `ITALIAN`, `SPANISH`) | Each language's voices |
| `CRASH6\SYS\*.IRX`, `IOPRP300.IMG` | The PS2's I/O processor modules (the port runs them as the PS2 would: a mod's aren't used) |
| `FMV\*.PSS` | The movies (MPEG-2 video with the PS2's ADPCM sound), `FMV\BONUS\` the extras' |

The archive (`CRASH6\CRASH.BD`, by the paths in `CRASH.BH`):

| Files | What they are |
|---|---|
| `Levels\...\*.rm2` | A level chunk's resources: its textures, materials, models and skins, and its code section (the objects' scripts, its sound effects and voices) |
| `Levels\...\*.sm2` | The same chunk's scenery: its meshes, the instances placed in it, its lights, collision, particles and moving scenery |
| `Startup\Default.rm2` | What every level shares (Crash, the common objects, their sounds) |
| `Language\Code\<Language>.txt` | The game's texts, one a line, in Windows-1252 (`~` breaks a line; a few characters stand for the controller's buttons) |
| `Language\AgentLab\`, `Language\Credits\` | The hints' texts and the credits' |
| `*.psm` (`Language\Loading\`, `Language\Titles\`, `Extras\`) | Pictures: loading screens, the levels' titles, the extras' galleries |
| `Startup\Fonts\*.psf` | The fonts |

`.\CrashTwinsanity.exe --list-files` lists every file of the archive with its size; `--extract=PATH` copies one (or one
of the disc's) into the mods folder to start from.

## The community's tools

These are other people's projects, each under its own licence; the port uses none of their code.

- [TT-Lab](https://github.com/Smartkin/TT-Lab) by Smartkin (GPL-3.0): an editor for the whole game, its levels, scripts and
  resources, that builds the files back.
- [twinsanity-editor](https://github.com/Smartkin/twinsanity-editor) by Smartkin, from NeoKesha's original (MIT): opens and
  rebuilds the archive (BD/BH) and edits levels (RM2/SM2).
- [twinstudio](https://github.com/Smartkin/twinstudio) by Smartkin (no licence stated): tools for the game's other formats.
- [Crate Mod Loader](https://beyondtwinsanity.com/modloader/), by the Beyond Twinsanity community: installs Mod Crates into
  a disc image for the PS2 and PCSX2. Its mods list: [beyondtwinsanity.com/mods](https://beyondtwinsanity.com/mods/).

## Limits

- **The game's program can't be changed.** The port runs its own translation of the game's code, and reads the game's
  data from your disc's program. A mod's `SLES_525.68`, PCSX2 patches (`.pnach`) and Crate Mod Loader's game settings
  (`ModCrateSettings.txt`) don't apply. The log says what a mod's program changes. One example: Padoru Pandemonium's
  program gives the script command 137 a new meaning (it sets the game's speed scales), so its scripts that use it don't do
  the same here.
- **The disc's own I/O processor modules and boot file** (`CRASH6\SYS\`, `SYSTEM.CNF`) are always the disc's.
- **Sizes**: a disc file of a mod must be under 1 GB, and the archive with every mod's archive files should stay under
  2 GB (the PS2's sound streaming counts in signed 32 bits; the log warns when it's passed).
- **Changes take effect at the next start.** The mods are looked at once, when the game starts.
- Crash Twinsanity Improved's fixes are options of the port already (**Options > Gameplay > Bug fixes**), and the cutscene
  skip is one too: their mod isn't needed.

## Sharing a mod

Share it as a Mod Crate zip: `ModCrateInfo.txt`, `layer0\` and `layer1\` with only the files your mod changes or adds.
That one zip installs in this port (unzipped into the mods folder, or imported), in Crate Mod Loader for the PS2 and
PCSX2, and in other ports that read crates. Never share a disc image or the game's files your mod doesn't change: players
use their own disc. A preview picture (`ModCrateIcon.png`) and notes for players are welcome in the zip.

The port itself stays the original game by default: added or restored content (new levels, cut content, other games'
characters) is something players install as a mod, never part of the port.
