#!/usr/bin/env bash
# Builds the RetroAchievements mod and the RA Bridge app. Needs no ROM.
#
#   mods/retroachievements/build.sh
#
# Output in mods/retroachievements/out/:
#   bk_retroachievements.zip   the mod: install it from the game's mod menu (it holds the .nrm and .so)
#   RABridge.apk               the companion app (only when BUILD_BRIDGE=1)
#
# Environment:
#   ANDROID_HOME     Android SDK with NDK 28.2.13676358. Without it, the native library is built
#                    for this computer instead (useful for testing on desktop).
#   BUILD_BRIDGE=1   also build RA Bridge (needs ANDROID_HOME and gradle)
#
# Host tools: clang, ld.lld, cmake, ninja, make, zip, git.

set -euo pipefail

MOD_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd "$MOD_DIR/../.." && pwd)"
WORK="${RA_WORK_DIR:-$MOD_DIR/.work}"
OUT="$MOD_DIR/out"
JOBS="${JOBS:-$(nproc)}"
NDK_VERSION="28.2.13676358"

step() { echo; echo "==> $*"; }

for required in "$ROOT/BanjoRecompSyms/bk.us.rev0.syms.toml" "$ROOT/lib/N64ModernRuntime/N64Recomp/CMakeLists.txt"; do
    if [[ ! -f "$required" ]]; then
        echo "Missing $required. Run: git submodule update --init --recursive BanjoRecompSyms lib/N64ModernRuntime" >&2
        exit 1
    fi
done

mkdir -p "$WORK" "$OUT"

# ---------------------------------------------------------------------------------------------
step "Building RecompModTool"

MODTOOL="$WORK/n64recomp/RecompModTool"
if [[ ! -x "$MODTOOL" ]]; then
    cmake -S "$ROOT/lib/N64ModernRuntime/N64Recomp" -B "$WORK/n64recomp" -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build "$WORK/n64recomp" --target RecompModTool -j "$JOBS"
fi

# ---------------------------------------------------------------------------------------------
step "Building the mod's game code"

make -C "$MOD_DIR" -j "$JOBS" MOD_CC="${MOD_CC:-clang}" MOD_LD="${MOD_LD:-ld.lld}"
rm -rf "$MOD_DIR/build/nrm"
mkdir -p "$MOD_DIR/build/nrm"
"$MODTOOL" "$MOD_DIR/mod.toml" "$MOD_DIR/build/nrm"

# ---------------------------------------------------------------------------------------------
step "Building the native library"

if [[ -n "${ANDROID_HOME:-}" ]]; then
    NDK="${RA_NDK:-$ANDROID_HOME/ndk/$NDK_VERSION}"
    NATIVE_BUILD="$WORK/native-android"
    cmake -S "$MOD_DIR/native" -B "$NATIVE_BUILD" -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
        -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 -DANDROID_STL=c++_static \
        -DCMAKE_BUILD_TYPE=Release
    LIB_NAME="bk_retroachievements.so"
else
    echo "ANDROID_HOME isn't set, so the native library is built for this computer."
    NATIVE_BUILD="$WORK/native-host"
    cmake -S "$MOD_DIR/native" -B "$NATIVE_BUILD" -G Ninja -DCMAKE_BUILD_TYPE=Release
    case "$(uname -s)" in
        Darwin) LIB_NAME="bk_retroachievements.dylib" ;;
        MINGW*|MSYS*|CYGWIN*) LIB_NAME="bk_retroachievements.dll" ;;
        *) LIB_NAME="bk_retroachievements.so" ;;
    esac
fi
cmake --build "$NATIVE_BUILD" -j "$JOBS"

# ---------------------------------------------------------------------------------------------
step "Packaging"

STAGE="$WORK/stage"
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp "$MOD_DIR/build/nrm/bk_retroachievements.nrm" "$NATIVE_BUILD/$LIB_NAME" "$STAGE/"
rm -f "$OUT/bk_retroachievements.zip"
(cd "$STAGE" && zip -q -9 "$OUT/bk_retroachievements.zip" bk_retroachievements.nrm "$LIB_NAME")
unzip -l "$OUT/bk_retroachievements.zip"

# ---------------------------------------------------------------------------------------------
if [[ "${BUILD_BRIDGE:-0}" == "1" ]]; then
    step "Building RA Bridge"
    : "${ANDROID_HOME:?BUILD_BRIDGE=1 needs ANDROID_HOME}"
    gradle --no-daemon -p "$MOD_DIR/bridge-app" :app:assembleRelease
    cp "$MOD_DIR/bridge-app/app/build/outputs/apk/release/app-release.apk" "$OUT/RABridge.apk"
fi

echo
echo "Done. Output in $OUT:"
ls -la "$OUT"
