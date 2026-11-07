# CyberMage Android

The APK contains the C port and SDL2, **no game data**. Supply your own retail
CyberMage directory; DOS file names must keep their upper-case spelling.

## Build

Requirements: JDK 17+ and the Android SDK (`ANDROID_HOME`). The Gradle wrapper
downloads Gradle 9.4.1; the Android Gradle plugin installs NDK 27.2.12479018 and
CMake 3.22.1; `build.sh` clones SDL2 `release-2.30.10` into `third_party/SDL`.

```
port/android/build.sh                          # default ABIs: arm64-v8a,armeabi-v7a,x86_64
ABIS=arm64-v8a,armeabi-v7a,x86_64,x86 port/android/build.sh
```

Output: `port/android/out/cybermage-debug.apk` (debug signing).

| Variable | Default | Purpose |
|---|---|---|
| `ABIS` | `arm64-v8a,armeabi-v7a,x86_64` | comma-separated ABIs |
| `ANDROID_HOME` | `$ANDROID_SDK_ROOT` or `~/Android/Sdk` | SDK location |
| `SDL2_SOURCE_DIR` | `port/android/third_party/SDL` | existing SDL2 checkout |
| `GRADLE_BIN` | `port/android/gradlew` | another Gradle 9 binary |
| `OFFLINE=1` | off | build only from the Gradle cache |
| `KEEP_BUILD=1` | off | keep `port/android/build/` for incremental builds |
| `PORT_SOURCE_DIR` | `port/` | build C sources from another checkout |

The APK carries `COPYING.Nuked-OPL3` (LGPL 2.1) and `SDL2-LICENSE.txt` (zlib).

## Install and add the game data

```
adb install -r port/android/out/cybermage-debug.apk
```

On first launch the app asks for the game files. Either:

- **Import folder**: copy the game directory anywhere in shared storage
  (e.g. `Download/CyberMage`) and pick it in the system folder picker. The
  files are copied into the app's own storage; a failed import keeps the old data.
- **adb** (needed on TVs without a folder picker): start the app once so it
  creates its folder, then

```
adb shell am force-stop com.jshield.cybermage
adb push /path/to/CYBERMAG/. /sdcard/Android/data/com.jshield.cybermage/files/data/
```

The launcher checks for `CM_HI.EXE`, `HIPAK.RSC` and `STRINGS.MSG`; copy the
whole directory, not only those.

## Display and controls

Each launch offers GPU widescreen (default), software stretched to fill, or
software 4:3; the choice is remembered. Intent extras skip the dialog:

```
adb shell am start -S -n com.jshield.cybermage/.DataActivity --es renderer sw
adb shell am start -S -n com.jshield.cybermage/.DataActivity --es controls classic
```

- **Touch** (default): left virtual stick, right-half swipe to look (a full-width
  swipe turns 360°), buttons **F** fire, **J** jump, **U** use, **W** weapon,
  **M** menu, **A** hold aim. Menus are tap-to-click.
- **TV remote** (selected automatically on Android TV): D-pad up/down walk,
  left/right turn, centre tap use, centre hold fire, centre double-tap jump,
  Back menu, hold Back next weapon.
- **Gamepad / keyboard / mouse** work everywhere through the modern bindings in
  [`docs/re/controls.md`](../../docs/re/controls.md).
- `--es controls classic` restores the exact DOS keyboard semantics.
