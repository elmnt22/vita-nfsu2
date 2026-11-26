# NFSU2 Vita Prep Tool — for Windows

The Prep Tool (formerly UG2Converter) gets your PC copy of Need for Speed
Underground 2 ready for the Vita:

- **Checks your game files** against the original game, so missing,
  damaged or modded files show up on your PC instead of as a crash on the
  Vita.
- **Prebuilds a few small caches** (each track's start, the cars' parts
  lists, the shop screens) in a few seconds. The game streams every track
  straight from the game files, so there is no world conversion any more;
  without the tool the game makes these caches itself on first use.
- **Puts everything on the Vita** over Wi-Fi or USB: only the game files
  the Vita needs, plus those caches.
- **Imports a PC career save.**

## Download

`NFSU2VitaPrep.exe` is on the [Releases](https://github.com/elmnt22/vita-nfsu2/releases)
page. It's a single file with nothing to install (Windows 10 / 11).

The Prep Tool is optional: you can copy the game folders by hand instead
(see the [README](../README.md#2-copy-the-game-data)). Use it from the
**same release** as your VPK. When a cache format changes, the Vita ignores
cache files from an older tool and makes them itself.

## 1. Pick your game folder

Run **NFSU2VitaPrep.exe**. It finds your NFSU2 install from the registry.
If it doesn't, click **Browse...** and choose the game folder (the one that
contains `TRACKS`). A copy of the game folders works too.

## 2. Check your game files

**Check game files** compares each of the 1,202 files the Vita needs with
the original game, by size and checksum. It takes a few seconds and only
reads your files. **Send to Vita** and **Copy to memory card** run the same
check first.

- **All files match:** you're good to go.
- **Missing files:** the Vita needs them. Reinstall the game or copy the
  folders again.
- **Changed files:** a mod, a different release or a damaged copy. These
  may crash or look wrong on the Vita. You can still send them anyway.

It also tells you if `SPEED2.EXE` is v1.2. The Vita doesn't use the EXE,
and the v1.2 patch didn't change the data files, so this is only a note.

## 3. Put it on the Vita

Nothing is converted: the game streams every track straight from the game
files, the way the original does. When the game starts, it deletes old
converted track files from your Vita (they are no longer used).

Sending checks your game files, then makes a few small caches (about 25 MB,
a second): each track's start-up file and the cars' parts lists and shop
screens, which the Vita would otherwise build the first time you open them.
They go to a folder called **`NFSU2 Vita Prep`** next to the tool, or in
your Documents folder if that's not writable (**Open output folder** shows
it), and are sent with the rest.

**Game files too** sends the game files along with the caches.
Leave it ticked the first time (about **1.7 GB**). Only the files the Vita
needs go over: the `Support`, `Uninstall` and `#JIMBO` folders and other
files the port never opens are left out. Game files that are already on
the Vita at the right size are skipped. The caches are skipped too
when it hasn't changed since the last send to that Vita (or memory card)
and is still there: the Prep Tool keeps a list of what it sent,
`sent_<where>.txt`, in its output folder. Adding the World Expansion later
only sends the new cities. Delete that file to send everything again.

### Over USB (Copy to memory card)

Faster than Wi-Fi.

1. In **VitaShell**, press **START**, pick **USB**, and connect the Vita to
   the PC. Make sure VitaShell's USB device is set to the memory card (ux0).
2. Click **Copy to memory card...** and pick the memory card's drive. Its
   `data` or `data\NFSU2` folder works too.

You can also pick any folder on your PC. You get a `data\NFSU2` folder there
to copy to the Vita later.

### Over Wi-Fi (Send to Vita)

1. On the Vita, start an FTP server on port **1337**: in **VitaShell**,
   press **SELECT**, or use **vitacompanion**.
2. Close NFS Underground 2 on the Vita.
3. In the Prep Tool, type the IP address the Vita shows and click
   **Send to Vita**.

Everything goes to `ux0:data/NFSU2`. If the transfer is cut off, send
again to finish: files
already sent are skipped.

### By hand

Copy the game folders (see the [README](../README.md#2-copy-the-game-data))
and the **`cache`** folder from the output folder into
**`ux0:data/NFSU2/`**, so you get for example
`ux0:data/NFSU2/cache/L4RG.focus`.

### Checking it worked

Start the game. It goes straight to the title screen.

## Importing a PC save

1. Click **Import PC save...**. The file picker opens in
   `%LOCALAPPDATA%\NFS Underground 2`, where the PC game keeps each
   profile as `<name>\<name>`. Choose the save file.
2. The Prep Tool shows what it imported (career cars, garage cars, high
   scores). The profile goes into the output folder's `profiles` folder.
3. **Send to Vita** or **Copy to memory card** takes it to
   `ux0:data/NFSU2/profiles`. Or copy the `profiles` folder there by hand.
4. In the game, load the profile from the **Profile Manager**.

### A save downloaded from a save site

Saves shared online (for example 100% saves from sites such as nfsu2.xyz)
are ordinary PC profile files and import the same way. Unzip the download
first, then either:

- In the Prep Tool, click **Import PC save...** and pick the save file
  itself (the file with no extension inside the profile folder, about 54 KB).
  Its name and folder don't matter.
- Or copy it to the Vita without the Prep Tool: put the file (or the whole
  unzipped folder) anywhere under `ux0:data/NFSU2/pcsave/`, up to three
  folders deep. In the game, **Profile Manager → Load** offers it.

The profile is named after the name stored in the save. A hand-edited save
without a usable name (1-7 characters, no spaces) is named after its file.
If you already have a profile with that name, delete or rename it first, or
the save is skipped.

It won't import if the file isn't a PC save of Underground 2 (exactly
54,966 bytes): console saves (PS2, GameCube, Xbox) and
saves converted for other tools are a different format. A save whose
checksum doesn't match its contents (common with edited saves) is still
imported, and the Prep Tool says so.

## World Expansion (Rockport and Palmont)

Optional. If you own **Need for Speed Most Wanted (2005)** or **Need for Speed
Carbon** for PC, the Prep Tool can put their cities on the Vita. Nothing is
converted: the game plays both cities from those games' own files, so the
tool only checks them and copies them over. You can also copy them by hand
(see the [README](../README.md#world-expansion-rockport-and-palmont)).

1. Under **Optional: World Expansion**, pick the **Most Wanted** and/or
   **Carbon** folder (the one with `TRACKS` in it). Installed copies are
   found by themselves. Leave a box empty to skip that city. The line under
   the boxes shows each city's state: *not set*, *ready to send*, *files
   differ* or *folder not right*.
2. **Send to Vita** / **Copy to memory card** check the cities' files and
   take them along while **World Expansion too** is ticked (about 1.8 GB for
   both; files already there are skipped). **Check game files** checks them
   on their own.
3. In the game, pick **Quick Race → Free Run**. The game finds the cities by
   itself. The first trip into each city takes a few minutes longer while it
   prepares it.

## Command line

```
NFSU2VitaPrep.exe --check GAME                   check the game files (exit code 0 = all match)
NFSU2VitaPrep.exe --cli GAME OUT [MW|- [CARBON|-]]
                                                 check + convert without the window (log in OUT\convert_log.txt);
                                                 MW / CARBON: the World Expansion's game folders, checked ("-" = skip)
NFSU2VitaPrep.exe --send IP OUT [GAME|- [MW|- [CARBON|-]]]
                                                 send OUT (and GAME's files) to the Vita at IP;
                                                 MW / CARBON: the World Expansion's game folders, sent too
NFSU2VitaPrep.exe --copy DEST OUT [GAME|- [MW|- [CARBON|-]]]
                                                 the same, into DEST (the memory card's drive, or a folder)
NFSU2VitaPrep.exe --import-save SAVE GAME OUT    import a PC save into OUT\profiles
```

## What it doesn't convert

- **Shader binaries.** These are specific to the Vita's GPU, so the Vita
  compiles them itself on first use.
- **Texture class lists.** The game learns these while you play.

## Troubleshooting

- **"Conversion failed on <track>"**: check `convert_log.txt` in the output
  folder and attach it to an issue.
- **A pop-up saying the conversion reached code it does not support**:
  please report the name it shows. It's a bug in the tool.
- **Send fails**: make sure the Vita's FTP server is running on port 1337,
  the Vita isn't in standby, the PC and Vita are on the same network, and
  the memory card has room (about 2 GB the first time). Sending again picks
  up where it stopped.
- **Copy fails**: check that the Vita is still connected in VitaShell's USB
  mode and that the memory card has room.
