# RetroAchievements mod

Earn [RetroAchievements](https://retroachievements.org) in Banjo: Recompiled on Android (AYN Thor). It's a regular mod: nothing in the game or the port is changed.

It works with this port (`io.github.hydriostatic.banjothor`) and with [AurelioB's BanjoRecomp-Android](https://github.com/AurelioB/BanjoRecomp-Android) (`com.aure.banjorecomp`).

## What you get

- Login with your RetroAchievements account (the password is only needed once).
- The game is identified from your ROM, and the official Banjo-Kazooie achievement set is loaded.
- Unlocks, leaderboard attempts and connection problems pop up as toasts in the top-right corner.
- Softcore achievements. Hardcore unlocks aren't accepted yet (see below).

## Why there's a second app

Neither Banjo app asks Android for internet access, and a mod can't change that: code loaded by a mod runs inside the game's process, under the game's permissions. **RA Bridge** is a tiny companion app that does have internet access. When it starts the game, it hands the game a private connection to itself; the mod sends its requests through that connection and RA Bridge relays them. It only forwards HTTPS requests to retroachievements.org, and only from the Banjo apps.

If a Banjo app ever gets internet access itself, the mod uses it directly and RA Bridge isn't needed.

## Install

You need two files from the [retroachievements-mod workflow](../../.github/workflows/retroachievements-mod.yml) run's artifacts (Actions → retroachievements-mod → latest run): `bk_retroachievements-mod` and `RABridge-apk`. GitHub zips each artifact, so unzip them once.

1. Install `RABridge.apk`.
2. Open the game as usual, go to **Mods**, install `bk_retroachievements.zip` (keep it zipped: it holds the mod and its native library), and enable it.
3. In the mod's settings, enter your RetroAchievements **username** and **password**.
4. Close the game completely.
5. Open **RA Bridge** and tap **Play**. Allow its notification if asked; it keeps the bridge running while you play.

From then on, always start Banjo from RA Bridge. If you start it from its own icon, the mod shows a toast saying it can't connect. After the first successful login you can clear the password field: a login token is saved in the mods folder.

## Hardcore mode

RetroAchievements only accepts hardcore unlocks from approved clients, and a client has to be public for at least 6 months before it can apply. Until then, leave **Hardcore mode** off and earn softcore achievements.

## How it works

| Part | What it does |
| --- | --- |
| `src/main.c` | Game-side code, recompiled by the mod loader. Hooks a function the game calls every frame, starts the native library with the mod's settings, ticks it, and draws the toasts with the recomp UI API. |
| `native/src/ra_mod.cpp` | Native library (`bk_retroachievements.so`, loaded next to the `.nrm`). Runs [rcheevos](https://github.com/RetroAchievements/rcheevos): login, game identification, evaluating achievements against the game's memory, unlocks. |
| `native/src/http_android.cpp` | HTTP over JNI: through RA Bridge's Binder when the game was launched by it, otherwise in-process if the app has internet access. |
| `bridge-app/` | RA Bridge. Launches the game with a Binder in the launch intent and relays the requests. |

The recomp runtime keeps the N64's RDRAM in the same layout emulators do (32-bit words in host byte order), which is what RetroAchievements' N64 achievement definitions are written against, so they read the right values without conversion.

## Building

```sh
git submodule update --init --depth 1 BanjoRecompSyms lib/N64ModernRuntime
git -C lib/N64ModernRuntime submodule update --init --depth 1 N64Recomp
git -C lib/N64ModernRuntime/N64Recomp submodule update --init --recursive --depth 1
ANDROID_HOME=~/Android/Sdk BUILD_BRIDGE=1 mods/retroachievements/build.sh
```

Output goes to `mods/retroachievements/out/`. Without `ANDROID_HOME` the native library is built for your computer instead. No ROM is needed.

Logs: `adb logcat -s BanjoRA RABridge`.
