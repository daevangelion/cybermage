#!/usr/bin/env bash
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export ANDROID_HOME="${ANDROID_HOME:-$HOME/.local/share/android-sdk}"
export JAVA_HOME="${JAVA_HOME:-/usr/lib/jvm/java-21-openjdk}"
export GRADLE_USER_HOME="${GRADLE_USER_HOME:-~/qg-android/gradle}"
SDL2_SOURCE_DIR="${SDL2_SOURCE_DIR:-~/qg-android/src/n64/port/android/third_party/SDL}"
ABIS="${ABIS:-arm64-v8a}"

if [[ ! -f "$SDL2_SOURCE_DIR/CMakeLists.txt" ]] ||
   [[ "$(git -C "$SDL2_SOURCE_DIR" describe --tags 2>/dev/null)" != release-2.30.10 ]]; then
    echo 'SDL2_SOURCE_DIR must point to the existing release-2.30.10 checkout' >&2
    exit 1
fi
if [[ ! -d "$GRADLE_USER_HOME/caches/modules-2/files-2.1/com.android.tools.build/gradle/8.13.2" ]]; then
    echo "Android Gradle plugin 8.13.2 is not cached in $GRADLE_USER_HOME" >&2
    exit 1
fi
if [[ -z "${GRADLE_BIN:-}" ]]; then
    shopt -s nullglob
    candidates=("$GRADLE_USER_HOME"/wrapper/dists/gradle-9.4.1-bin/*/gradle-9.4.1/bin/gradle)
    if [[ ${#candidates[@]} != 1 ]]; then
        echo 'Set GRADLE_BIN to the cached Gradle 9.4.1 executable' >&2
        exit 1
    fi
    GRADLE_BIN="${candidates[0]}"
fi
for required in "$GRADLE_BIN" "$JAVA_HOME/bin/java" "$ANDROID_HOME/ndk/27.2.12479018/build/cmake/android.toolchain.cmake"; do
    [[ -f "$required" ]] || { echo "Missing prerequisite: $required" >&2; exit 1; }
done
export TMPDIR="$HERE/build/tmp"
mkdir -p "$TMPDIR"
SOURCE_FLAGS=()
if [[ -n "${PORT_SOURCE_DIR:-}" ]]; then
    SOURCE_FLAGS+=("-PportSourceDir=$PORT_SOURCE_DIR")
fi
"$GRADLE_BIN" --project-dir "$HERE" --project-cache-dir "$HERE/build/gradle-project" \
    --no-daemon --offline --build-cache --max-workers=2 \
    "-PportAbis=$ABIS" "-Psdl2SourceDir=$SDL2_SOURCE_DIR" "${SOURCE_FLAGS[@]}" assembleDebug "$@"
apk="$HERE/build/app/outputs/apk/debug/app-debug.apk"
if [[ -f "$apk" ]]; then
    mkdir -p "$HERE/out"
    cp "$apk" "$HERE/out/cybermage-debug.apk"
    echo "APK: $HERE/out/cybermage-debug.apk ($(stat -c %s "$HERE/out/cybermage-debug.apk") bytes)"
    if [[ "${KEEP_BUILD:-0}" != 1 ]]; then
        rm -rf "$HERE/build"
    fi
fi
