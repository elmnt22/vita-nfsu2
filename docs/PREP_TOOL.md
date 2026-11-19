# NFSU2 Vita Prep Tool — for Windows

The Prep Tool (formerly UG2Converter) gets your PC copy of Need for Speed
Underground 2 ready for the Vita:

- **Checks your game files** against the original game, so missing,
  damaged or modded files show up on your PC instead of as a crash on the
  Vita.
- **Converts the world data.** On its first boot the Vita build converts
  the game's world data into a format it can stream quickly (the
  **"OPTIMIZING WORLD DATA"** screen). On the console that takes
  **15–20+ minutes**. The Prep Tool does the same work on your PC in about
  **1½ minutes**, with the port's own conversion code, so its output is the
  same as what the Vita would make itself.
- **Puts everything on the Vita** over Wi-Fi or USB: only the game files
  the Vita needs, plus the converted data.
- **Imports a PC career save.**

## Download

`NFSU2VitaPrep.exe` is on the [Releases](https://github.com/elmnt22/vita-nfsu2/releases)
page. It's a single file with nothing to install (Windows 10 / 11).

Use the Prep Tool from the **same release** as your VPK. When the game's
data format changes, the Vita ignores cache files from an older tool and
converts on its own again.

## 1. Pick your game folder

Run **NFSU2VitaPrep.exe**. It finds your NFSU2 install from the registry.
If it doesn't, click **Browse...** and choose the game folder (the one that
contains `TRACKS`). A copy of the game folders works too.

## 2. Check your game files

**Check game files** compares each of the 1,202 files the Vita needs with
the original game, by size and checksum. It takes a few seconds and only
reads your files. **Convert** runs the same check first.

- **All files match:** you're good to go.
- **Missing files:** the Vita needs them. Reinstall the game or copy the
  folders again.
- **Changed files:** a mod, a different release or a damaged copy. These
  may crash or look wrong on the Vita. You can still convert and send
  anyway.

It also tells you if `SPEED2.EXE` is v1.2. The Vita doesn't use the EXE,
and the v1.2 patch didn't change the data files, so this is only a note.

## 3. Convert

Click **Convert**. All six track sets are converted:

- The City
- Drag Strips 1 and 2
- Drift Venues
- Street X Tracks
- URL Tracks

It also prebuilds the cars' parts lists and the shop screens, which the
Vita would otherwise build the first time you open them.

The result (about **330 MB**) goes to a folder called
**`NFSU2 Vita Prep`** next to the tool, or in your Documents folder if
that's not writable. **Open output folder** shows it.

## 4. Put it on the Vita

**Game files too** sends the game files along with the converted data.
Leave it ticked the first time (about **1.7 GB**). Only the files the Vita
needs go over: the `Support`, `Uninstall` and `#JIMBO` folders and other
files the port never opens are left out. Game files that are already on
the Vita at the right size are skipped. The converted data is skipped too
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

Everything goes to `ux0:data/NFSU2`. The Vita's old track index files are
deleted first, and each track's index is sent last. If the transfer is cut
off, the Vita sees that track as "not converted" and converts it itself, so
you never end up with a half-copied track. Send again to finish: files
already sent are skipped.

### By hand

Copy the game folders (see the [README](../README.md#2-copy-the-game-data))
and the **`cache`** folder from the output folder into
**`ux0:data/NFSU2/`**, so you get for example
`ux0:data/NFSU2/cache/tiles/STREAML4RA_v6.idx`.

### Checking it worked

Start the game. If the conversion was picked up, it goes straight to the
title screen without the "OPTIMIZING WORLD DATA" screen.

## Importing a PC save

1. Click **Import PC save...**. The file picker opens in
   `%LOCALAPPDATA%\NFS Underground 2`, where the PC game keeps each
   profile as `<name>\<name>`. Choose the save file.
2. The Prep Tool shows what it imported (career cars, garage cars, high
   scores). The profile goes into the output folder's `profiles` folder.
3. **Send to Vita** or **Copy to memory card** takes it to
   `ux0:data/NFSU2/profiles`. Or copy the `profiles` folder there by hand.
4. In the game, load the profile from the **Profile Manager**.

## World Expansion (Rockport and Palmont)

Optional. If you own **Need for Speed Most Wanted (2005)** or **Need for Speed
Carbon** for PC, the Prep Tool can add their cities to the port.

1. Under **Optional: World Expansion**, pick the **Most Wanted** and/or
   **Carbon** folder (the one with `TRACKS` in it). Installed copies are
   found by themselves. Leave a box empty to skip that city. The line under
   the boxes shows each city's state: *not set*, *found -- press Convert*,
   or *ready to send*.
2. **Convert** does the Underground 2 tracks first, then checks the other
   games' files and converts each city. **Check game files** checks them too.
3. **Send to Vita** / **Copy to memory card** take the cities along while
   **World Expansion too** is ticked. Untick it to send only Underground 2
   (the cities are large and are always sent whole).
4. On the Vita, open **Settings** (LiveArea, or hold L+R at start) and tick
   **Rockport and Palmont** under **World Expansion**. Then pick
   **Quick Race → Free Run** in the game.

## Command line

```
NFSU2VitaPrep.exe --check GAME                   check the game files (exit code 0 = all match)
NFSU2VitaPrep.exe --cli GAME OUT [MW|- [CARBON|-]]
                                                 check + convert without the window (log in OUT\convert_log.txt);
                                                 MW / CARBON: the World Expansion's game folders ("-" = skip)
NFSU2VitaPrep.exe --send IP OUT [GAME]           send OUT (and GAME's files) to the Vita at IP
                                                 (converted World Expansion cities go too)
NFSU2VitaPrep.exe --copy DEST OUT [GAME]         the same, into DEST (the memory card's drive, or a folder)
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
