# Building WiiFin-XE

How to build WiiFin-XE from source and run it while working on it. To install a release, see the [user guide](USER_GUIDE.md); to contribute, [CONTRIBUTING.md](../CONTRIBUTING.md).

## Requirements

- [devkitPro](https://devkitpro.org) with `devkitPPC`, `libogc`, and `wii-dev` portlibs
- Graphics: `GRRLIB`, `libpngu`, `freetype`, `libjpeg`
- mbedTLS (bundled under `libs/`, cross-compiled by `setup.sh`)
- MPlayer CE as `libmplayer.a`, for playback: prebuilt in `libs/mplayer-ce-build`, rebuilt from source by `tools/mplayer/build.sh` (see [MPLAYER_CE_BUILD.md](../MPLAYER_CE_BUILD.md)). Without it, WiiFin still compiles but cannot play.

## Building

On a fresh machine, `./setup.sh` installs devkitPro and the portlibs, builds GRRLIB and mbedTLS, then compiles WiiFin (Arch-based distros, or any host with `dkp-pacman`).

```bash
./build.sh          # WiiFin.dol
./build.sh wad      # WiiFin.wad, the installable channel (needs libWiiPy, installed by setup.sh)
```

`make wad` puts `WiiFin.dol` into `tools/wad/template.wad` (banner, NAND loader, ticket and TMD of title `WIFN`) and fakesigns it: see `tools/make_wad.py`.

### On Windows

WiiFin-XE is built on Windows with devkitPro's own installer and its MSYS2 shell (`C:\devkitPro\msys2\usr\bin\bash.exe`).

1. Install devkitPro with the Wii tools. Then run `./setup.sh --no-build` once from its shell, to build GRRLIB and mbedTLS.
2. **Watch out for spaces:** make can't build in a path with spaces in it, such as `Documents\wii app forks`. Either keep the source in a path without spaces, or make a junction to it and build there:

   ```powershell
   New-Item -ItemType Junction -Path C:\wiifin-src -Target "C:\path with spaces\WiiFin-src"
   ```

   ```bash
   cd /c/wiifin-src && WERROR=-Werror ./build.sh CURDIR=$PWD
   ```

   `CURDIR=$PWD` makes make use the junction's path rather than the real one.
3. `WERROR=-Werror` turns warnings into errors, the same as the CI build.

### Translations

`tools/lang_update.sh` brings `lang/wiifin.pot` and every `lang/*.po` up to date with the strings in the source:
- New strings come in empty, and changed ones are marked fuzzy. WiiFin shows the English for both until someone translates them.
- `tools/lang_check.py` checks that every translated character is in one of WiiFin's fonts.

### The website

`tools/make_site.py` builds the WiiFin-XE site, a small wiki, into `build/site/` from the Markdown docs. It needs `pip install markdown-it-py mdit-py-plugins`.

- The pages are listed in `PAGES` at the top of the script. The template, styles, script, home page and pictures are in `docs/site/`.
- Every page holds its own styles and script, plus a search over every section of every page. Only the `img/` folder sits beside them.
- A link to a section that doesn't exist fails the run, so a renamed heading can't silently break a link.

### Memory layout

`tools/wii_wiifin.ld` keeps WiiFin's code and data below `0x80804000`, where the stub that returns to the loader lives.
- If the link fails on that check, move more constant data into the `.data.blobs` section, which is placed after the stub's area.
- Brotli's dictionary is already there.

## Running

On **real Wii hardware**: unzip `WiiFin-hbc.zip` at the root of the SD card (it holds `apps/WiiFin/`) to start WiiFin from the Homebrew Channel, or install `WiiFin.wad` with a WAD manager (works on vWii too). WiiFin keeps its settings, profiles and log (`wiifin.log`) in `SD:/apps/WiiFin/`.

On **Dolphin Emulator**:

```bash
dolphin-emu -e WiiFin.dol
```

To test against a local Jellyfin in Docker, with scripted button presses and frame captures, see [tools/test](../tools/test/README.md).

## Project structure

```
WiiFin/
├── source/
│   ├── core/        # App, settings, playback sessions, sounds and music, text drawing, log
│   ├── input/       # Wii Remote, Nunchuk, Classic, GameCube, USB mouse and keyboard
│   ├── jellyfin/    # Jellyfin API client (HTTPS via mbedTLS), remote control (WebSocket)
│   ├── player/      # MPlayer CE integration, video output, player overlay, subtitles, thumbnails
│   └── ui/          # The screens: connection, profiles, home, libraries, details, music,
│                    #   settings, themes and the theme editor, profile pictures
├── data/            # Fonts, sounds, pictures
├── docs/            # The user guide, XE's changes, themes, this file
├── libs/            # mbedTLS, the prebuilt MPlayer CE
├── tools/           # WAD packager, linker script, MPlayer CE build (mplayer/),
│                    #   scripts that draw the icon, logos and pictures (*.py)
│   └── test/        # Test harness: Jellyfin in Docker, scripted runs in Dolphin
├── apps/WiiFin/     # Homebrew Channel metadata
├── DIRECT_PLAY.md   # What the Wii plays as it is
└── Makefile
```

