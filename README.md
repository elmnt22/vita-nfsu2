# Need for Speed Underground 2 — Vita Edition

A native PlayStation Vita port of **Need for Speed Underground 2**. It is not
emulation and not a wrapper: the engine is rebuilt in C and runs the PC
game's own data on the console. That covers the city, cars, menus, HUD,
speech, music and movies.

The goal is to play like the real game. Menus, HUD, sounds, handling, camera
and controls are rebuilt 1:1 from the shipped game data and from reverse
engineering of the PC executable (`SPEED2.EXE` v1.2). Nothing is redesigned.

**New in v0.2.0: the World Expansion.** If you also own Need for Speed Most
Wanted (2005) or Need for Speed Carbon for PC, you can take your Underground 2
car to **Rockport** and **Palmont** at night, with traffic and about 300 race
events between them. See [World Expansion](#world-expansion-rockport-and-palmont).

> **Status: in development.** Builds are test builds and may crash or show
> glitches. Many features are confirmed on hardware. The newest ones have so
> far only been checked in the Windows simulator of the Vita build. Please
> report problems with the log files (see [Reporting problems](#reporting-problems)).

> **No game data is included.** This repository has no EA assets and no EA
> code. You need your own legally owned copy of the PC game (and, for the
> World Expansion, of Most Wanted and/or Carbon). This project is not
> affiliated with, authorized or endorsed by Electronic Arts.

Disclosure: Whilst I know a bit of programming and coding, I used AI to help accelerate areas of this port that otherwise would have taken me forever. AI used was Claude Opus.

---

## Contents

- [What's in the port](#whats-in-the-port)
- [What you need](#what-you-need)
- [Installation](#installation)
- [NFSU2 Vita Prep Tool (PC)](#nfsu2-vita-prep-tool-pc)
- [Bringing over a PC save](#bringing-over-a-pc-save)
- [World Expansion: Rockport and Palmont](#world-expansion-rockport-and-palmont)
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
- **World Expansion (optional).** Two more cities: Rockport (from Need for
  Speed Most Wanted, 2005) and Palmont (from Need for Speed Carbon), at night,
  with traffic, the minimap, world map, GPS, shops and about 300 race events.
  Needs your own PC copy of those games. See
  [World Expansion](#world-expansion-rockport-and-palmont).
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
- For the World Expansion only: about **2.7 GB** more, and your own PC copy
  of **Need for Speed Most Wanted (2005)** and/or **Need for Speed Carbon**
  (see [World Expansion](#world-expansion-rockport-and-palmont)).
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
└── logs/        <- logs: latest/ is the current run, plus the last 5 runs by start time
```

### 3. Convert the world data (recommended)

On its first boot the game converts the world data into a format the Vita
can stream quickly. This is the **"OPTIMIZING WORLD DATA"** screen, and on
the console it takes **15–20+ minutes**. The **NFSU2 Vita Prep Tool** does
the same job on your PC in about **2 minutes** and puts the result on the
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
  needs go over (no `Support`, `Uninstall` or other unused files). Files
  already there are skipped: the tool remembers what it sent to each Vita or
  memory card, so sending again later only sends what changed.
- **Converts the World Expansion cities** from your Most Wanted and/or Carbon
  install (see [below](#world-expansion-rockport-and-palmont)).
- **Imports a PC save** (see [below](#bringing-over-a-pc-save)).

Use the Prep Tool from the **same release** as your VPK. If the data format
changes, the Vita ignores cache files from an older tool and converts the
data itself.

**Quick start:**

Run **NFSU2VitaPrep.exe**. The window follows the steps from top to bottom:

1. **Your Need for Speed Underground 2 Folder.** The tool finds your NFSU2
   install from the registry. If it doesn't, click **Browse...** and pick
   the game folder (the one with `TRACKS` in it). **Check Game Files** checks
   it on its own; the line beside it tells you whether it's ready.
2. **Optional: World Expansion.** Leave it empty if you only want Bayview.
3. **Convert.** It checks your game files first, then converts all six
   track sets (the City, both Drag Strips, Drift, Street X and URL). The
   result is about 330 MB and goes to a folder called `NFSU2 Vita Prep`.
   **Done!** shows beside the progress bar when it has finished.
4. **Put It on Your Vita.** Leave **Game files too** ticked the first time
   (about 1.7 GB), then either:
   - **Over Wi-Fi:** start an FTP server on the Vita on port **1337** (in
     VitaShell press **SELECT**, or use vitacompanion), type the IP address
     the Vita shows and click **Send to Vita**.
   - **Over USB (faster):** in VitaShell press **START**, pick **USB**
     and connect the Vita. Click **Copy to Memory Card...** and pick the
     memory card's drive.

   **Open Output Folder** shows the converted data and the logs.

If it worked, the game goes straight to the title screen without the
"OPTIMIZING WORLD DATA" screen.

The full guide, with the command line options and troubleshooting, is in
**[docs/PREP_TOOL.md](docs/PREP_TOOL.md)**.

---

## Bringing over a PC save

The Prep Tool can import a career save from the PC game with
**Import PC Save...**. The PC game keeps saves in
`%LOCALAPPDATA%\NFS Underground 2\<name>\<name>`. The imported profile is
sent or copied to `ux0:data/NFSU2/profiles` with the rest. Load it from the
**Profile Manager** in the game. See
[docs/PREP_TOOL.md](docs/PREP_TOOL.md#importing-a-pc-save).

---

## World Expansion: Rockport and Palmont

The World Expansion is an optional add-on that brings two more cities into
the port, both at night:

- **Rockport**, from **Need for Speed Most Wanted (2005)**
- **Palmont**, from **Need for Speed Carbon**

They play by Underground 2's rules, with your Underground 2 cars and
handling. Each city has:

- **Free roam** with traffic, the minimap, the world map and the GPS.
- **Race events** spread around the map: about 156 in Rockport and 140 in
  Palmont. Circuits, sprints and drift races, plus checkpoint, tollbooth,
  speedtrap and lap knockout races, and Palmont's canyon duels, canyon
  sprints and canyon drifts.
- **Shops and the garage**, and smackable props along the roads.
- **Your garage or a fresh start.** The first time you drive into a city,
  the game asks whether to **share your garage** with it (your cars, parts
  and cash) or **start fresh** there with its own career. You can switch
  later.

Not included: cops, heat and pursuits (Underground 2 has none), and
Carbon's pursuit knockout events.

### What you need

- Your own PC copy of **Need for Speed Most Wanted (2005)** for Rockport
  and/or **Need for Speed Carbon** for Palmont. One is enough: the other
  city is simply left out. **No data from those games is included** in this
  repository or the VPK; the Prep Tool converts your own copy on your PC.
- About **2.7 GB** more on the memory card for both cities.
- The **Prep Tool** from the same release as your VPK.

### Setting it up

1. In the Prep Tool, under **Optional: World Expansion**, pick your Most
   Wanted and/or Carbon folder (the one with `TRACKS` in it). Installed
   copies are found by themselves. Leave a box empty to skip that city. The
   labels under the boxes show each city's state (**Found**, **Ready to
   send**, **Not set** or **Folder not right**).
2. Click **Convert**. After the Underground 2 tracks it checks those games'
   files and converts each city. This takes longer than the Underground 2
   tracks alone.
3. Send or copy as usual with **World Expansion too** ticked. If this tool
   already sent the Underground 2 data, only the new cities go over.
4. On the Vita, open [Settings](#settings) and tick **Rockport and Palmont**
   under **World Expansion**. The right side shows which cities are on the
   memory card.
5. In the game, pick **Quick Race → Free Run**. It asks which city you want.

Turning the switch off hides the cities again. Your Underground 2 career is
untouched either way.

### Without the Prep Tool

The Vita can translate a city itself from the game's raw files. It's slower
(the first trip into each city shows a progress screen) and has had less
testing than the Prep Tool. Copy these files from your PC install to the
memory card, keeping the folders:

| City | From your PC install | To the Vita |
|---|---|---|
| Rockport | `TRACKS\STREAML2RA.BUN`, `TRACKS\L2RA.BUN`, `TRACKS\L2RA\MINI_MAP.BIN`, `GLOBAL\gameplay.bin` (Most Wanted) | `ux0:data/NFSU2/expansion/mw/` |
| Palmont | `TRACKS\STREAML5RA.BUN`, `TRACKS\L5RA.BUN`, `TRACKS\L5RA\MINI_MAP.BIN`, `GLOBAL\gameplay.bin` (Carbon) | `ux0:data/NFSU2/expansion/carbon/` |

For example, Rockport's stream ends up at
`ux0:data/NFSU2/expansion/mw/TRACKS/STREAML2RA.BUN`. The two `.BUN` files are
required. Without `MINI_MAP.BIN` the city has no minimap, and without
`gameplay.bin` it has no race events. Then turn on the switch in
[Settings](#settings) as in step 4 above. Leave the raw files there: when a
later release changes the cities' format, the Vita translates them again
from those files.

### Good to know

- The World Expansion is **new and has had little hardware testing**. Some
  busy spots can drop below 30 FPS. Logs from either city are very welcome
  (see [Reporting problems](#reporting-problems)).
- If the cities' data format changes in a later release, convert them again
  with that release's Prep Tool.

---

## Settings

Tap **Settings** on the game's LiveArea screen, or hold **L + R** while the
game starts. A small menu opens before the game loads:

| Setting | What it does |
|---|---|
| **Logs** | On by default. Off stops `log.txt` and `perf.csv` from being written. `boot.txt` and `crumbs.txt` are always kept. |
| **Frame rate** | **30 FPS** (default, locked and even) or **Unlocked** (up to 60, varies; experimental). |
| **Fast travel** | Off by default. On: on the World Map, choosing **No** on the GPS prompt takes you straight to that place (`fasttravel.txt`). |
| **Trophies** | Off by default. On: trophies are reported to the system (`trophies.txt`; needs the NoTrpDrm plugin). Off, progress is still saved in your profile and is reported when you turn them on. |
| **Full detail** | Off by default: the lighter mix for a steady 30 FPS downtown (road reflection redrawn every second frame, fewer traffic cars, half the particle rate). On: the original rates (`fulldetail.txt`). |
| **World Expansion: Rockport and Palmont** | Off by default. On: Free Run offers Rockport and Palmont (`expansion/enable.txt`). The right side shows which cities are on the memory card; send them with the [Prep Tool](#world-expansion-rockport-and-palmont) first. |
| **Reset caches** | Deletes `ux0:data/NFSU2/cache`: the converted world data, shaders and shop lists. Your profiles and options are kept. On the next boot the game converts the world again, or you can send it again from the Prep Tool. |

**Start Game** (or START) saves the settings and restarts the game.

The in-game controls use the original game's gamepad defaults. You can
change them in **Options → Controls**.

---

## Reporting problems

Open an [issue](https://github.com/elmnt22/vita-nfsu2/issues). The game
writes its logs to `ux0:data/NFSU2/logs/`:

- `latest/`: the current run (or the last one, if the game is closed)
- `2026-11-17_21-05-33/` and so on: the 5 runs before it, named by the time
  each one started. Older runs are deleted.

Each folder has `log.txt`, plus `boot.txt` and `crumbs.txt` (startup and the
last steps before a crash). If the game crashed, the crash is in `latest/`
until you start the game again, then in the newest dated folder.

Attach that folder's files to the issue, plus a screenshot for a visual problem. For a
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

Need for Speed, Need for Speed Underground 2, Need for Speed Most Wanted and
Need for Speed Carbon are trademarks of Electronic Arts. All game content
belongs to its owners. This project ships none of it: the port loads the
data from your own copies at runtime, and the World Expansion's cities are
converted on your own PC from your own installs.
