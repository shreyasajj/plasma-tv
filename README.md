# ![](https://github.com/jclsn/flimmer/blob/main/data/icons/hicolor/48x48/apps/flimmer.png)  Flimmer 

A fullscreen, remote-controllable live TV app for
[Plasma Bigscreen](https://plasma-bigscreen.org), for [TVHeadend](https://tvheadend.org)
and M3U playlists with an XMLTV guide (e.g. [Dispatcharr](https://github.com/Dispatcharr/Dispatcharr)).
It is built with Qt 6, Qt Quick, Kirigami and
[Bigscreen UI](https://invent.kde.org/devinlin/bigscreen-ui). It follows Plasma
theming and uses Breeze Dark by default.

## Features

* Channel list with logos
* TV guide
* TVHeadend or M3U + XMLTV as the source
* Navigation with TV remote buttons

## Screenshots

**Watching**: the info bar shows the channel, the current programme with its
progress and what's on next. Up/down zaps, left opens the channel list and OK
pauses.

![Watch mode with the info bar over live video](img/watch.png)

**Live TV**: channel logos, the programme on air with its progress and the
time left, plus details and "up next" for the highlighted channel. The channel
that is playing shows through behind the menus.

![Live TV page with the channel list and programme details](img/live-tv.png)

**Settings**: the source (TVHeadend or M3U + XMLTV), server address,
credentials (the password is stored in the wallet), playback options, days of
guide data and color scheme, next to the connection status.

![Settings page](img/settings.png)

## Install

Download a package from the
[latest release](https://github.com/jclsn/flimmer/releases/latest).

**Flatpak** (KDE Linux, or any distribution with Flatpak): pick the bundle for
your machine (`x86_64` for PCs, `aarch64` for ARM boards like the Raspberry
Pi), open it in Discover or run

```sh
flatpak install --user flimmer-<version>-x86_64.flatpak
```

Flimmer then shows up as "Live TV" on the Plasma Bigscreen home screen. The
bundle doesn't update itself; install the one from a newer release to update.

**Arch Linux** (x86_64): download both `.pkg.tar.zst` files and install them
together:

```sh
sudo pacman -U bigscreen-ui-*.pkg.tar.zst flimmer-*.pkg.tar.zst
```

## Build

Supported on **Ubuntu** (26.10 and later) and **Arch Linux**, on both x86_64
and ARM64 (aarch64, including Arch Linux ARM). The code has no
architecture-specific parts, so build natively on the target machine.

### Arch Linux / Arch Linux ARM

Build and install a proper package with the bundled PKGBUILDs. Bigscreen UI
isn't in the Arch repositories yet, so build and install it once first:

```sh
sudo pacman -Syu --needed base-devel git   # toolchain (gcc, binutils, make)
cd packaging/arch/bigscreen-ui
makepkg -si        # Bigscreen UI
cd ..
makepkg -si        # installs the dependencies, builds and installs the package
```

The PKGBUILD builds the committed state of this git repository. For a
development build, install the dependencies and use CMake directly:

```sh
sudo pacman -S --needed base-devel cmake git qt6-base qt6-declarative qt6-multimedia \
    qt6-multimedia-ffmpeg qt6-svg qtkeychain-qt6 kirigami qqc2-desktop-style breeze breeze-icons
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
```

If CMake reports that the C++ compiler "is not able to compile a simple test
program", the toolchain itself is broken, usually because of a partial upgrade
(`gcc`/`glibc`/`binutils` out of sync). Run `sudo pacman -Syu`, delete the
build directory and configure again.

### Ubuntu

Ubuntu doesn't package Bigscreen UI, so it is built along with the app from the
`bigscreen-ui` submodule:

```sh
sudo apt install cmake g++ git qt6-base-dev qt6-declarative-dev qt6-declarative-dev-tools \
    qt6-multimedia-dev qtkeychain-qt6-dev libqt6sql6-sqlite \
    extra-cmake-modules gettext libkf6i18n-dev libkirigami-dev \
    qml6-module-qtmultimedia qml6-module-qtquick-controls \
    qml6-module-org-kde-kirigami qml6-module-org-kde-desktop kde-style-breeze qt6-svg-plugins
git submodule update --init
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DUSE_SYSTEM_BIGSCREEN_UI=OFF
cmake --build build -j$(nproc)
sudo cmake --install build   # optional: installs to /usr/local with a launcher entry
```

### Running

```sh
./build/flimmer            # fullscreen
./build/flimmer --windowed # for development (F11 toggles)
```

Kirigami, [Bigscreen UI](https://invent.kde.org/devinlin/bigscreen-ui), the
Plasma Qt Quick Controls style and Breeze (style and color schemes) are used at
runtime only, so no KF6 `-dev` packages are needed unless Bigscreen UI is built
too. It isn't packaged by distributions yet: on Arch, use
`packaging/arch/bigscreen-ui`; elsewhere, run `git submodule update --init` and
configure with `-DUSE_SYSTEM_BIGSCREEN_UI=OFF` to build it along with
Flimmer (this needs ECM and the Kirigami and KI18n development files, and
Qt 6.11 or later).

## M3U + XMLTV

Under *Settings → Source* choose **M3U + XMLTV** and enter the playlist
address, e.g. `http://<host>:9191/output/m3u` for Dispatcharr. The guide address
can stay empty if the playlist names it in its header. Recordings and streaming
profiles need TVHeadend.

## Remote control

| Key (remote / keyboard)                | Action                                      |
|----------------------------------------|---------------------------------------------|
| Arrows                                 | Navigate                                     |
| OK / Enter                             | Select, watch, open programme details; pause/resume while watching |
| Play/Pause / P                         | Pause/resume while watching                 |
| Back / Esc / Backspace                 | Go back (from the top bar: return to video); while watching: ask to quit (can be turned off in Settings) |
| Menu / Settings / F2                   | Settings                                    |
| Guide / G / F3                         | TV guide                                    |
| 0-9                                    | Jump to channel number                      |
| Ch+/Ch- / PgUp/PgDn                    | Zap (watching) or page through lists        |
| ⏭ / ⏮ (or F / R) in the guide        | +/- 24 hours                                |
| Info / I                               | Info bar while watching                     |
| Audio / A, Subtitle / S                | Cycle audio track / subtitles               |
| Stop / X                               | Stop playback                               |

Remotes using HDMI-CEC (kernel CEC input) or LIRC/ir-keytable mapped to these
key codes work out of the box.
