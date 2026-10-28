# WiiFin-XE user guide

Everything WiiFin-XE does, its controls, its settings and how to give it your own sounds. What the XE version adds to WiiFin is listed on its own in [XE_FEATURES.md](XE_FEATURES.md); your own colours are in [THEMES.md](THEMES.md). To build it from source, see [BUILDING.md](BUILDING.md).

- [Installing](#-installing)
- [What WiiFin does](#-what-wiifin-does)
- [Controls](#-controls)
- [Settings](#%EF%B8%8F-settings)
- [Custom sounds](#-custom-sounds)
- [Files on the SD card](#-files-on-the-sd-card)
- [Known limitations](#%EF%B8%8F-known-limitations)

## 📦 Installing

Download the files from the latest WiiFin-XE release.

- **Homebrew Channel**: unzip `WiiFin-apps.zip` (or `WiiFin-hbc.zip`) at the root of the SD card (it holds `apps/WiiFin/`, fonts included) and start WiiFin from the Homebrew Channel
- **Wii Menu channel**: install `WiiFin.wad` with a WAD manager (Wii and vWii); for Chinese or Korean, also copy `apps/WiiFin/fonts/` from the zip to the SD card
- **Dolphin**: open `WiiFin.dol` (`dolphin-emu -e WiiFin.dol`)

WiiFin-XE uses the same folder as WiiFin and replaces it. It keeps its settings, profiles and log (`wiifin.log`, to join to a bug report) in `SD:/apps/WiiFin/`.

## ✅ What WiiFin does

### Connecting

- Sign in with a username and password, or with **Quick Connect** (approved from another device)
- **Server discovery** on the local network, or the address typed in (HTTP or **HTTPS**, self-signed certificates accepted)
- **Saved profiles**: several accounts and servers, only an access token kept (no password); each its own look (theme, backgrounds, home screen, library view) and its own sign-in: at once, a **four-digit passcode**, or the password (or Quick Connect) each time with nothing kept on the Wii
- A new profile asks how it signs in, then its home screen and library view, on small pictures of each
- The **profile menu** (the avatar, top right): back to the libraries, switch profile, Settings without signing out, WiiFin's menu

### Browsing

- A home screen of **rows** (Continue Watching, Next Up, latest, top rated, favourites, genres) or a **grid** of libraries
- Film, series and music libraries as posters or lists (with the cover of the selected title), their **tabs** in every view (−/+, or the pointer on one); **A-Z** jumps with left / right
- A title too long for its place **scrolls** when it is selected
- **Sort & filter** a film or series library, or a music library's albums (button 2): by name, year, rating or date added, ascending or descending; one genre; all, not watched, watched or favourites
- **Search** (button 1 on the home screen)
- **Detail page**: synopsis (all of it with − when the page cuts it short), rating, genres, cast, audio and subtitle tracks (those the server picks for you to start with: your subtitle mode and languages, the file's default and forced tracks), several **versions** of a film, **favourites** (button 1, also on a series), **watched / not watched** (+, also on a row of the episode list)
- **Special features**: trailers, featurettes, behind the scenes... of a film (button 2 on its page) or a series (after its seasons)
- **Scenes**: the chapters of a film or an episode (the Scenes row of its page), with Jellyfin's picture of each: play from one
- **Series**: seasons (as posters, or a list with the season's cover) and episodes, **shuffle** (button 2), next / previous episode across seasons
- **Playlists of videos**: played one after the other from the one picked, or shuffled (button 2)
- **Photos**: a photo library's pictures on the whole screen, left / right through the folder, a **slideshow** with A
- **Japanese, Chinese and Korean** titles and subtitles: Japanese built in, Chinese and Korean with the font of `apps/WiiFin/fonts/` (in the Homebrew Channel package)

### Video

- Played by the integrated MPlayer CE engine: **direct play** of what the Wii decodes in real time (SD DivX/Xvid, MPEG-1/2, H.264 up to 480p, VP8; AVI, MKV, MP4, MPEG-TS/PS; seeks in the file itself), server-side transcoding for the rest, and back to transcoding when a file does not play smoothly (see [DIRECT_PLAY.md](../DIRECT_PLAY.md))
- **Player**: seek bar with the server's trickplay thumbnails (Jellyfin 10.9+), left / right held to seek on, |◀ ▶| from chapter to chapter, volume, audio and subtitle tracks (text subtitles drawn by WiiFin, so the video can still play as it is), zoom (fit / fill)
- **Overlay Navigation**: on GameCube and Classic Controllers, D-Pad or Left Stick navigates the on-screen controls with an animated pulsing focus ring, and **A** triggers the selected action. Press **B** to hide the overlay.
- **Live Volume HUD**: real-time floating volume card (0–100% and mute cross) and transport bar volume badge during adjustments.
- **Subtitles**: OFF by default, with automatic preferred language matching against media tracks.
- **Skip intro / recap / credits** buttons from the server's media segments (Jellyfin 10.10+ with a plugin such as Intro Skipper), and **Next episode** during the credits
- **Resume** where you left off; progress, pauses and the play method (direct or transcoded) reported to the server, each console a device of its own
- A slow connection lowers the quality by itself; a picture that gets stuck is retried another way, with a message that says what happens

### Music

- Libraries in tabs as on Jellyfin: **Albums, Suggestions, Artists, Playlists, Songs** (a library of loose files opens on its songs)
- A **Now Playing** screen with the cover, the sound live in five visualizers (bars, ring, bubbles, wave, blocks: up twice, then A), an Up Next queue, shuffle, repeat, favourites, and similar tracks once the queue runs out
- **Shuffle** (button 2): an album, a playlist, or songs drawn from the whole library
- **Lyrics** (Jellyfin 10.9+: an `.lrc` beside the song, or embedded): the player's last visualizer, the line sung lit as it comes, on the whole screen once the controls fade away, to sing along
- The music **goes on while browsing**: B while a song plays; the footers say what plays, and **Now Playing** in the profile menu opens the player again (a video stops the music)

### Remote control

From the Jellyfin dashboard or apps:

- **Play on... Nintendo Wii** (films, episodes, music; Play next and Add to queue for music)
- Play / pause, stop, seek, next / previous, volume and mute (the dashboard's slider follows), audio and subtitle tracks
- **Messages** sent from the dashboard show over any screen

### And also

- The interface in the **Wii's ten languages**: English, French, German, Spanish, Italian, Dutch, Japanese, Korean, Simplified and Traditional Chinese, the Wii's own by default (French reviewed, the others waiting for native speakers: see [lang/README.md](../lang/README.md))
- **Wii Remote** pointer, **Classic Controller**, **Wii U Pro Controller**, **Wii U GamePad** (Virtual Console injects) and **GameCube controller**: everything works with the D-pad, no sensor bar needed (see [Controls](#-controls))
- **Interface sounds** and **background music**, both replaceable with your own files (see [Custom sounds](#-custom-sounds))
- On-screen keyboard; screen area calibration for TVs that crop the picture; the disc slot light can stay off
- Ships as a ready-to-use `.dol`, an installable `.wad` (Wii / vWii) and a Homebrew Channel package

## 🎮 Controls

WiiFin-XE supports all standard Wii input methods: the **Wii Remote (Wiimote) & Nunchuk**, the **GameCube controller** (all 4 ports), the **Classic Controller / Classic Controller Pro**, the wireless **Wii U Pro Controller**, the **Wii U GamePad** (in Virtual Console injects), and standard **USB mice and keyboards**.

The pointer is optional: every screen works completely with the D-pad and analog sticks.

| Wii Remote (Wiimote) | Classic / Wii U Pro / GamePad | GameCube controller | Action |
|---|---|---|---|
| A / B | A / B | A / B | Select / Back |
| D-pad | D-pad or left stick | D-pad or stick | Move (hold to repeat); left / right: A-Z in lists, −10 / +10 s in the player (held: on seeking) |
| − / + | − / + or L / R | L / R | Tabs, pages, previous / next episode or track; + : watched / not watched (a film, an episode); − : the whole synopsis (a page) |
| 1 | Y | Y | Search (home), favourite (a film, a series), audio track (player), Up Next (music) |
| 2 | X | X | Sort & filter (lists, music albums), special features (a film), shuffle (series, music), subtitles (player) |
| HOME | HOME | START | HOME menu |
| pointer | ZL / ZR | Z | Video zoom (fit / fill); in the menus, what + does on its own: watched / not watched, the browse page, the keyboard's Enter |

On-screen hints show the buttons of the controller used last (GameCube: A green, B red, X / Y beans, L / R, Z, START).

- **Several controllers:** all four Wii Remotes, Wii U Pro Controllers, and all four GameCube ports work at once.
- **Wii Remote & Nunchuk:** use the IR pointer or D-pad; the Nunchuk's stick acts as the D-pad, C is − and Z is +.
- **Wii U Pro Controller:** syncs wirelessly over Bluetooth. Supports dual analog sticks, rumble, and uses Classic Controller button hints.
- **GameCube Controllers:** dual-analog sticks are fully utilized. By default, C-Stick scrubs playback (left/right ±10 s, held seeks continuously) and D-Pad controls volume (up/down). Roles can be customized in `Settings > Playback > Controller`.
- **In-Player Overlay Navigation:** when transport controls are visible, the D-Pad or stick highlights buttons directly with a pulsing ring, and **A** executes the button. Press **B** to hide the overlay.
- **Rows home header:** L goes to Search, R goes to Menu. In the header, L / R and Left / Right go round the buttons.
- **On-screen keyboard:** − deletes, 1 types a space and 2 switches to symbols (Classic: −, y, x; GameCube: X, Y, R, and L for Shift). B deletes too, and leaves when the text is empty.

### USB Mouse and Keyboard

Standard USB mice and keyboards plugged into the Wii's rear USB ports work out of the box:

- **USB Mouse:** moves the on-screen pointer; left-click is A (select), right-click is B (back), and the scroll wheel navigates lists or seeks playback. Moving or clicking the mouse displays an arrow cursor; pointing a Wii Remote switches back to the hand pointer.
- **USB Keyboard:** allows direct typing into search bars, profile names, and server address fields. The keyboard also fully operates the interface:

| Key | Arrows | Enter | Esc | F1 / F2 | Page Up / Down | Home | F3 |
|---|---|---|---|---|---|---|---|
| Does | D-pad | A | B | 1 / 2 | − / + | HOME | Zoom |

## ⚙️ Settings

Settings is one screen:
- The sections are on the left: **Search**, **View**, **Playback**, **System**, and **Sign-In** for a profile.
- The open section's settings are on the right, in groups under headers. A on a header folds the group away or opens it again.
- Under the list, the highlighted setting explains what it does.
- **Search** (or 1 anywhere in Settings) finds a setting by any word of its name or description.

From a profile's menu, View holds that profile's own look. From WiiFin's main menu, it holds the look WiiFin starts with.

**View**

| Group | Setting | |
|---|---|---|
| Theme | Theme | Light, Dark, Flix, Luhu (black, silver and green), Sushi (dark grey and orange), or one of your own |
| | Customize Theme | Change any colour, then save it as a theme of your own, or remove one you saved (see [THEMES.md](THEMES.md)) |
| | Backgrounds | Soft gradients, or solid colours for TVs that show bands in them |
| Home and Libraries | Home Screen * | Rows or a grid |
| | Library View * | Posters, a list, or a list with the cover |
| | Display Libraries * | The libraries shown on your home |
| | Start Screen | The page the rows home opens on: Browse or Home |
| | Other Rows | Shaded under the row you're on, or all shown plainly |
| | Header Buttons | Icons (Search, Home, Browse, Menu) or Classic |
| Screen and Keyboard | Screen Area | Shrink the interface when the TV crops its edges; save one per TV |
| | Keyboard Size | Normal, Large or Larger |

\* in a profile's settings only

**Playback**

| Group | Setting | |
|---|---|---|
| Video | Video Quality | The transcoding bitrate, from Low (1.5 Mb/s) to Max (5 Mb/s, for a wired adapter); WiiFin also measures the connection and stays under it |
| | Direct Play | Play the files the Wii can decode as they are, without the server converting them |
| | Smooth Motion | Blend frames on picture changes, so 24 fps films move evenly on 60 Hz TVs |
| Controller | Scrub Control | Choose which control scrubs playback on GameCube controllers: C-Stick (default) or D-Pad |
| | Volume Control | Choose which control adjusts volume on GameCube controllers: D-Pad (default) or C-Stick |
| Subtitles | Subtitles Default | Whether subtitles are Off (default) or On when playback begins |
| | Preferred Subtitle Language | Preferred language to match against stream subtitle tracks (Any, English, French, Spanish, German, Italian, etc.) |
| Speaker and Disc Light | Wii Remote Speaker | Off, Wii Remote only, or TV + Remote |
| | Disc Slot Light | The light pulsing with the sound during playback, or off |

**System**

| Group | Setting | |
|---|---|---|
| Sound | Background Music / Interface Sounds | The menus' music and sounds |
| Screensaver | Screensaver | Off, or after 1 to 30 minutes untouched (or paused) |
| | Screensaver Logo | WiiFin, DVD, VHS, CD, or your own pictures |
| | Screensaver Speed | Slow, Normal or Fast |
| | Screensaver Colours | A new colour at each edge, a slow fade, or off |
| Clock and Language | Clock | 24-hour, or 12-hour (AM/PM, the date as month/day) |
| | Language | The Wii's, or one chosen here |
| Buttons and Exit | Exit To | Where Exit and the HOME menu go: the Homebrew Channel or the Wii Menu |
| | Power Button / Reset Button | Power Off, Homebrew Channel, WiiMC or Wii Menu |
| Connection | SSL Verification | Check the server's HTTPS certificate (off for self-signed ones) |

**Sign-In** (a profile's own)

| Setting | |
|---|---|
| Sign-In | At once, a four-digit passcode, or the password each time; it applies when you leave Settings |
| Open at Start-Up | Open this profile when WiiFin starts (hold B at start to skip) |

## 🔊 Custom sounds

Put your own sounds in `SD:/apps/WiiFin/sounds/` (next to `wiifin.cfg`): a file there replaces the built-in sound of the same name, the others stay. They are read when WiiFin starts.

| File | Played when |
|---|---|
| `move` | the highlight moves (lists, rows, grids, settings) |
| `open` | a show, film, season or library opens |
| `back` | B goes back |
| `page` | a page or tab turns (− / +) |
| `play` | a video or a song starts |
| `start` | a button is pressed in the main menu, a setting changes |
| `select` | the pointer moves onto a main menu button |
| `press_key` / `backspace` | a key of the on-screen keyboard |
| `loading` | a loading screen lasts (played in a loop) |
| `menu_enter` / `menu_exit` | the HOME menu opens / closes |
| `bgm` | background music (MP3 only, up to 16 MB, played in a loop) |

Each sound is `name.mp3` or `name.wav` (PCM, 8 or 16-bit, mono or stereo, up to 48 kHz), at most 2 MB and 10 s (longer sounds are cut). A file WiiFin cannot read is ignored, and the log says why. Settings > Interface Sounds turns them all off; Background Music turns off the music.

## 📁 Files on the SD card

Everything is in `SD:/apps/WiiFin/`:

| | |
|---|---|
| `boot.dol`, `meta.xml`, `icon.png` | WiiFin-XE itself |
| `wiifin.cfg` | Settings and profiles (each profile keeps a sign-in token, never a password) |
| `wiifin.log` | What happened last time; add it to a bug report |
| `themes/` | Your own themes (`.theme` files); removed ones go to `themes/removed/` |
| `pictures/` | PNG or JPEG pictures for profiles and the screensaver |
| `sounds/` | Your own interface sounds and menu music |
| `fonts/` | The Chinese and Korean font, and any other `.ttf` / `.otf` to use |
| `screensaver.png` / `.jpg` | A screensaver picture of your own |

## ⚠️ Known limitations

- Direct play only for what the Wii decodes in real time (SD resolutions, see [DIRECT_PLAY.md](../DIRECT_PLAY.md)); the server converts the rest
- Stereo output only (multi-channel audio is mixed down)
- Picture subtitles (PGS, VobSub) are burned into the video by the server; text subtitles are drawn by WiiFin
- The Wii Remote Speaker setting doesn't make a sound from the remote yet
- While a song plays, the interface sounds and the menu music are silent (the Wii's audio is the player's)
- Chinese and Korean, titles and the interface alike, need the font in `SD:/apps/WiiFin/fonts/`: the Homebrew Channel package has it; with the WAD, copy that folder from the package (or `data/fonts/cjk/`) there. Without it, WiiFin doesn't offer those languages (English instead); any other `.ttf` / `.otf` there is used too, for what the other fonts lack
