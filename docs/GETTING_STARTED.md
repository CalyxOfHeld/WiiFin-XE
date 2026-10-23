# Getting started

From a Jellyfin server on your PC to a film on your TV. If you already have a server, skip to [Installing on the Wii](#installing-on-the-wii).

- [What you need](#what-you-need)
- [Setting up the server](#setting-up-the-server)
- [Installing on the Wii](#installing-on-the-wii)
- [The first start](#the-first-start)
- [Signing in](#signing-in)
- [Getting the best picture](#getting-the-best-picture)

## What you need

- **A Wii** (or a Wii U in Wii mode) with the Homebrew Channel, and an SD card.
- **A Jellyfin server** on a computer on the same network. Version 10.10 or newer works best; 10.9 has fewer extras.
- **A network connection for the Wii.** Wi-Fi works. A USB LAN adapter is steadier and allows the highest quality.

You don't need to convert any files. The server converts what the Wii can't play, while it plays.

## Setting up the server

1. Install Jellyfin from [jellyfin.org](https://jellyfin.org) and add your libraries in its dashboard.
2. **Let the Wii reach it.** WiiFin-XE talks to the server on port **8096**. If the computer has a firewall (Windows has one by default), allow Jellyfin through it.
3. **Optional extras**, each in the server's dashboard:

| Extra | What it gives you on the Wii | Needs |
|---|---|---|
| Hardware transcoding (Playback settings) | Less work for the computer while it converts videos for the Wii | A graphics card or CPU that Jellyfin supports |
| Trickplay images | Pictures above the seek bar while you skip through a video | Jellyfin 10.9+ |
| The Intro Skipper plugin | Skip Intro, Skip Recap and Skip Credits buttons, and Next Episode during the credits | Jellyfin 10.10+ |
| Lyrics (`.lrc` files beside the songs) | Synced lyrics on the Now Playing screen | Jellyfin 10.9+ |
| Quick Connect (General settings) | Signing in without typing your password on the Wii | — |

## Installing on the Wii

Download the latest WiiFin-XE release, then pick one:

- **Homebrew Channel:** unzip `WiiFin-hbc.zip` at the root of the SD card. It puts everything in `apps/WiiFin/`, including the Chinese and Korean font.
- **A channel on the Wii Menu:** install `WiiFin.wad` with a WAD manager (Wii and vWii). For Chinese or Korean, also copy `apps/WiiFin/fonts/` from the zip to the SD card.
- **Dolphin:** open `WiiFin.dol`.

WiiFin-XE uses the same folder as WiiFin. If WiiFin is already on the SD card, WiiFin-XE replaces it and keeps its profiles and settings.

## The first start

The first time, WiiFin-XE asks **whether the whole picture fits your TV**. Older TVs, CRTs especially, cut off the edges of the picture.
- Choose **Adjust** to bring the edges in until they show.
- **−/+** jump between common shapes and margins.
- You can change it later in Settings > View > Screen Area, and save a different one for each TV.

Then choose **Connect To Jellyfin** on the main menu.

## Signing in

The Connect screen has three tabs (−/+ moves between them):

| Tab | What it does |
|---|---|
| **Discover** | Finds Jellyfin servers on your network and fills in the address. This uses UDP port 7359; if it finds nothing, type the address instead. |
| **Credentials** | The server's address (`http://<the computer's IP>:8096`), your username and your password. |
| **Quick Connect** | Shows a code. Enter it in Jellyfin on your phone or computer, and the Wii signs in without your password. |

Signing in makes a **profile**. A new profile asks three things, shown on small pictures:
- **How it signs in:** at once, with a four-digit passcode, or with the password every time.
- **Its home screen:** rows or a grid.
- **Its library view:** posters, a list, or a list with covers.

On "Who's watching?":
- **1** (Y on a Classic Controller or GameCube controller) opens **Edit Profile**: its nickname, server address, user name, password and picture.
- **2** gives a profile its picture.
- **−** deletes it.

To have one profile open by itself when WiiFin-XE starts, turn on **Open at Start-Up** in its Sign-In settings. Hold B while WiiFin-XE starts to skip it.

## Getting the best picture

The Wii shows 640×480. The server sends video at that size or smaller, converted as MPEG-4 with MP3 sound.

- **Video Quality** (Settings > Playback) sets how much data each second gets, from 1.5 Mb/s to 5 Mb/s.
  - Max is for a wired adapter; on Wi-Fi, lower it if videos stop to buffer.
  - WiiFin-XE also measures your connection and stays under it.
- **Direct Play** plays files the Wii can decode as they are: SD Xvid and DivX, DVD MPEG-2, H.264 up to 480p.
  - Seeking is almost instant, and the server does no work.
  - If a file plays too slowly, WiiFin-XE switches to a converted stream at the same spot.
  - The full list is on [Direct play](../DIRECT_PLAY.md).
- **Smooth Motion** blends frames, so 24 fps films pan evenly on a 60 Hz TV.
- **Backgrounds: Solid** removes the bands some TVs show in gradients.

Next, see the [user guide](USER_GUIDE.md) for every control and setting, or [What's new in XE](XE_FEATURES.md).
