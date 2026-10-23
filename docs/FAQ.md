# FAQ and troubleshooting

Answers to the questions that come up most. If yours isn't here, include `wiifin.log` when you ask (see [Where is the log?](#where-is-the-log)).

- [Connecting](#connecting)
- [Playing](#playing)
- [The screen](#the-screen)
- [Profiles and settings](#profiles-and-settings)
- [Other apps](#other-apps)

## Connecting

### Discover finds no server

The Wii and the server must be on the same network. Discover asks on UDP port 7359, which some routers and firewalls block. Type the address on the Credentials tab instead: `http://<the computer's IP>:8096`.

### "No network connection"

The Wii itself isn't online. Check its connection in Wii Settings > Internet, then try again. If it says "This is taking long", the Wii's network sometimes needs a restart: turn the Wii off and on again.

### The server is found, but signing in fails

- Check that the computer's firewall lets Jellyfin through on port 8096.
- For a server on HTTPS with a self-signed certificate, turn off **SSL Verification** (Settings > System > Connection).
- For Quick Connect, turn it on in the server's dashboard first.

### Where is the log?

`SD:/apps/WiiFin/wiifin.log` holds what happened the last time WiiFin-XE ran. Sign-in tokens, passwords, addresses and server names are masked in it, so it's safe to share in a bug report.

## Playing

### Videos stop to buffer, or stutter

- Lower **Video Quality** (Settings > Playback > Video). Max is meant for a wired USB LAN adapter.
- On Wi-Fi, move the Wii closer to the router, or try a USB LAN adapter.
- WiiFin-XE lowers the quality by itself after repeated buffering, and tells you when it does.

### A file plays as it is, but too slowly

When fewer than 85% of the frames show in time, WiiFin-XE switches to a converted stream at the same spot. If a file keeps having trouble, turn **Direct Play** off. The limits are on [Direct play](../DIRECT_PLAY.md).

### The server's computer works hard during playback

Every video the Wii can't decode is converted while it plays. Hardware transcoding in Jellyfin's Playback settings takes most of that work off the processor.

### Surround sound

The Wii plays stereo. 5.1 sound is mixed down to two channels.

### Picture subtitles take a moment to appear

Picture-based subtitles (PGS, VobSub) are drawn into the video by the server, so they need a converted stream. Text subtitles (SRT, ASS, VTT) are drawn by the Wii and work with direct play too.

### No sound from the Wii Remote's speaker

The **Wii Remote Speaker** setting doesn't make a sound yet. It's a known issue.

## The screen

### The edges of the picture are cut off

Settings > View > **Screen Area**:
- Bring the arrows in until the white edge shows all the way round.
- **−/+** jump to common shapes and margins.
- **1** saves the area for this TV.

### There are bands in the backgrounds

Settings > View > **Backgrounds: Solid** uses plain colours instead of gradients.

### Chinese or Korean shows as boxes

Those characters need the font in `SD:/apps/WiiFin/fonts/`. It's in the Homebrew Channel zip. With the WAD alone, copy that folder from the zip to the SD card.

### The first press after the screensaver does nothing

That press only wakes the screen, so nothing gets selected by accident. Press again.

### The keyboard is too small to read

Settings > View > **Keyboard Size**: Normal, Large or Larger.

## Profiles and settings

### My server's address changed

On "Who's watching?", press 1 (Y on a Classic Controller or GameCube controller) on the profile and choose **Server URL**. Type the new address and the password, then Save. The profile keeps its nickname, picture and settings.

### A profile opens on its own at start

That profile has **Open at Start-Up** on. Hold B while WiiFin-XE starts to go to the menu instead, or turn it off in that profile's Sign-In settings.

### I removed a theme by mistake

Removed themes move to `SD:/apps/WiiFin/themes/removed/`. Move the file back into `themes/` on a PC, and it's listed again.

### How do I start over?

Every setting and profile is in `SD:/apps/WiiFin/wiifin.cfg`. Rename or delete it, and WiiFin-XE starts as if new. Profiles keep only a sign-in token, never a password, so you'll need to sign in again.

### Where do my pictures and sounds go?

All in `SD:/apps/WiiFin/`:
- profile and screensaver pictures in `pictures/`
- your own interface sounds and menu music in `sounds/`
- themes in `themes/`

The [user guide](USER_GUIDE.md#-files-on-the-sd-card) lists every file.

## Other apps

### "WiiMC-SS XE not found"

**Local Media (WiiMC)** opens WiiMC to play the files on your SD card or USB drive. Put WiiMC in `SD:/apps/wiimc/`. Any WiiMC plays your files, but only WiiMC-SS XE has the way back to WiiFin-XE.

### Can I control it from my phone?

Yes. In the Jellyfin app or web page, choose **Play on...** and pick Nintendo Wii. From there you can:
- play, pause, seek and change the volume
- switch audio and subtitle tracks
- queue music
- send a message that shows on the TV

### Does it replace WiiFin?

WiiFin-XE uses the same folder, `apps/WiiFin/`, and reads the same settings. To go back to WiiFin, copy its `boot.dol` over WiiFin-XE's.
