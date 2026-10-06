# Android port

An Android build of Banjo: Recompiled, made for the AYN Thor (Snapdragon, Vulkan, two screens).

**You need your own Banjo-Kazooie (USA 1.0) ROM**, both to build the APK and to play. Nothing in this repository or its builds comes from the game.

## Building with GitHub Actions

The APK is built in a **private** repository that holds your ROM, so the ROM never ends up in this public one.

1. Create a private repository, e.g. `banjo-private-inputs`.
2. Put the ROM at the top of it, as `.z64`, `.n64`, `.v64` or a `.zip` containing one.
3. Copy [`android/ci/private-build.yml`](ci/private-build.yml) into it as `.github/workflows/build.yml`.
4. Each push to that repository (or Actions → build-apk → Run workflow) builds the APK from this repository's `main`. Download it from the run's artifacts.

This repository's own `android` workflow only compiles the port without a ROM, to catch build errors.

APKs are signed with the debug key in `android/debug.keystore`, so every build can be installed over the previous one.

## Building on a Linux PC

Install the Android SDK with NDK `28.2.13676358` and CMake `3.22.1`, plus clang, lld, cmake, ninja, cargo, gradle and unzip. Then:

```sh
git clone --recurse-submodules https://github.com/Hydriostatic/BanjoRecomp-Thor.git
cd BanjoRecomp-Thor
ANDROID_HOME=~/Android/Sdk BANJO_ROM=/path/to/banjo.z64 android/build_android.sh
adb install -r android/out/*.apk
```

## First launch

The game starts in its usual launcher. Choose **Select ROM** and pick your ROM in Android's file picker. It's checked and stored inside the app, so you only do this once.

Logs go to logcat under the `BanjoThor` tag:

```sh
adb logcat -s BanjoThor SDL
```

## How the port works

The desktop code is used as-is wherever possible. The Android-only parts are:

| Where | What |
| --- | --- |
| `CMakeLists.txt` | On Android, builds the game as `libmain.so` for SDL's activity, and uses pre-generated patch and game code. |
| `src/main/main.cpp` | Android entry point name, no window icon, larger audio buffer. |
| `src/android/android_bridge.cpp` | `SDL_main`, logcat output, and the JNI calls from the Java side. |
| `android/app/` | Gradle project. `ThorActivity` (on top of SDL's activity) unpacks the UI assets, opens Android's document picker in place of file dialogs, tells the renderer when the screen goes away, and shows the second-screen view. |
| `android/patches/` | Changes to the libraries, applied at build time (see below). |
| `android/build_android.sh` | Builds everything from a clean checkout. |

### Library patches

The libraries stay at the exact versions upstream uses. Instead of forking them, the build applies these patches:

- **`rt64.patch`**:
  - Picks DXC and `file_to_c` for the build machine when cross-compiling.
  - Turns on SDL's Vulkan path for Android and swaps in a no-op file dialog backend.
  - Uses an RGBA swap chain on Android, since Android's swap chains don't offer BGRA.
  - Creates swap chain framebuffers on demand.
- **`rt64-plume.patch`**:
  - Rebuilds the Vulkan surface whenever Android gives the app a new window, for example after leaving and coming back to the app.
  - Stops presenting while there's no window.
  - Falls back to whatever surface format is available.
- **`RecompFrontend.patch`**:
  - Replaces the desktop file dialogs with Android's document picker (used for the ROM and for mods).
  - Finds SDL's headers on Android.
  - Uses the RGBA swap chain format.
  - Turns off UI MSAA, which leaves the screen black on Adreno.

## Not done yet

- Live game stats on the second screen. It currently shows a static title card.
- Pausing the game's audio and timers while the app is in the background.
- Custom GPU drivers (Turnip).
