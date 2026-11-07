#!/usr/bin/env bash
# Build the CyberMage Android APK (no game data is bundled).
# Needs a JDK (17+) and the Android SDK; the Gradle wrapper fetches Gradle and
# the Android plugin installs the pinned NDK/CMake. SDL2 is cloned on first use.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export ANDROID_HOME="${ANDROID_HOME:-${ANDROID_SDK_ROOT:-$HOME/Android/Sdk}}"
SDL2_TAG=release-2.30.10
SDL2_SOURCE_DIR="${SDL2_SOURCE_DIR:-$HERE/third_party/SDL}"
ABIS="${ABIS:-arm64-v8a,armeabi-v7a,x86_64}"
GRADLE_BIN="${GRADLE_BIN:-$HERE/gradlew}"

[[ -d "$ANDROID_HOME" ]] || { echo "Android SDK not found; set ANDROID_HOME" >&2; exit 1; }
if [[ ! -f "$SDL2_SOURCE_DIR/CMakeLists.txt" ]]; then
    git clone --depth 1 --branch "$SDL2_TAG" https://github.com/libsdl-org/SDL.git "$SDL2_SOURCE_DIR"
fi
FLAGS=(--project-dir "$HERE" --no-daemon "-PportAbis=$ABIS" "-Psdl2SourceDir=$SDL2_SOURCE_DIR")
[[ "${OFFLINE:-0}" == 1 ]] && FLAGS+=(--offline)
[[ -n "${PORT_SOURCE_DIR:-}" ]] && FLAGS+=("-PportSourceDir=$PORT_SOURCE_DIR")
export TMPDIR="$HERE/build/tmp"
mkdir -p "$TMPDIR"
"$GRADLE_BIN" "${FLAGS[@]}" assembleDebug "$@"

apk="$HERE/build/app/outputs/apk/debug/app-debug.apk"
mkdir -p "$HERE/out"
cp "$apk" "$HERE/out/cybermage-debug.apk"
echo "APK: $HERE/out/cybermage-debug.apk ($(wc -c < "$HERE/out/cybermage-debug.apk") bytes)"
[[ "${KEEP_BUILD:-0}" == 1 ]] || rm -rf "$HERE/build"
