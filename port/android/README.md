# CyberMage Android

The APK contains the C port and SDL2, **no game data**. Supply your own installed
retail CyberMage directory. DOS filenames must retain their uppercase spelling.

## Build

From the repository root:

```
ABIS=arm64-v8a,x86_64 ASP_MEM=4000M tools/run_capped.sh port/android/build.sh
```

Output: `port/android/out/cybermage-debug.apk` (development signing).
Default ABI is `arm64-v8a`; `x86_64` enables the API36 emulator.

This follows `../hexx/port/android`: SDL2 `release-2.30.10` is compiled directly
from `~/qg-android/src/n64/port/android/third_party/SDL`,
never copied into this project. Cached Gradle9.4.1/AGP8.13.2 are reused through
`GRADLE_USER_HOME=~/qg-android/gradle`; the build is strictly
offline. SDK defaults to `~/.local/share/android-sdk`, NDK27.2.12479018,
CMake3.22.1, JDK21. Override `SDL2_SOURCE_DIR`, `GRADLE_USER_HOME`, `GRADLE_BIN`,
`ANDROID_HOME`, or `JAVA_HOME` to equivalent existing installations.

All Gradle/CMake/temp output is under `port/android/build/`. Successful builds
copy the APK to `out/` and delete that build directory. `KEEP_BUILD=1` retains
only this job's outputs for incremental development; failures retain diagnostics.
The unmodified third-party Nuked-OPL3 has the same isolated Clang
`-Wno-static-in-inline` exception as Hexx; port sources retain strict C99 warnings.
The APK carries `COPYING.Nuked-OPL3` (LGPL2.1) and `SDL2-LICENSE.txt` (zlib);
the notice text is read from the existing source trees, not game data.

For an isolated source checkout, `PORT_SOURCE_DIR=/path/to/checkout/port` selects
its C files while keeping Gradle/SDK/SDL inputs and build outputs in this project.
No asset directory is copied or bundled by that option.

## Install and choose the data folder

```
adb install -r port/android/out/cybermage-debug.apk
adb shell am start -n com.jshield.cybermage/.DataActivity
```

On first launch a dialog displays the app-specific external folder and asks you
to put the installed game there. No broad storage permission or bundled data is
needed. Two ways:

- **Import folder** (no adb): copy the retail directory's files anywhere in shared
  storage (e.g. `Download/CyberMage`, via Termux `ssh -p 8022`), choose it in the
  system folder picker; its top-level files are copied into the app folder
  (names keep their case, `.part` then rename). Verified on `box-api36`: 46 files
  imported SHA-256-identical, game launched to the title and scene 1.
- **adb** for the standard emulator/device user0:

```
adb push orig/retail/. /sdcard/Android/data/com.jshield.cybermage/files/data/
```

Select **Retry**, or reopen the app. The launcher checks for CM_HI.EXE, HIPAK.RSC
and STRINGS.MSG; the native loaders validate resource contents and report missing
files. Copy the entire retail directory, not only those three witnesses.
`GameActivity` passes this path as `--data`; all native I/O uses platform SDL_RWops.

The screen is centered at4:3, with black bars rather than stretching640×480.
Tapping inside the game maps to a left mouse click in menu coordinates; the input
struct also carries bounded per-frame touch events with IDs, phases, coordinates,
pressure and an inside-viewport flag. Touches in the black bars do not click.
On Android the modern action layer (touch overlay) is the default, because DOS
keyboard controls are unusable on a touch-only phone. Classic controls (exact DOS
key semantics) remain available with:

```
adb shell am start -S -n com.jshield.cybermage/.DataActivity --es controls classic
```

Modern gameplay has a virtual left stick, a right-side look swipe area and six
outlined buttons: **F** fire, **J** jump, **U** use, **W** weapon cycle, **M** menu,
**A** hold hand aim. The overlay uses the black side bars where they are wide
enough and overlays the lower game corners otherwise. Fingers keep their
original role until lifted, including across menu transitions. Menus retain
tap-to-click. Focus loss releases all inputs. Android Back maps to Escape.
Hardware keyboard/mouse and SDL GameController share the modern action layer.
See `../../docs/re/controls.md` for bindings and configuration.

## Emulator

Reuse `box-api36` from `~/.android/avd` (x86_64). Example:

```
ASP_MEM=4000M tools/run_capped.sh "$ANDROID_HOME/emulator/emulator" \
  -avd box-api36 -port 5580 -no-window -no-snapshot -no-boot-anim \
  -gpu swiftshader_indirect -no-audio
```

The AVD enforces2GiB guest RAM even if a smaller value is requested; the default
2.5GiB process cap is insufficient, so use the4GiB cap. Do not start a second
writer against the same AVD. Save screenshots with `adb exec-out screencap -p`;
retail-derived screenshots stay in ignored `captures/` or `out/`, never Git.

Historical title-only verification on `box-api36` x86_64: first-run data prompt, external folder loading,
2400×1080 landscape title with centered4:3 game area, and a real touchscreen tap
opening Options. Local evidence: `captures/android/first-run.png`,
`title-landscape.png`, `options-touch.png`, `head-logcat.txt`.
That earlier title-only dual-ABI APK was4,373,775 bytes, SHA256
`1e12057cf818ea8d48f28686f931494304a8a12e6057377a8c6517615a8ddb0c`.
Its archive contains no retail asset entries. Source-only title worktree,
emulator process and115MiB Android build outputs were removed after verification;
the shared SDK/SDL/Gradle inputs and resulting APK remain.

## Full-game consolidated smoke

Frozen source commit `c699ea4d97f735458793ae7794506c658b70ab4a` was built with
the canonical script, `ABIS=arm64-v8a,x86_64 KEEP_BUILD=1`, and
`PORT_SOURCE_DIR` pointing to an ignored `git archive HEAD port` snapshot.
SDK/NDK/SDL/Gradle caches above were reused offline. Both ABIs compiled and
linked; APK size3,696,689 bytes, SHA256
`64053b74fcd0f288eb00489423c7863c2696ee2da1afff333776c4463ca53b0f`.
It contains SDL/main libraries for both ABIs and no retail asset entries.

Installed on the existing API36 `box-api36` x86_64 emulator
(emulator36.3.10.0, SwiftShader,2400×1080 landscape), launched through
`DataActivity --es controls modern`. A touchscreen tap on NEW entered the
real scene1. The committed virtual stick and F/J/U/W/M/A overlay appeared in
the side bars. A2200ms upward left-stick swipe visibly moved the player from
the start toward the door. The same app PID remained alive; its142-line log
contained no fatal exception/signal, abort or engine ERROR.

Evidence: `captures/Integrator-android/{title,scene-start,scene-walk}.png`,
`logcat.txt` and `run.json`. All screenshots were opened and inspected.
The emulator was stopped and the job-owned Android build tree plus three
frozen source views were removed (144,219,432 bytes); the3.7MB APK, evidence
and shared SDK/SDL/Gradle caches remain.
