# CyberMage: Darklight Awakening — native port

A from-scratch C99 + SDL2 reimplementation of Origin's 1995 DOS game
*CyberMage: Darklight Awakening*, written from reverse engineering of the
original executable. It runs the original game rules on modern Linux, macOS,
Windows and Android (phones and Google/Android TV).

**No game data is included.** You need your own copy of the retail game
(version 1.01F, `CM_HI.EXE`). The port reads the original files directly.

Optional enhancements (all off by default on desktop): a GPU renderer with
widescreen, modern mouse/gamepad controls, touch and TV-remote controls.

## Download

Prebuilt binaries for every platform are attached to each
[release](../../releases) and to every CI run under *Actions → build → Artifacts*:

| Platform | File |
|---|---|
| Linux x86_64 / arm64 | `cybermage-linux-<arch>.tar.gz` (needs the distro SDL2 package: `libsdl2-2.0-0`) |
| macOS arm64 / x86_64 | `cybermage-macos-<arch>.tar.gz` (SDL2 bundled) |
| Windows x86_64 / arm64 | `cybermage-windows-<arch>.zip` (SDL2.dll bundled) |
| Android (phone, tablet, TV) | `cybermage-debug.apk` (arm64-v8a, armeabi-v7a, x86_64, x86) |

## Setting up the game data

Copy **every file** from your installed CyberMage directory (the folder that
contains `CM_HI.EXE`, `HIPAK.RSC`, `SCENE.RSC`, `STRINGS.MSG`, the `PANEL*.DAT`
files, …). Keep the upper-case file names.

### Desktop

Either put the `cybermage` binary inside the game folder and run it from there,
or point it at the folder:

```
cybermage --data /path/to/CYBERMAG
```

Saves and settings go to the per-user SDL folder (for example
`~/.local/share/jshield/cybermage` on Linux, `%APPDATA%\jshield\cybermage` on
Windows); `--user DIR` overrides it.

On macOS the downloaded binary is unsigned: run `xattr -d com.apple.quarantine cybermage libSDL2*.dylib` once.

### Android phone or tablet

1. Install `cybermage-debug.apk` (allow installs from your file manager/browser).
2. Copy the game folder to the device, e.g. `Download/CyberMage`.
3. Start CyberMage, choose **Import folder** and pick that folder. The files are
   copied into the app's private storage once.

### Android / Google TV

TVs usually have no folder picker, so use adb from a computer
(enable *Developer options → USB/Network debugging* on the TV):

```
adb connect <tv-ip>:5555
adb install -r cybermage-debug.apk
adb shell am start -n com.jshield.cybermage/.DataActivity   # creates the data folder
adb shell am force-stop com.jshield.cybermage
adb push /path/to/CYBERMAG/. /sdcard/Android/data/com.jshield.cybermage/files/data/
```

Then start CyberMage from the TV home screen.

## Playing

On Android a launcher dialog picks the display mode (remembered):
GPU widescreen (default), software stretched to fill, or software 4:3.

### Controls

| | Desktop (classic, default) | Modern (`--controls modern`, Android default) |
|---|---|---|
| Move / turn | original DOS keys (set in the game's Options menu) | WASD/arrows move, mouse turns and looks |
| Fire / use / jump | original keys | left mouse / E or Enter / Space |
| Weapons | original keys | wheel or `[` `]`, F1–F3, 1–0 |
| Gamepad | original joystick setup | left stick move, right stick look, RT fire, A jump, X use |

Touch (phones): left virtual stick moves, swipe the right half to turn/look,
on-screen **F**ire, **J**ump, **U**se, **W**eapon, **M**enu, **A**im.

TV remote: D-pad up/down walk, left/right turn, centre tap = use, centre hold =
fire, centre double-tap = jump, Back = menu, hold Back = next weapon.
Vertical aim is automatic. A Bluetooth gamepad gives full control.

All bindings can be changed in `controls.cfg`; see
[docs/re/controls.md](docs/re/controls.md).

### Command-line options

```
--data DIR             game files (default: ./orig/retail, then the current directory)
--user DIR             saves and settings
--renderer sw|gl       software (original pixels, default) or GPU renderer
--width N --height N   GPU window size
--display fit|fill     software output: centred 4:3 (default) or stretched
--view-size 0..4       starting 3D view size: 0 = full screen, 2 = DOS default
--controls classic|modern
--remote               TV-remote layout (modern controls)
```

## Building from source

The desktop build needs a C99 compiler, `make`, `pkg-config` and SDL2 2.0.18 or
newer.

| System | Install | Build |
|---|---|---|
| Debian/Ubuntu | `sudo apt install build-essential pkg-config libsdl2-dev` | `make -C port` |
| Fedora | `sudo dnf install gcc make pkgconf SDL2-devel` | `make -C port` |
| Arch | `sudo pacman -S base-devel sdl2` | `make -C port` |
| macOS | `brew install sdl2 pkg-config` | `make -C port` |
| Windows | [MSYS2](https://www.msys2.org) UCRT64 shell: `pacman -S make mingw-w64-ucrt-x86_64-{gcc,SDL2,pkgconf}` | `make -C port` |

The binary is `port/build/cybermage` (`cybermage.exe` on Windows; copy
`SDL2.dll` from `/ucrt64/bin` next to it to run outside MSYS2).

### Android APK

Needs a JDK (17 or newer) and the Android SDK (Android Studio installs one;
`ANDROID_HOME` must point to it). Gradle, the NDK and SDL2 are fetched
automatically on the first build:

```
port/android/build.sh                      # arm64-v8a, armeabi-v7a, x86_64
ABIS=arm64-v8a port/android/build.sh       # one ABI, faster
```

The APK is written to `port/android/out/cybermage-debug.apk`.
Details: [port/android/README.md](port/android/README.md).

## Repository layout

| Path | Contents |
|---|---|
| `port/` | the C99 port: `src/` engine, `platform_sdl.c` (only SDL/OS code), `android/` |
| `docs/re/` | reverse-engineering notes: file formats, world, renderer, simulation, AI, events, UI, audio |
| `docs/architecture_port.md` | port architecture and the list of opt-in enhancements |
| `REVERSE.md`, `labels.csv` | reverse-engineering index and symbol labels for `CM_HI.EXE` |
| `tools/` | analysis and comparison tools used against the original executable |

Third-party code: [Nuked-OPL3](https://github.com/nukeykt/Nuked-OPL3) (LGPL 2.1,
`port/src/audio/COPYING.Nuked-OPL3`) and SDL2 (zlib). CyberMage is © Origin
Systems / Electronic Arts; this project contains none of its data.
