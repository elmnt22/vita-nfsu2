# UG2Converter — world data converter for Windows

On its first boot the Vita build converts the game's world data into a
format it can stream quickly (the **"OPTIMIZING WORLD DATA"** screen). On
the console that takes **15–20+ minutes**. UG2Converter does the same work
on your PC in about **1½ minutes** and can send the result to the Vita over
Wi-Fi.

It runs the port's own conversion code, so its output is the same as what
the Vita would make itself.

## Download

`UG2Converter.exe` is on the [Releases](https://github.com/elmnt22/vita-nfsu2/releases) page. It's a
single file with nothing to install (Windows 10 / 11).

Use the converter from the **same release** as your VPK. When the game's
data format changes, the Vita ignores cache files from an older converter
and converts on its own again.

## Converting

1. Run **UG2Converter.exe**.
2. It finds your NFSU2 install from the registry. If it doesn't, click
   **Browse...** and choose the game folder (the one that contains
   `TRACKS`).
3. Click **Convert**. All six track sets are converted:
   - The City
   - Drag Strips 1 and 2
   - Drift Venues
   - Street X Tracks
   - URL Tracks

   It also prebuilds the cars' parts lists and the shop screens, which the
   Vita would otherwise build the first time you open them.

The result (about **330 MB**) goes to a folder called
**`NFSU2 Vita cache`** next to the converter, or in your Documents
folder if that's not writable. **Open output folder** shows it.

## Sending it to the Vita

### Over Wi-Fi (Send to Vita)

1. On the Vita, start an FTP server on port **1337**: in **VitaShell**,
   press **SELECT**, or use **vitacompanion**.
2. In the converter, type the IP address the Vita shows and click
   **Send to Vita**.

The files go to `ux0:data/NFSU2/cache`. The Vita's old index files are
deleted first, and each track's index is sent last. If the transfer is cut
off, the Vita sees that track as "not converted" and converts it itself,
so you never end up with a half-copied track.

### By hand

Copy the **`cache`** folder from the output folder into
**`ux0:data/NFSU2/`**, so you get for example
`ux0:data/NFSU2/cache/tiles/STREAML4RA_v5.idx`.

### Checking it worked

Start the game. If the conversion was picked up, it goes straight to the
title screen without the "OPTIMIZING WORLD DATA" screen.

## Importing a PC save

1. Click **Import PC save...**. The file picker opens in
   `%LOCALAPPDATA%\NFS Underground 2`, where the PC game keeps each
   profile as `<name>\<name>`. Choose the save file.
2. The converter shows what it imported (career cars, garage cars, high
   scores). The profile goes into the output folder's `profiles` folder.
3. **Send to Vita** copies it to `ux0:data/NFSU2/profiles`. Or copy the
   `profiles` folder there by hand.
4. In the game, load the profile from the **Profile Manager**.

## Command line

```
UG2Converter.exe --cli GAME OUT                  convert without the window (log in OUT\convert_log.txt)
UG2Converter.exe --send IP OUT                   send OUT to the Vita at IP
UG2Converter.exe --import-save SAVE GAME OUT     import a PC save into OUT\profiles
```

## What it doesn't convert

- **Shader binaries.** These are specific to the Vita's GPU, so the Vita
  compiles them itself on first use.
- **Texture class lists.** The game learns these while you play.

## Troubleshooting

- **"Conversion failed on <track>"**: check `convert_log.txt` in the output
  folder and attach it to an issue.
- **A pop-up saying the conversion reached code it does not support**:
  please report the name it shows. It's a bug in the converter.
- **Send fails**: make sure the Vita's FTP server is running on port 1337,
  the Vita isn't in standby, and the PC and Vita are on the same network.
