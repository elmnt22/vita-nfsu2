# Need for Speed Underground 2 — Vita Edition

A native PlayStation Vita port of **Need for Speed Underground 2**. It is not
emulation and not a wrapper: the engine is rebuilt in C and runs the PC
game's own data on the console. That covers the city, cars, menus, HUD,
speech, music and movies.

The goal is to play like the real game. Menus, HUD, sounds, handling, camera
and controls are rebuilt 1:1 from the shipped game data and from reverse
engineering of the PC executable (`SPEED2.EXE` v1.2). Nothing is redesigned.

> **Status: in development.** Builds are test builds and may crash or show
> glitches. Many features are confirmed on hardware. The newest ones have so
> far only been checked in the Windows simulator of the Vita build. Please
> report problems with the log files (see [Reporting problems](#reporting-problems)).

> **No game data is included.** This repository has no EA assets and no EA
> code. You need your own legally owned copy of the PC game. This project is
> not affiliated with, authorized or endorsed by Electronic Arts.

Disclosure: Whilst I know a bit of programming and coding, I used AI to help accelerate areas of this port that otherwise would have taken me forever. AI used was Claude Opus.

---

## Contents

- [What's in the port](#whats-in-the-port)
- [What you need](#what-you-need)
- [Installation](#installation)
- [NFSU2 Vita Prep Tool (PC)](#nfsu2-vita-prep-tool-pc)
- [Bringing over a PC save](#bringing-over-a-pc-save)
- [Settings](#settings)
- [Reporting problems](#reporting-problems)
- [Building from source](#building-from-source)
- [Repository layout](#repository-layout)
- [Credits](#credits)
- [License and legal](#license-and-legal)

---

## What's in the port

- **Career mode.** Free roam of Bayview, the career stages and lock walls,
  races and tournaments, sponsors, SMS messages, the world map and GPS,
  shops found in the city, magazine cover showcases, the crib, the
  status screens, and the career's movies and speech.
- **Quick Race.** Circuit, Sprint, Drag, Drift, Street X, URL, Lap Knockout
  and Free Run, with track and car select, AI opponents and the results
  screen.
- **Driving.** A port of the game's own vehicle handling code, with manual
  and automatic gearboxes, nitrous, turbo, drifting and the original chase
  camera.
- **AI and traffic.** Opponents and traffic run on ports of the game's racer
  and traffic logic.
- **Customization.** The real shop screens: Body Shop, Performance (with the
  dyno), Graphics (paint, vinyls, decals) and Specialties (neon, spinners,
  ICE and more). Includes the visual rating and the Test Track.
- **Front end and HUD.** Built from the game's own FEng menu packages and
  fonts: the main menu over the showroom, profiles and saving, Options,
  Controls, Jukebox and Credits.
- **Audio.** Per-car engine, turbo, shift, skid and collision sounds from the
  game's own sound banks, plus music and speech.
- **Graphics.** The original world shading, car reflections, each car's own
  ground shadow, bloom, regional fog, rain, particles, the speed and nitrous
  blur, and smackable props. The title and loading screens use widescreen
  art.
- **Trophies (optional).** 76 Vita trophies based on the RetroAchievements
  set for the game. Progress is kept in your profile. They can be turned off
  in [Settings](#settings).
- **Fast travel (optional).** A native version of the NFSU2FastTravel PC mod.
  When it is on, choosing **No** on the World Map's GPS prompt puts you at
  that place. Off by default; turn it on in [Settings](#settings).
- **World streaming.** The whole city streams in while you drive.
- **Languages.** The game uses the Vita's system language when the original
  game has it. That covers English, French, German, Italian, Spanish, Dutch,
  Swedish and Danish. Other languages fall back to English.

**Not in the port yet:** Online and LAN play.

---

## What you need

### On the PC side

- **Need for Speed Underground 2 for PC, patched to v1.2.** The port only
  reads the data folders, but it is built and tested against v1.2
  (`SPEED2.EXE` dated Feb 9 2005).
- A Windows 10/11 PC for the **NFSU2 Vita Prep Tool** (optional, but
  recommended: it checks your game files, puts them on the Vita, and saves
  15–20+ minutes on the first boot).

### On the Vita side

- A **PS Vita or PS TV** with **HENkaku / Ensō** custom firmware.
- **VitaShell**, to install the VPK and copy files.
- **`libshacccg.suprx`** extracted to `ur0:data/libshacccg.suprx`. The
  renderer compiles its shaders on the console and needs it. If you don't
  have it yet, extract it with **ShaRKBR33D**.
- About **1.7 GB** free on the memory card for the game data, plus about
  **330 MB** for the converted world cache.
- For trophies only: the **[NoTrpDrm](https://github.com/Yoti/psv_notrpdrm)**
  plugin, listed under `*main` in `ur0:tai/config.txt`. Without it the game
  runs normally and simply reports no trophies.

---

## Installation

### 1. Install the VPK

Copy `NFSU2Vita.vpk` from the [Releases](https://github.com/elmnt22/vita-nfsu2/releases)
page to your Vita and install it with VitaShell. It shows up as
**NFS Underground 2** (title ID `NFSU20001`).

### 2. Copy the game data

The easiest way is the **[NFSU2 Vita Prep Tool](#nfsu2-vita-prep-tool-pc)**:
it checks your game files, then sends only the ones the Vita needs over
Wi-Fi or copies them over USB, along with the converted world data (step 3).
Files already on the Vita are skipped, so sending again later is quick.

To copy by hand instead, copy these folders from your PC install (usually
`C:\Program Files (x86)\EA GAMES\Need for Speed Underground 2`) to
**`ux0:data/NFSU2/`** on the Vita:

| Folder | Size | Contents |
|---|---|---|
| `CARS` | 725 MB | cars, parts, vinyls |
| `CREDITS` | 0.5 MB | credits |
| `FRONTEND` | 36 MB | menus and HUD |
| `GLOBAL` | 22 MB | shared game data |
| `LANGUAGES` | 5 MB | game text (copy the whole folder) |
| `MOVIES` | 228 MB | videos |
| `NIS` | 30 MB | in-game cut scenes |
| `SDATA` | 248 MB | speech and music |
| `SOUND` | 22 MB | sound effects |
| `SUBTITLES` | < 1 MB | subtitles |
| `TRACKS` | 388 MB | the city and race tracks |
| `memcard` | < 1 MB | save/load message text |

You don't need the folders `Support`, `Uninstall` and `#JIMBO`.

When you're done, the Vita should have files such as
`ux0:data/NFSU2/TRACKS/STREAML4RA.BUN`.

The layout on the memory card:

```
ux0:data/NFSU2/
├── CARS/  CREDITS/  FRONTEND/  GLOBAL/  LANGUAGES/  MOVIES/
├── NIS/   SDATA/    SOUND/     SUBTITLES/  TRACKS/  memcard/   <- copied from the PC game
├── cache/      <- converted world data, shaders, shop lists (made by the game or the Prep Tool)
├── profiles/   <- your saves
└── log.txt, log_prev.txt, boot.txt, crumbs.txt               <- logs
```

### 3. Convert the world data (recommended)

On its first boot the game converts the world data into a format the Vita
can stream quickly. This is the **"OPTIMIZING WORLD DATA"** screen, and on
the console it takes **15–20+ minutes**. The **NFSU2 Vita Prep Tool** does
the same job on your PC in about **1½ minutes** and puts the result on the
Vita with the game files. See [NFSU2 Vita Prep Tool](#nfsu2-vita-prep-tool-pc) below.

You can skip this step. The Vita then converts the data itself on the first
boot.

### 4. Play

Start **NFS Underground 2** from the LiveArea.

---

## NFSU2 Vita Prep Tool (PC)

`NFSU2VitaPrep.exe` is a single Windows file with nothing to install. It's
on the [Releases](https://github.com/elmnt22/vita-nfsu2/releases) page. It
gets your PC copy of the game ready for the Vita:

- **Checks your game files.** Every file the Vita needs is compared with the
  original game (size and checksum), so a missing, damaged or modded file
  shows up on the PC instead of as a crash on the Vita.
- **Converts the world data** with the port's own code, so the result is
  identical to what the Vita would make. It also prebuilds the cars' parts
  lists and the shop screens.
- **Puts it on the Vita** over Wi-Fi (FTP) or USB. Only the files the Vita
  needs go over (no `Support`, `Uninstall` or other unused files), and game
  files already there are skipped.
- **Imports a PC save** (see [below](#bringing-over-a-pc-save)).

Use the Prep Tool from the **same release** as your VPK. If the data format
changes, the Vita ignores cache files from an older tool and converts the
data itself.

**Quick start:**

1. Run **NFSU2VitaPrep.exe**. It finds your NFSU2 install from the
   registry. If it doesn't, click **Browse...** and pick the game folder
   (the one with `TRACKS` in it).
2. Click **Convert**. It checks your game files first, then converts all
   six track sets (the City, both Drag Strips, Drift, Street X and URL).
   The result is about 330 MB and goes to a folder called `NFSU2 Vita Prep`.
3. Put it on the Vita. Leave **Game files too** ticked the first time
   (about 1.7 GB), then either:
   - **Over Wi-Fi:** start an FTP server on the Vita on port **1337** (in
     VitaShell press **SELECT**, or use vitacompanion), type the IP address
     the Vita shows and click **Send to Vita**.
   - **Over USB (faster):** in VitaShell press **START**, pick **USB**
     and connect the Vita. Click **Copy to memory card...** and pick the
     memory card's drive.

If it worked, the game goes straight to the title screen without the
"OPTIMIZING WORLD DATA" screen.

The full guide, with the command line options and troubleshooting, is in
**[docs/PREP_TOOL.md](docs/PREP_TOOL.md)**.

---

## Bringing over a PC save

The Prep Tool can import a career save from the PC game with
**Import PC save...**. The PC game keeps saves in
`%LOCALAPPDATA%\NFS Underground 2\<name>\<name>`. The imported profile is
sent or copied to `ux0:data/NFSU2/profiles` with the rest. Load it from the
**Profile Manager** in the game. See
[docs/PREP_TOOL.md](docs/PREP_TOOL.md#importing-a-pc-save).

---

## Settings

Tap **Settings** on the game's LiveArea screen, or hold **L + R** while the
game starts. A small menu opens before the game loads:

| Setting | What it does |
|---|---|
| **Logs** | On by default. Off stops `log.txt` and `perf.csv` from being written. `boot.txt` and `crumbs.txt` are always kept. |
| **Frame rate** | **30 FPS** (default, locked and even) or **Unlocked** (up to 60, varies; experimental). |
| **Fast travel** | Off by default. On: on the World Map, choosing **No** on the GPS prompt takes you straight to that place (`fasttravel.txt`). |
| **Trophies** | On by default. Off: progress is still saved in your profile, but nothing is reported to the system (`notrophy.txt`). |
| **Reset caches** | Deletes `ux0:data/NFSU2/cache`: the converted world data, shaders and shop lists. Your profiles and options are kept. On the next boot the game converts the world again, or you can send it again from the Prep Tool. |

**Start Game** (or START) saves the settings and restarts the game.

The in-game controls use the original game's gamepad defaults. You can
change them in **Options → Controls**.

---

## Reporting problems

Open an [issue](https://github.com/elmnt22/vita-nfsu2/issues). The game
writes its logs to `ux0:data/NFSU2/`:

- `log.txt`: the current run
- `log_prev.txt`: the run before it. The next start overwrites it, so copy
  it first.
- `boot.txt`, `crumbs.txt`: startup and the last steps before a crash

Attach them to the issue, plus a screenshot for a visual problem. For a
Prep Tool problem, attach `convert_log.txt` from its output folder
(`NFSU2 Vita Prep`).

---

## Building from source

You only need this to build the port yourself. Players can use the
release files.

**Requirements:** a native-Windows [VitaSDK](https://vitasdk.org/) and a
bash shell (Git Bash works). The simulator and the Prep Tool also need
[zig](https://ziglang.org/) (`zig cc`) and SDL2. The patched vitaGL is
included in `vita/lib/`.

```sh
VITASDK=E:/vitasdk ./vita/build_vita.sh   # -> vita/build/NFSU2Vita.vpk (+ eboot.bin)
./sim/build_sim.sh                        # Windows simulator of the Vita build
./converter/build_converter.sh            # -> converter/build/NFSU2VitaPrep.exe
```

- The **simulator** (`sim/`) runs the Vita build on Windows at the Vita's
  resolution and memory limits, for testing flow and UI without a console.
  See [sim/README.md](sim/README.md).
- [vita/PORTING_NOTES.md](vita/PORTING_NOTES.md) covers the Vita specifics:
  memory, streaming, the renderer, shaders and logs.
- [converter/README.md](converter/README.md) has the Prep Tool's developer
  notes, including what it writes and when it needs a rebuild.
- [docs/DEVELOPER_GUIDE.md](docs/DEVELOPER_GUIDE.md) and
  [docs/FORMATS.md](docs/FORMATS.md) cover the engine and the game's file
  formats.

The desktop `Makefile` comes from the OpenUG2 base engine. The port's
supported targets are the three scripts above.

---

## Repository layout

| Path | Contents |
| --- | --- |
| `src/` | Game code: engine, world streaming, rendering, physics (`n2sim`), AI, career, front end (FEng), HUD, shops, audio, speech, movies, profiles |
| `vita/` | Vita platform layer (an SDL subset on vitaGL / SceCtrl / SceAudio), boot settings menu, build script, LiveArea assets, vendored vitaGL |
| `sim/` | Windows simulator of the Vita build, plus self-tests in `sim/test/` |
| `converter/` | NFSU2 Vita Prep Tool: game file check, PC-side world conversion, cache builders, PC save import, FTP / USB send |
| `tools/` | Asset dump/audit tools, LiveArea and loading-screen art helpers |
| `docs/` | Player guide for the Prep Tool, file format notes, developer guide |
| `third_party/` | Dear ImGui (desktop debug UI) |

---

## Credits

- **[OpenUG2](https://github.com/whoismept/OpenUG2)** by **whoismept** and
  contributors. It is the open, from-scratch NFSU2 engine this port is built
  on and provides the asset parsers and the base engine. This repository
  keeps its history.
- **[vitaGL](https://github.com/Rinnegatamante/vitaGL)** by Rinnegatamante,
  and the **[VitaSDK](https://vitasdk.org/)** team.
- **[VitaShell](https://github.com/TheOfficialFloW/VitaShell)** and
  **HENkaku / Ensō** by TheFloW and contributors. Players need them to run
  homebrew.
- **ShaRKBR33D** / **SharkF00D** for extracting `libshacccg.suprx`.
- **[vitacompanion](https://github.com/devnoname120/vitacompanion)** by
  devnoname120, for FTP and remote launch during development.
- **[minimp3](https://github.com/lieff/minimp3)** by lieff.
- **[NFSU2FastTravel](https://github.com/CrabJournal/NFSU2FastTravel)** by
  CrabJournal, the PC mod the fast travel option is based on.
- **BigWeedSmokerMan** for the
  [RetroAchievements set](https://retroachievements.org/game/5613) the
  trophy list is based on.
- The author of the **Widescreen Start/Loading Screen** mod
  ([nfsmods.xyz/mod/3786](https://nfsmods.xyz/mod/3786)) for the wide
  title and loading art used in the VPK. The art is not in this repository.
- **[NoTrpDrm](https://github.com/Yoti/psv_notrpdrm)** by Yoti, which lets
  homebrew unlock trophies.
- **[Dear ImGui](https://github.com/ocornut/imgui)** by Omar Cornut.
- **Electronic Arts / EA Black Box** for the original game.

## License and legal

The code is licensed under the
**[PolyForm Noncommercial License 1.0.0](https://polyformproject.org/licenses/noncommercial/1.0.0)**
(see [LICENSE](LICENSE)). You may use, change and share it for noncommercial
purposes. **Commercial use is not allowed.** The original OpenUG2 code this
port started from was released under the MIT License and stays available
under it; the MIT text is kept in [LICENSE](LICENSE). Third-party code keeps
its own license.

Need for Speed and Need for Speed Underground 2 are trademarks of Electronic
Arts. All game content belongs to its owners. This project ships none of
it: the port loads the data from your own copy at runtime.
