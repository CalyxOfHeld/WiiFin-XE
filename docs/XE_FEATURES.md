# What WiiFin-XE adds

WiiFin-XE is a modified version of [WiiFin](https://github.com/fabienmillet/WiiFin) by fabienmillet. Everything WiiFin does is still here (see the [user guide](USER_GUIDE.md)). This page lists what the XE version changes or adds.

- [Home screen](#home-screen)
- [Settings](#settings)
- [Themes](#themes)
- [Profiles](#profiles)
- [Controllers and input](#controllers-and-input)
- [On-screen keyboard](#on-screen-keyboard)
- [Screen area](#screen-area)
- [Screensaver](#screensaver)
- [Series and seasons](#series-and-seasons)
- [The Wii itself](#the-wii-itself)
- [WiiMC for local media](#wiimc-for-local-media)
- [Faster loading](#faster-loading)
- [Defaults](#defaults)

## Home screen

The rows home has two pages:
- **Home** has Continue Watching, My Favorites, Next Up, New Additions, Top Rated and Favorite Albums.
- **Browse** has your libraries, then one row per library.

**Header buttons:** Search, Home, Browse and Menu, each with an icon. The page you're on has its icon filled in.
- L focuses Search, R focuses Menu. Inside the header, L/R and Left/Right wrap around.
- B on the home page opens the menu.
- The older header (search, one Home/Browse button and your profile's circle) is still there as Settings > View > **Header Buttons: Classic**.

**New Additions and Top Rated** show every library at once.

**Expand (button 2)** opens the row you're on as a page of up to 100 titles in the library layout.
- On that page, 1 changes the layout, −/+ turn pages, A opens a title and B goes back.
- Rows with parts (New Additions, Top Rated and each library on Browse) list them on the left: **All** first, then each library or genre.
- On those rows, 2 also changes the order: Date Added (newest or oldest), Release Date, Title A-Z, Rating.
- In the List + Posters layout, New Additions shows the date each title was added.

**Display Libraries** chooses which libraries show on your home. Hidden ones still open from the list. You can open it from Settings > View, or with 2 on the Libraries row.

**1 View**, inside a library, switches between Posters, List and List + Posters. It changes your Library View setting too.

**Other Rows** (Settings > View): **Shaded** fades the rows under the one you're on. **Shown** draws every row plainly.

**Start Screen** sets which page the rows home opens on. It's in a profile's Sign-In settings, and under View in WiiFin's main menu settings.

## Settings

All the settings are on one screen:
- The sections are on the left: Search, View, Playback, System, and Sign-In for a profile.
- The open section's settings are on the right, with a scroll bar beside them.
- Under the list, the highlighted setting explains what it does.

Each section is split into **groups with headers**: Theme, Home and Libraries, Screen and Keyboard, Video, Speaker and Disc Light, Sound, Screensaver, Clock and Language, Buttons and Exit, Connection. A on a header folds that group away or opens it again. Every group starts open.

**Search** (A on it, or 1 from anywhere in Settings) uses the on-screen or USB keyboard. It lists every setting whose name or description has what you typed.

## Themes

The five built-in themes stay: Light, Dark, Flix, Luhu and Sushi. You can also make your own.

**Customize Theme** (Settings > View) edits eleven colours, text included:
- You pick colours on an HSV square, with Hue/R/G/B sliders, or by typing a code (`#RRGGBB` or `r,g,b`).
- Changes show as you make them. **Save As** writes a theme file.
- **Accent** colours the filled things: switches, tabs, sliders, progress bars.
- **Selection** colours the ring around what's highlighted, so the two can differ.
- If a theme's highlight ring would be hard to see against its cards, WiiFin draws the ring in the text colour instead.

**Remove** gets rid of a theme you saved. It asks first, with Keep highlighted. The file moves to `themes/removed/` on the SD card, so a PC can still put it back.

Theme files are short text files in `sd:/apps/WiiFin/themes/`, easy to share and to edit on a PC. The format: [THEMES.md](THEMES.md).

## Profiles

**Profile pictures:** 2 on "Who's watching?" opens a picker with three groups:
- **Pictures:** PNG or JPEG files you put in `sd:/apps/WiiFin/pictures/`.
- **Miis:** the Miis of this Wii, drawn as simple portraits from their Mii Channel data.
- **WiiFin:** your profile's letter, or the jellyfish. New profiles start with the jellyfish.

**Edit Profile:** 1 (Y on a Classic Controller or GameCube controller) on "Who's watching?" lists a profile's details, each with its value:
- **Nickname:** a name of its own. The user name shows dimmed after it. Leave it empty to go back to the user name.
- **Server URL**, **Username** and **Password:** open the sign-in screen with the server and user name filled in. The password isn't kept on the Wii, so changing any of the three signs in again. Save keeps the profile's nickname, picture, look and passcode. Signing in as another account also clears its hidden libraries.
- **Picture:** the same picker as 2.

**Open at Start-Up** (a profile's Sign-In settings) skips the profile list and opens that profile's home when WiiFin starts. Hold B while WiiFin starts to pick another.

**Deleting a profile** asks first. Cancel is highlighted, and Left/Right picks between the two.

The **profile menu** has an icon on each entry. It also has **Local Media (WiiMC)** and **WiiFin Main Menu**.

## Controllers and input

- **Every controller at once:** all four Wii Remotes (with their Nunchuks and Classic Controllers), wireless Wii U Pro Controllers (Bluetooth), Nintendo Switch Pro Controllers (USB), Microsoft Xbox One controllers (USB), Sony PlayStation controllers (DualShock 4 and DualShock 3 over USB), and all four GameCube ports. DualSense, Xbox 360 and generic USB gamepads should also work. The on-screen hints follow the controller you pressed last.
- **Wii U Pro Controller:** syncs wirelessly to the Wii via Bluetooth. Features dual analog sticks, rumble support, and seamless Classic Controller button mapping. (Note: the Wii U Pro Controller is wireless only due to hardware limitations—its USB port only charges and has no data lines).
- **Nintendo Switch Pro Controller (USB):** tested and working plugged into either rear USB port (including analog sticks and hot-plugging while running). Automatically performs high-speed USB handshake and delivers 60Hz telemetry (Report 0x30/0x21 and Report 0x3F). Maps seamlessly to Nintendo Classic button hints (A, B, X, Y, +, −, and Home).
- **Microsoft Xbox One Controllers (USB):** tested and working plugged into either rear USB port (including analog sticks and hot-plugging while running). Features automatic GIP wake-up initialization, dual analog sticks, D-pad, LT/RT analog triggers, LB/RB bumpers, and Xbox Guide home button. Pressing **View + Menu** acts as a fallback HOME button. (Note: wired Xbox 360 controllers should work. The **Xbox Series X|S** controller does not work because the Wii's USB system drops it during setup).
- **Native Xbox Vector Badges:** dynamic on-screen prompt hints render authentic vector shapes and colors: green A, red B, blue X, yellow Y, LB/RB badges, LT/RT triggers, Menu/View, and the Xbox Guide button.
- **Sony PlayStation Controllers (DualShock 4 & DualShock 3 over USB):** tested and working plugged into either rear USB port (including analog sticks and hot-plugging while running). Full dual analog sticks, D-pad, face buttons, shoulder triggers, options, and PS home button. DualShock 3 activates automatically upon connection. DualSense should also work.
- **Native PlayStation Vector Glyphs:** dynamic on-screen prompt hints render authentic vector shapes and colors: blue Cross (✕), red Circle (○), pink Square (□), green Triangle (△), L1/R1, L2/R2 badges, Options/Share, and PS Home.
- **Generic USB HID Gamepads:** PC USB gamepads, DragonRise arcade boards, and retro USB pads (SNES/NES style) should work. Pressing **Select + Start** together acts as the HOME menu button.
- **Comfortable Shoulder Navigation:** L1/LB and L2/LT comfortably navigate backward (previous tab/page/scroll); R1/RB and R2/RT navigate forward (next tab/page/scroll). Video zoom is mapped to R3 / RS (stick click) to prevent accidental trigger zooming.
- **Configurable Playback Controls:** in `Settings > Playback > Controller`, configure whether the GameCube / Gamepad right stick (C-Stick) or D-Pad handles scrubbing versus volume:
  - **Scrub Control:** C-Stick (default) or D-Pad. Left/right nudges playback by ±10 seconds; holding seeks continuously.
  - **Volume Control:** D-Pad (default) or C-Stick. Up/down adjusts volume in 5% increments.
- **In-Player Overlay Navigation:** on GameCube, Classic, PlayStation, Xbox, Generic Gamepad, Switch Pro, and Wii U Pro Controllers, when the player controls are visible, the D-Pad or Left Stick navigates the on-screen buttons (|◀◀, −10s, Play/Pause, +10s, ▶▶|, Zoom, and Skip Intro when available). The focused button pulses with an animated selection glow, and pressing **A** (or ✕) executes the action. When a skip button appears (Skip Intro, Next Episode), it automatically takes the controller's focus so pressing **A** activates it. When controls are hidden, D-Pad Left/Right provides immediate ±10 second seeking (holding scrubs continuously). Pressing **B** (or ○) dismisses the overlay back to clean video playback.
- **Seek bar navigation:** with the transport controls displayed, pressing Up moves onto the seek bar. Left and Right move 30 seconds at a time; holding down accelerates (60 seconds, then 3 minutes per step). Pressing **A** seeks immediately, or it seeks automatically 2 seconds after the last press. Pressing **B** cancels and returns the marker to where playback was. The stream restarts only once, not on every press. While controls are visible, D-pad Up and Down move between any skip prompt, the seek bar, and the transport buttons (Up/Down returns to controlling volume when controls are hidden).
- **Playback Controller Settings:**
  - **Episode Buttons** (Settings > Playback > Controller): on by default. When turned off, + and − no longer change the episode, and |◀◀ / ▶▶| only skip between chapters.
  - **Ask Before Stopping** (Settings > Playback > Controller): on by default. Pressing B prompts with "Stop watching?" and Stop / Keep watching choices to prevent accidental exits.
  - **Ask Before Next Episode** (Settings > Playback > Controller): going to the next or the previous episode asks first.
  - An episode you already started asks **Continue** or **From Start** when you go to it.
  - **1** hides the player's controls; **2** opens the subtitle tracks, then the audio tracks.
- **Skip Intro settings** (Settings > Playback > Skip Intro):
  - **Skip Intro Button** turns the skip intro / recap / credits button on or off. It needs the Intro Skipper plugin on your server ([how to set it up](FAQ.md#skip-intro-doesnt-appear)).
  - **Show Skip Intro After** keeps the button hidden until you've watched that many episodes of the series (Always, or 1 to 10; default 3), so a new show's opening plays the first few times.
  - **Skip Intro (this show)**, in a series' episode list (2, Options), overrides both for that series: My Setting (follow Settings), Off, Always, or After 1 to 10. Each profile keeps its own.
- **Live Volume HUD:** adjusting volume or muting displays a floating on-screen card showing the exact volume percentage (0–100%) and mute indicator (`×`), mirrored by a live volume badge on the transport bar.
- **Nunchuk:** the stick works as the D-pad, C is − and Z is +.
- **C-Stick** (GameCube / Gamepad Right Stick) works as the D-pad everywhere in menus.
- **Button pictures** match the controller: the GameCube's green A, red B and X/Y beans, the Classic's a/b/x/y and ZL/ZR, PlayStation's ✕/○/□/△, Xbox's green A / red B / blue X / yellow Y, and a house for HOME.
- **USB mouse:** it moves the pointer. The left button is A, the right button is B, and the wheel scrolls. Wired mice and many wireless receivers work. Point or press a Wii Remote to take the pointer back.
- **USB keyboard:**

  | Key | Does |
  |---|---|
  | Arrows | D-pad |
  | Enter | A |
  | Esc | B |
  | F1 / F2 | 1 / 2 |
  | Page Up / Page Down | − / + |
  | Home | HOME |
  | F3 | Zoom |

  Typing goes straight into text fields. The hints show key caps while you use it.

## On-screen keyboard

Shortcuts on each controller:

| | Wii Remote | Classic | PlayStation | Generic USB | GameCube |
|---|---|---|---|---|---|
| Delete | − | − | □ / L1 | Btn 5 / L1 | X |
| Space | 1 | y | △ | Btn 4 | Y |
| Symbols | 2 | x | ○ | Btn 3 | R |
| Shift | (the key) | (the key) | (the key) | (the key) | L |

B (or ○) also deletes, and leaves when the text is empty.

**Server URL:** the keyboard has **http://** and **https://** keys where Space would be. Each puts its prefix in front of what you typed, replacing the other one if it's there. Type the address, then add the prefix.

**Keyboard Size** (Settings > View): Normal, Large or Larger. The labels grow with the keys.

## Screen area

For TVs that crop the picture (overscan), mostly older CRTs:
- **Snaps:** −/+ step through the picture's shapes (16:9, 16:10, 4:3) and Small/Medium/Large margins. The D-pad still moves each corner a pixel at a time.
- **A clear edge:** the edge of the picture is drawn in white and red stripes. Bring the arrows in until the white shows all the way round.
- **One per TV:** 1 saves the area under a name. −/+ also go through your saved screens, and 2 forgets the one you're on.
- The interface follows the area's shape, so a 4:3 area on a 16:9 picture isn't squashed. Nothing spills into the margins.

## Screensaver

After a while with nothing pressed, or with a video or song paused, a logo drifts around a dark screen. The press that wakes it does nothing else.

| Setting | Choices |
|---|---|
| Screensaver | Off, 1, 3, 5, 10, 15 or 30 minutes |
| Screensaver Logo | WiiFin, DVD, VHS, CD, or your own pictures from `apps/WiiFin/pictures/` |
| Screensaver Speed | Slow, Normal, Fast |
| Screensaver Colours | Each Edge, Fade, Off |

The logo's path is set up so it **really does hit the corners**, every few minutes. A corner hit glows.

The DVD, VHS and CD logos are WiiFin's own drawings in that style, not the trademarked marks.

## Series and seasons

- **+ on a season** (or Mark Season Watched) marks the episodes not watched yet. It asks first, and episodes already watched keep their dates.
- **In the episode list:**
  - Left/Right or −/+ move a page.
  - 1 marks an episode watched or not.
  - 2 opens Options: Sort by (Episode, Title A-Z, Last Watched, Watched Status, Rating, Length), Order, Search (by name or episode number), Mark Season Watched, Shuffle Season, Skip Intro (this show).
  - B clears a search first, then goes back.

## The Wii itself

- **Exit To** (System): Exit and the HOME menu go back to the Homebrew Channel or to the Wii Menu. Started as a channel (WAD), WiiFin always goes back to the Wii Menu.
- **Power Button / Reset Button** (System): each can Power Off, go to the Homebrew Channel, open WiiMC, or go to the Wii Menu.
  - Started as a channel, "Homebrew Channel" finds HBC's channel.
  - If WiiFin is ever stuck, the Power button still turns the Wii off after a few seconds.
- **Wii Remote Speaker** (Playback): Off, Wii Remote only, or TV + Remote. Still being worked on: see [known issues](#known-issues).
- **HOME menu:** its buttons are in a column:
  - **Profile Menu** (with a profile open): closes what's on screen and opens the profile menu (Back to Jellyfin, Switch Profile, Settings...). A video stops and keeps its place; music keeps playing.
  - **WiiFin Main Menu**: asks first, then closes the profile and goes back to the start, where profiles are picked.
  - **Homebrew Channel** (or **Wii Menu**, as Exit To says) and **Restart WiiFin**.

## WiiMC for local media

WiiFin plays what's on your Jellyfin server. For videos and music on the Wii's own SD card or USB drive, **Local Media (WiiMC)** opens WiiMC. It's on the main menu and the profile menu.
- WiiFin looks for `apps/wiimc/` first, then any app named WiiMC, on the SD card and then USB.
- It checks the file is a complete program before starting it. If none is found, a notice says so and WiiFin stays open.
- With **WiiMC-SS XE**, WiiMC's own Settings have a **Jellyfin (WiiFin)** entry, and Exit comes back to WiiFin. A plain WiiMC still plays your files, but it can't come back on its own.

## Faster loading

- The loading spinner turns over a dimmed copy of the screen you were on, not a blank screen.
- Poster grids open at once, and the posters fill in as they arrive.
- The home asks the server for much less while it builds. Each library's parts and genres load when you pick them.

## Defaults

A fresh install starts with settings that suit most TVs:
- subtitles off by default, with automatic preferred language matching when enabled
- playback controls default to C-Stick scrubbing and D-Pad volume on GameCube controllers
- interface sounds and menu music off
- Video Quality Max, held under what your connection measures
- solid backgrounds, a 24-hour clock and the Larger keyboard
- the screensaver after 1 minute (VHS logo, Slow, Fade)
- the disc slot light off

Everything can be changed in Settings.

## Known issues

- **Wii Remote Speaker:** the speaker doesn't make a sound yet, in either mode.
- **Translations:** the strings XE added or reworded show in English in every language, until translators catch up (see [lang/README.md](../lang/README.md)).
