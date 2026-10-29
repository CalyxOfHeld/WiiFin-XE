<p align="center">
  <img src="data/images/logo_wiifin.png" alt="WiiFin-XE logo" width="512"/><br>
  <em>A Jellyfin client for the Nintendo Wii</em>
</p>

---

<p align="center">
<strong>WiiFin-XE</strong> plays your <a href="https://jellyfin.org">Jellyfin</a> films, series and music on the Wii itself.<br>
It's a modified version of <a href="https://github.com/fabienmillet/WiiFin">WiiFin</a> by fabienmillet, written in C++ with <a href="https://github.com/GRRLIB/GRRLIB">GRRLIB</a> and <a href="https://github.com/extremscorner/mplayer-ce">MPlayer CE</a>.
</p>

<table align="center">
  <tr>
    <td><img src="assets/screenshots/main-menu.png" alt="Main menu" width="420"/></td>
    <td><img src="assets/screenshots/film.png" alt="A film" width="420"/></td>
  </tr>
  <tr>
    <td><img src="assets/screenshots/player.png" alt="The player" width="420"/></td>
    <td><img src="assets/screenshots/now-playing.png" alt="The music player" width="420"/></td>
  </tr>
</table>
<p align="center"><sub><em>Big Buck Bunny</em> © Blender Foundation, <a href="https://peach.blender.org">peach.blender.org</a> (CC BY 3.0) · <em>Pill</em> by Heuse & Zeus X Crona feat. Emma Sameth, <a href="https://ncs.io">NCS</a></sub></p>

---

## 📦 Install

Download the files from the latest [WiiFin-XE release (v1.0.0-rc3-xe.1)](https://github.com/CalyxOfHeld/WiiFin-XE/releases/tag/v1.0.0-rc3-xe.1):

- **Homebrew Channel:** unzip `WiiFin-apps.zip` (or `WiiFin-hbc.zip`) at the root of your SD card or USB drive (or transfer it over your network using ftpii). It extracts directly into `apps/WiiFin/` with `boot.dol`, `icon.png`, and `meta.xml`.
- **Wii Menu channel:** install `WiiFin.wad` with a WAD manager (Wii and vWii). For Chinese or Korean, also copy `apps/WiiFin/fonts/` from the zip to the SD card.
- **Dolphin:** open `WiiFin.dol` directly in the emulator.

Then sign in to your Jellyfin server with your username and password, or with Quick Connect from your Jellyfin dashboard (or phone).

## ✨ Highlights

- **Films, series and music** from your Jellyfin server. Files the Wii can decode play as they are, and the server converts the rest.
- A **rows home** in two pages, Home and Browse. Expand any row into a full page, and choose which libraries show.
- A **player** with the server's thumbnails, scenes and chapters, skip intro and credits, next episode, audio and subtitle tracks, and resume.
- **Full GameCube & Gamepad Navigation:** navigate the player overlay hands-free with the D-Pad or Stick; focused buttons pulse with visual feedback and trigger with `A`. Use the seek bar to jump through videos, and skip buttons (Intro / Next episode) take focus automatically.
- **Configurable Playback Controls:** customize whether the C-Stick or D-Pad handles scrubbing versus volume adjustment under `Settings > Playback > Controller`. Options to disable episode buttons (+ / −) and ask before stopping on `B`.
- **Live Volume HUD:** floating on-screen volume card and transport bar badge provide immediate visual confirmation of volume adjustments and mute status.
- **Smart Subtitles:** subtitles default to OFF with automatic preferred language matching against available audio and subtitle tracks.
- **Music** with a Now Playing screen, visualizers and synced lyrics. It keeps playing while you browse.
- **Profiles** with pictures (yours, your Miis, or the jellyfish), nicknames, an optional passcode, and a theme each.
- **Themes of your own:** edit every colour on the Wii, save it, and share the file.
- **Controllers:** Wii Remote and Nunchuk, Classic Controller, GameCube controller, wireless Wii U Pro Controller (Bluetooth), Nintendo Switch Pro Controller (USB), Sony DualShock 4 and DualShock 3 (USB), wired Microsoft Xbox One (USB), and USB mouse or keyboard. DualSense, Xbox 360 and generic USB pads should also work. No sensor bar needed. (Note: the Wii U Pro Controller works over Bluetooth only; the Xbox Series X|S controller is not supported).
- **Screensaver & Screen Area:** a bouncing clock screensaver and custom screen area calibration snaps for TVs that crop overscan.
- **Local Media (WiiMC)** opens WiiMC for the files on your SD card or USB drive. WiiMC-SS XE brings you back.
- **Remote control** from Jellyfin's dashboard and apps: Play on... Nintendo Wii.

📖 **Documentation:** Read the online guide at **[calyxofheld.github.io/WiiFin-XE](https://calyxofheld.github.io/WiiFin-XE/)**.
What XE adds to WiiFin, in detail: **[docs/XE_FEATURES.md](docs/XE_FEATURES.md)**. Everything else, including controls, settings and custom sounds: the **[user guide](docs/USER_GUIDE.md)**.

## ⚠️ Project status

> 🚧 **Experimental:** it works, but expect rough edges on real hardware.

- Direct play is for SD videos only (see [DIRECT_PLAY.md](DIRECT_PLAY.md)), and sound is stereo.
- Picture subtitles (PGS, VobSub) are burned in by the server.
- The Wii Remote Speaker setting doesn't make a sound yet.
- Strings added in XE show in English in other languages for now.
- More in the [known limitations](docs/USER_GUIDE.md#%EF%B8%8F-known-limitations).

## 🛠️ Building

See [docs/BUILDING.md](docs/BUILDING.md), including how to build on Windows. To translate, see [lang/README.md](lang/README.md). [CONTRIBUTING.md](CONTRIBUTING.md) covers the rest.

## 🙏 Credits

- **[WiiFin](https://github.com/fabienmillet/WiiFin)** by fabienmillet and its contributors. WiiFin-XE is built on it, and nearly everything listed in the user guide is their work.
- [Jellyfin](https://jellyfin.org), [GRRLIB](https://github.com/GRRLIB/GRRLIB), [MPlayer CE](https://github.com/extremscorner/mplayer-ce), [libogc and devkitPro](https://devkitpro.org), [mbedTLS](https://github.com/Mbed-TLS/mbedtls).
- Fonts: DejaVu Sans (Bitstream Vera licence), Droid Sans Japanese (Apache 2.0) and Noto Sans CJK (SIL Open Font License), in `data/fonts/`.

Jellyfin is a trademark of the Jellyfin project. Wii is a trademark of Nintendo. WiiFin-XE isn't affiliated with or endorsed by either.

## 📜 License

WiiFin-XE, like WiiFin, is licensed under the **GPLv3**. See [LICENSE](LICENSE).
