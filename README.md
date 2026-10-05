<h1 align="center">VitaXMB</h1>

<p align="center">
  <b>A PSP-style XMB launcher for the PlayStation Vita.</b><br>
  The cross media bar, rebuilt for the Vita: your games and homebrew, music, settings and system apps in the layout, motion and sound of the original.
</p>

<p align="center">
  <img src="docs/demo.gif" alt="VitaXMB demo: startup animation, category bar, Settings pages, music player" width="720">
</p>

<p align="center">
  <img alt="Platform" src="https://img.shields.io/badge/platform-PS%20Vita%20(HENkaku%2FEnso)-blue">
  <img alt="Language" src="https://img.shields.io/badge/language-C-lightgrey">
  <img alt="License" src="https://img.shields.io/badge/license-GPL--3.0-green">
</p>

---

## Contents

- [Features](#features)
- [Screenshots](#screenshots)
- [Controls](#controls)
- [Installing](#installing)
- [Building from source](#building-from-source)
- [How it works](#how-it-works)
- [Game artwork](#game-artwork)
- [Music and video](#music-and-video)
- [Settings reference](#settings-reference)
- [Repository layout](#repository-layout)
- [Known issues](#known-issues)
- [Credits and licenses](#credits-and-licenses)

## Features

**The XMB, faithfully**

- Six categories on a horizontal bar (Settings, Photo, Music, Video, Game, Network) with the PSP's icon sizes, spacing, glow, drop shadows, rules under selected titles and its monthly color themes with the animated waves.
- The two-level layout the PSP uses for Settings: the open category slides left, the parent list collapses into an icon column, and the child list shows wrench-badge rows with values on the right.
- A wide-icon **game list**: the selected title grows into a landscape tile while neighbours stack above and below. Rest on a game and its full-screen background art fades in behind the list.
- Startup animation (fade in, bar and lists slide in together), the XMB sound set (opening, cursor, cancel, confirm, start game), PSP-style confirmation dialogs and the **Options** panel opened with the triangle button.
- Text is rendered the way the PSP does it: a soft drop shadow, kerned, sub-pixel positioned and glued to its icon while scrolling.

**Made for the Vita**

- **Game** lists every installed title and homebrew bubble (and your saved data), reads each app's own LiveArea artwork, and starts it. *Information* shows title, ID, version and location.
- **Settings** has real, working pages: *System Information* (firmware, nickname, MAC address, model, memory card, battery, CPU clock), plus VitaXMB's own settings (theme color, clock format, sound effects, startup animation, confirmation dialogs, launch method).
- **Music** is an MP3 player with the PSP's player screen: LED spectrum analyzer driven by the real audio, track badge, ID3 title and artist, elapsed and total time, progress bar, next/previous and seeking.
- **Network, Photo, Video** hand off to the Vita's own apps (Internet Browser, PlayStation Store, Party, Messages, Near, Photos, Videos), and Settings opens the Vita's real settings pages.
- **Video** lists the videos in `ux0:pspemu/VIDEO`.

**Engineering**

- Smooth scrolling on a very large game library: icons and backgrounds are decoded on worker threads and uploaded a couple per frame; GPU textures are only freed once the GPU has finished with them.
- Own text engine: FreeType glyphs with light auto-hinting in a padded atlas, HarfBuzz shaping (OpenType kerning), a pre-blurred shadow per glyph.
- A baseline-normalised font so every letter, at every UI size, lands on the same pixel row (see [Fonts](#fonts)).

## Screenshots

| | |
|---|---|
| ![Game category](docs/images/home.png) <br> **Game** - installed titles and saved data | ![Game list](docs/images/game-list.png) <br> **Game list** - wide tiles, background art on rest |
| ![Options panel](docs/images/options-panel.png) <br> **Options** (triangle) - Start, Information, Refresh | ![Settings](docs/images/settings.png) <br> **Settings** - the PSP's settings groups |
| ![System Settings](docs/images/system-settings.png) <br> **Two-level Settings layout** | ![VitaXMB settings](docs/images/vitaxmb-settings.png) <br> **VitaXMB Settings** - values on the right |
| ![System Information](docs/images/system-information.png) <br> **System Information** (nickname and MAC blurred) | ![Confirmation dialog](docs/images/confirm-dialog.png) <br> **Confirmation dialog** |
| ![Track list](docs/images/track-list.png) <br> **Music** - track list with ID3 tags | ![Music player](docs/images/music-player.png) <br> **Music player** - real-time LED spectrum |
| ![Network](docs/images/network.png) <br> **Network** - the Vita's online apps | |

## Controls

| Button | Action |
|---|---|
| D-pad / left stick **Left, Right** | Change category. Inside a Settings page: **Left** goes back, **Right** changes the selected value |
| D-pad / left stick **Up, Down** | Move through the list (hold to repeat) |
| **L / R** | Jump five rows |
| **Cross** | Open / start / change a value |
| **Circle** | Back, close a page or dialog |
| **Triangle** | Options (on a game or on saved data) |

Music player: **Cross** play/pause, **L / R** previous/next track, **Left / Right** seek 10 s, **Circle** leave the player (the music keeps playing).

## Installing

VitaXMB is an *unsafe* homebrew (it reads other apps' folders and launches other titles), so it needs a hacked Vita with **HENkaku / Enso** and *Enable Unsafe Homebrew* switched on.

1. Build `VitaXMB.vpk` (see below) or take it from a release.
2. Copy it to the Vita and install it with VitaShell (Cross on the file, then confirm).
3. Start **VitaXMB** from the LiveArea. Settings are saved in `ux0:data/VitaXMB/config.bin`; failures are logged to `ux0:data/VitaXMB/log.txt`.

Tested on firmware 3.70.

## Building from source

You need the [VitaSDK](https://vitasdk.org/) plus these libraries from its package manager:

```sh
vdpm vita2d libpng libjpeg-turbo freetype harfbuzz zlib bzip2 taihen
```

Then, with `VITASDK` set:

```sh
cmake -S . -B build          # add  -G Ninja  if you use Ninja
cmake --build build
```

The result is `build/VitaXMB.vpk` (and `build/VitaXMB.self`, the `eboot.bin`).

Notes

- On Windows the prebuilt VitaSDK toolchain works with CMake and Ninja (`pip install ninja`); `vdpm` needs a POSIX shell, or you can unpack the package archives from <https://github.com/vitasdk/packages> into the SDK yourself.
- The build links a tiny *weak* import stub (`third_party/vitashell_stub`) for the optional artwork-decryption feature; it is harmless if VitaShell's modules are not present.
- The executable is built with the same auth ID VitaShell uses (`-a 0x2808000000000000`, see `CMakeLists.txt`) so it may start other titles.

## How it works

```
                     +-------------------+
   pad / remote ---> |  main loop (60fps)|----> vita2d (GXM)  <--- text engine (FreeType + HarfBuzz atlas)
                     +---------+---------+
        icon_thread  <---------+---------> bg_thread   (full-screen backgrounds)
        music_thread <---------+---------> audio_thread (mixer: UI sounds + music ring)
                               +---------> dec_thread   (optional artwork decryption)
```

- **Layouts.** Every list is a `Menu` of `Item`s. A menu is drawn by one of three layouts: the category *column*, the wide-tile *game folder*, or the Settings-style *sub list* (`menu_layout()`); transitions between them are eased values (`folder_t`, `sub_t`, `page_t`, `dlg_t`, `opt_t`).
- **Selection emphasis** (brightness, glow, rule) follows an eased per-item `glow` value, not the slide position, so a row lights up the moment it is selected.
- **Art loading.** Rows near the cursor ask the loader thread for their art; it resolves LiveArea files, decodes and downscales PNGs off-thread, and the render thread uploads at most two per frame.
- **Text.** HarfBuzz shapes each string (kerning); glyphs are rasterised by FreeType at four sub-pixel phases into a padded atlas together with a blurred shadow bitmap, then drawn as textured quads.
- **Music.** minimp3 decodes on its own thread into a 48 kHz ring buffer; the audio mixer drains it alongside the UI sounds and publishes the last 1024 samples, from which a Goertzel filter bank computes the 12 spectrum bands.
- **Safety.** Textures are freed on a deferred list after the GPU has gone idle (freeing a texture the GPU still reads crashed the Vita).

### Fonts

The font clone shipped in `assets/font.otf` has been **baseline-normalised**. The original design places flat letters about 17 units below the baseline, round letters 35 units below it, and gives x-height and cap-height tops that differ per glyph. At 20-28 px those fractions get rounded into whole pixel rows, so for instance an *m* looked a row higher than an *a* or *e*. The shipped file remaps every Latin glyph's y-coordinates through one piecewise-linear function that collapses each alignment zone to a single value (baseline 0, x-height 596, cap height 782). Verified on the device by rendering all 94 ASCII glyphs at 20, 22, 24 and 28 px and measuring every glyph's top and bottom pixel row: all aligned.

Any OpenType/TrueType font can be dropped in as `assets/font.otf`.

## Game artwork

Rows show the landscape *gate* image of the app's LiveArea and, on rest, its background. They are found in this order: the app's own `sce_sys/livearea` (homebrew), LiveArea's copy in `ur0:appmeta/<id>/livearea`, and the plain `pic0.png` copy in `ur0:appmeta/<id>`.

Retail games' own files are encrypted, and the Vita only keeps `appmeta` copies for roughly 500 bubbles. For games without one, run **[CopyIcons](https://github.com/cy33hc/copyicons)** once: it copies each game's `pic0.png`/`icon0.png` into `ur0:appmeta`, and VitaXMB picks them up. Games with no artwork at all show their square icon on a landscape plate.

An experimental *Decrypt Game Artwork* setting (off by default, asks for confirmation) can mount a game's encrypted files through VitaShell's own kernel modules, as CopyIcons does, and cache the plain copies. It loads kernel modules and has frozen a test device, so treat it as an experiment.

## Music and video

- Music plays **MP3** files. The system blocks apps from reading `ux0:music`, so VitaXMB scans **`ux0:pspemu/MUSIC`** (Adrenaline's folder). Put your songs there. FLAC and WAV are not supported.
- Videos are listed from `ux0:pspemu/VIDEO`; selecting one opens the Vita's Videos app (a decoder is out of scope).

## Settings reference

**Settings > VitaXMB Settings**

| Setting | Values | Notes |
|---|---|---|
| Clock Format | 24-hour / 12-hour | status bar |
| Sound Effects | On / Off | UI sounds and music mixing are independent |
| Startup Animation | On / Off | fade in + slide in |
| Confirmation Dialogs | On / Off | asks before leaving the XMB |
| Game Launch Method | A-D | the system call and flag used to start a title |
| Decrypt Game Artwork | On / Off | experimental, see above |

**Settings > Theme Settings > Color**: automatic (follows the month, like the PSP) or any of the twelve months. October is measured from a real PSP capture; the other months are approximations.

**Settings > System Settings**: *System Information*, plus shortcuts to the Vita's own Settings pages.

## Development

Release builds contain no debugging code. Configure with `-DVITAXMB_DEBUG=ON` to add:

- `ux0:data/VitaXMB/trace.txt`: breadcrumbs, slow-frame timings (`input / icons / draw / swap`) and launch results.
- A file-based **remote control**: write space-separated commands to `ux0:data/VitaXMB/remote.txt` and the app executes one per frame, then deletes the file. Commands: `left right up down cross circle triangle l r`, `w<frames>` (wait), `shot:<name>` (save `ux0:data/VitaXMB/<name>.png`), `page:text` / `page:off` (glyph test page), `hint:<0-3>` (text hinting mode), `uri:<hexflags>:<uri>` (try a launch request).
- This is how the screenshots and the demo GIF in this README were captured, over an FTP connection to the console.

## Repository layout

```
CMakeLists.txt            build definition (VPK, SELF, assets)
src/main.c                the launcher
sce_sys/                  bubble icon and LiveArea files of the VitaXMB app itself
assets/font.otf           baseline-normalised UI font
assets/icons/             PSP-style XMB icons (PNG)
assets/sounds/            XMB sound effects (48 kHz stereo PCM)
third_party/minimp3/      MP3 decoder (CC0)
third_party/vitashell_stub/  import stub + headers for VitaShell's user module
docs/                     README images
```

Everything in the tree is needed to build; nothing else is committed.

## Known issues

- **Starting a game shows the Vita's LiveArea bubble transition** before the game appears. The system draws it whenever one app starts another, so no launcher (VitaShell and vita-launcher included) can skip it; only a shell-level plugin could.
- **System apps cannot be started by title ID** from an app (the Vita shows error `C2-12570-5`). VitaXMB therefore opens them through their own URI schemes (`wbapp0:` Browser, `settings_dlg:` Settings, `psns:` PlayStation Store, `pspy:` Party, `psnmsg:` Messages, `near:` Near, `photo:`, `music:`, `video:`). The Browser and the Settings pages are confirmed on firmware 3.70; the others come from a working third-party launcher but have not been tried on every setup.
- If launch requests are being refused (the call returns `0x80802026` in a debug build), the console's app manager is in a bad state: reboot the Vita.
- `ux0:music`, `ux0:video` and `ux0:picture` cannot be read by apps; use the Adrenaline folders or the Vita's own apps.
- The PlayStation Network and "Extras" categories of the real PSP are not implemented.
- The *Decrypt Game Artwork* experiment can freeze the console; it is off by default.

## Credits and licenses

VitaXMB is released under the **GNU GPL v3** (see `LICENSE`).

- [VitaSDK](https://vitasdk.org/), [libvita2d](https://github.com/xerpi/libvita2d), [FreeType](https://freetype.org/), [HarfBuzz](https://harfbuzz.github.io/), libpng, libjpeg-turbo, zlib, bzip2.
- [minimp3](https://github.com/lieff/minimp3) by lieff (CC0).
- [VitaShell](https://github.com/TheOfficialFloW/VitaShell) by TheFloW (GPL-3.0) and [CopyIcons](https://github.com/cy33hc/copyicons) by cy33hc (GPL-3.0): the user-module header, the import stub and the PFS-mount approach used for artwork decryption.
- [vita-uriCaller](https://github.com/Freakler/vita-uriCaller) for the list of system URIs.

**Third-party assets.** The XMB icons, sound effects and typeface in `assets/` imitate the look of Sony's PSP system software and the FOT-NewRodin typeface and are included as supplied by the project author for personal, non-commercial use. They remain the property of their respective owners and are not covered by the GPL. If you are a rights holder and want them removed, please open an issue.

VitaXMB is an independent fan project. It is not affiliated with or endorsed by Sony Interactive Entertainment. "PlayStation", "PSP", "PS Vita" and "XMB" are trademarks of their respective owners.
