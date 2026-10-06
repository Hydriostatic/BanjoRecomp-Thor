#!/usr/bin/env bash
# Builds the Android APK from a clean checkout. This is what the GitHub workflow runs, and it
# works the same on a Linux PC with the Android SDK installed.
#
# Needs:
#   ANDROID_HOME      Android SDK, with NDK 28.2.13676358 and CMake 3.22.1 installed
#   BANJO_ROM         your Banjo-Kazooie (USA 1.0) ROM, .z64/.n64/.v64 or a .zip holding one
#
# With BANJO_CHECK_ONLY=1 no ROM is needed: it compiles the libraries and the Android-specific
# sources for Android and the Java code, without generating or linking the game. That's a quick
# way to see whether the port still compiles.
#   host tools        clang, ld.lld, cmake, ninja, make, cargo, python3, gradle, unzip
#
# Steps:
#   1. apply the Android patches to RT64, plume and RecompFrontend
#   2. build the host tools (N64Recomp, RSPRecomp, file_to_c, bk_rom_decompress)
#   3. generate the game code from the ROM (same as the desktop build)
#   4. build SDL2 and FreeType for Android
#   5. build the APK with Gradle
#
# The APK ends up in android/out/. Nothing derived from the ROM is ever committed: the generated
# code folders and the decompressed ROM are in .gitignore.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="${BANJO_WORK_DIR:-$ROOT/android/.work}"
HOST_DIR="$WORK/host"
DEPS_DIR="$WORK/deps"
JOBS="${JOBS:-$(nproc)}"

NDK_VERSION="28.2.13676358"
ANDROID_PLATFORM="29"
SDL2_VERSION="${SDL2_VERSION:-2.32.10}"
FREETYPE_TAG="${FREETYPE_TAG:-VER-2-13-3}"
BK_ROM_COMPRESSOR_COMMIT="${BK_ROM_COMPRESSOR_COMMIT:-master}"
ROM_SHA1="1fe1632098865f639e22c11b9a81ee8f29c75d7a"

: "${ANDROID_HOME:?Set ANDROID_HOME to your Android SDK}"
CHECK_ONLY="${BANJO_CHECK_ONLY:-0}"
if [[ "$CHECK_ONLY" != "1" ]]; then
    : "${BANJO_ROM:?Set BANJO_ROM to your Banjo-Kazooie (USA 1.0) ROM}"
fi
# Always the pinned NDK: GitHub's runners set ANDROID_NDK_HOME to whatever version they ship.
ANDROID_NDK="${BANJO_NDK:-$ANDROID_HOME/ndk/$NDK_VERSION}"
TOOLCHAIN_FILE="$ANDROID_NDK/build/cmake/android.toolchain.cmake"

step() { echo; echo "==> $*"; }

mkdir -p "$HOST_DIR" "$DEPS_DIR"
cd "$ROOT"

# ---------------------------------------------------------------------------------------------
step "Applying Android patches"

apply_patch() {
    local dir="$1" patch="$ROOT/android/patches/$2"
    if git -C "$dir" apply --reverse --check "$patch" 2>/dev/null; then
        echo "$2 is already applied"
    else
        git -C "$dir" apply --whitespace=nowarn "$patch"
        echo "applied $2"
    fi
}

apply_patch lib/rt64 rt64.patch
apply_patch lib/rt64/src/contrib/plume rt64-plume.patch
apply_patch lib/RecompFrontend RecompFrontend.patch

# ---------------------------------------------------------------------------------------------
step "Building host tools"

if [[ ! -x "$HOST_DIR/file_to_c" ]]; then
    c++ -std=c++17 -O2 lib/rt64/src/tools/file_to_c/file_to_c.cpp -o "$HOST_DIR/file_to_c"
fi

if [[ ! -x "$HOST_DIR/N64Recomp" || ! -x "$HOST_DIR/RSPRecomp" ]]; then
    cmake -S lib/N64ModernRuntime/N64Recomp -B "$HOST_DIR/n64recomp-build" -G Ninja -DCMAKE_BUILD_TYPE=Release
    cmake --build "$HOST_DIR/n64recomp-build" --target N64RecompCLI RSPRecomp -j "$JOBS"
    cp "$HOST_DIR/n64recomp-build/N64Recomp" "$HOST_DIR/n64recomp-build/RSPRecomp" "$HOST_DIR/"
fi

if [[ "$CHECK_ONLY" != "1" && ! -x "$HOST_DIR/bk_rom_decompress" ]]; then
    rm -rf "$HOST_DIR/bk_rom_compressor"
    git clone --quiet https://github.com/MittenzHugg/bk_rom_compressor.git "$HOST_DIR/bk_rom_compressor"
    git -C "$HOST_DIR/bk_rom_compressor" checkout --quiet "$BK_ROM_COMPRESSOR_COMMIT"
    (cd "$HOST_DIR/bk_rom_compressor" && cargo build --release --bin bk_rom_decompress)
    cp "$HOST_DIR/bk_rom_compressor/target/release/bk_rom_decompress" "$HOST_DIR/"
fi

# ---------------------------------------------------------------------------------------------
if [[ "$CHECK_ONLY" != "1" ]]; then
step "Generating the game code from the ROM"

ROM_INPUT="$BANJO_ROM"
if [[ "$ROM_INPUT" == *.zip ]]; then
    rm -rf "$WORK/rom"
    mkdir -p "$WORK/rom"
    unzip -q -o "$ROM_INPUT" -d "$WORK/rom"
    ROM_INPUT="$(find "$WORK/rom" -type f \( -iname '*.z64' -o -iname '*.n64' -o -iname '*.v64' \) | head -n 1)"
    [[ -n "$ROM_INPUT" ]] || { echo "No .z64/.n64/.v64 file inside $BANJO_ROM" >&2; exit 1; }
fi

# bk_rom_decompress accepts any byte order and refuses ROMs it doesn't know, but the recompiled
# code only matches USA 1.0, so check for exactly that one.
python3 - "$ROM_INPUT" "$ROM_SHA1" <<'PY'
import hashlib, sys
data = bytearray(open(sys.argv[1], 'rb').read())
magic = bytes(data[:4])
if magic == b'\x37\x80\x40\x12':      # .v64, byte-swapped
    data[0::2], data[1::2] = data[1::2], data[0::2]
elif magic == b'\x40\x12\x37\x80':    # .n64, little-endian
    for i in range(0, len(data), 4):
        data[i:i+4] = data[i:i+4][::-1]
digest = hashlib.sha1(data).hexdigest()
if digest != sys.argv[2]:
    sys.exit(f"This ROM isn't Banjo-Kazooie (USA 1.0): sha1 {digest}, expected {sys.argv[2]}")
print("ROM is Banjo-Kazooie (USA 1.0)")
PY

"$HOST_DIR/bk_rom_decompress" "$ROM_INPUT" banjo.us.v10.decompressed.z64

make -C patches CC="${PATCHES_CC:-clang}" LD="${PATCHES_LD:-ld.lld}" -j "$JOBS"
"$HOST_DIR/N64Recomp" patches.toml
"$HOST_DIR/file_to_c" patches/patches.bin bk_patches_bin RecompiledPatches/patches_bin.c RecompiledPatches/patches_bin.h
"$HOST_DIR/N64Recomp" banjo.us.rev0.toml
"$HOST_DIR/RSPRecomp" n_aspMain.us.rev0.toml
fi

# ---------------------------------------------------------------------------------------------
step "Building SDL2 and FreeType for Android"

ndk_cmake() {
    cmake -G Ninja \
        -DCMAKE_TOOLCHAIN_FILE="$TOOLCHAIN_FILE" \
        -DANDROID_ABI=arm64-v8a \
        -DANDROID_PLATFORM="android-$ANDROID_PLATFORM" \
        -DCMAKE_BUILD_TYPE=Release \
        "$@"
}

SDL2_SOURCE="$DEPS_DIR/SDL2-$SDL2_VERSION"
SDL2_PREFIX="$DEPS_DIR/sdl2-android"
if [[ ! -f "$SDL2_PREFIX/lib/libSDL2.so" ]]; then
    curl -fsSL "https://github.com/libsdl-org/SDL/releases/download/release-$SDL2_VERSION/SDL2-$SDL2_VERSION.tar.gz" | tar -xz -C "$DEPS_DIR"
    ndk_cmake -S "$SDL2_SOURCE" -B "$DEPS_DIR/sdl2-build" \
        -DCMAKE_INSTALL_PREFIX="$SDL2_PREFIX" -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF
    cmake --build "$DEPS_DIR/sdl2-build" -j "$JOBS"
    cmake --install "$DEPS_DIR/sdl2-build"
fi

FREETYPE_PREFIX="$DEPS_DIR/freetype-android"
if [[ ! -f "$FREETYPE_PREFIX/lib/libfreetype.a" ]]; then
    rm -rf "$DEPS_DIR/freetype-src"
    mkdir -p "$DEPS_DIR/freetype-src"
    curl -fsSL "https://github.com/freetype/freetype/archive/refs/tags/$FREETYPE_TAG.tar.gz" | tar -xz -C "$DEPS_DIR/freetype-src" --strip-components=1
    ndk_cmake -S "$DEPS_DIR/freetype-src" -B "$DEPS_DIR/freetype-build" \
        -DCMAKE_INSTALL_PREFIX="$FREETYPE_PREFIX" -DBUILD_SHARED_LIBS=OFF \
        -DFT_DISABLE_ZLIB=TRUE -DFT_DISABLE_BZIP2=TRUE -DFT_DISABLE_PNG=TRUE \
        -DFT_DISABLE_HARFBUZZ=TRUE -DFT_DISABLE_BROTLI=TRUE
    cmake --build "$DEPS_DIR/freetype-build" -j "$JOBS"
    cmake --install "$DEPS_DIR/freetype-build"
fi

export BANJO_SDL2_PREFIX="$SDL2_PREFIX"
export BANJO_SDL2_SOURCE="$SDL2_SOURCE"
export BANJO_FREETYPE_PREFIX="$FREETYPE_PREFIX"
export BANJO_HOST_FILE_TO_C="$HOST_DIR/file_to_c"

# ---------------------------------------------------------------------------------------------
if [[ "$CHECK_ONLY" == "1" ]]; then
    step "Compile check (no ROM)"

    # The generated sources don't exist without a ROM. Empty stand-ins let CMake configure;
    # nothing that needs their contents gets built.
    CHECK_STUBS=()
    for stub in RecompiledFuncs/check_stub.c RecompiledPatches/patches.c RecompiledPatches/patches_bin.c rsp/n_aspMain.cpp; do
        if [[ ! -e "$stub" ]]; then
            mkdir -p "$(dirname "$stub")"
            : > "$stub"
            CHECK_STUBS+=("$stub")
        fi
    done
    trap 'rm -f "${CHECK_STUBS[@]}"' EXIT

    CHECK_BUILD="$WORK/check-build"
    ndk_cmake -S "$ROOT" -B "$CHECK_BUILD" \
        -DANDROID_STL=c++_static \
        -DSDL2_DIR="$SDL2_PREFIX/lib/cmake/SDL2" \
        -DCMAKE_PREFIX_PATH="$SDL2_PREFIX;$FREETYPE_PREFIX" \
        -DCMAKE_FIND_ROOT_PATH="$SDL2_PREFIX;$FREETYPE_PREFIX" \
        -DFREETYPE_LIBRARY="$FREETYPE_PREFIX/lib/libfreetype.a" \
        -DFREETYPE_INCLUDE_DIRS="$FREETYPE_PREFIX/include/freetype2" \
        -DFREETYPE_INCLUDE_DIR_ft2build="$FREETYPE_PREFIX/include/freetype2" \
        -DFREETYPE_INCLUDE_DIR_freetype2="$FREETYPE_PREFIX/include/freetype2" \
        -DRT64_HOST_FILE_TO_C="$HOST_DIR/file_to_c" \
        -DZSTD_BUILD_PROGRAMS=OFF -DZSTD_BUILD_TESTS=OFF

    # Everything except the game code itself and the final link.
    cmake --build "$CHECK_BUILD" -j "$JOBS" --target rt64 librecomp ultramodern recompui recompinput
    (cd "$CHECK_BUILD" && ninja -j "$JOBS" \
        CMakeFiles/BanjoRecompiled.dir/src/main/main.cpp.o \
        CMakeFiles/BanjoRecompiled.dir/src/android/android_bridge.cpp.o \
        CMakeFiles/BanjoRecompiled.dir/src/game/config.cpp.o)

    gradle --no-daemon -p android :app:compileReleaseJavaWithJavac
    echo "Compile check passed."
    exit 0
fi

# ---------------------------------------------------------------------------------------------
step "Building the APK"

GRADLE_TASK="${GRADLE_TASK:-assembleRelease}"
gradle --no-daemon -p android ":app:$GRADLE_TASK"

mkdir -p android/out
find android/app/build/outputs/apk -name '*.apk' -exec cp {} android/out/ \;
ls -la android/out
