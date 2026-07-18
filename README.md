
# Read me first

This is a wolvic for vive xr elite.
I made this by using codex.
the following content is created by codex.
Acording to me, this is usable.
It is better that vive browser s**t.
It cost me about 100AUD to buy credits.
Please help.

The rest of the readme is written by codex so if you are a programmer, you can read it and figure out how to fix it or you can submit some issue about this browser.
Thanks for your watching.












# Wolvic for VIVE XR Elite — Stage 11

This package contains the latest APK that was successfully built, installed, and tested on a VIVE XR Elite, together with the exact corresponding source code.

## Version Information

- Stage: 11
- Android versionCode: `202000921`
- Wolvic versionName: `1.9`
- Branch: `codex/vive-xr-elite`
- Upstream base commit: `4c1c848932f919c647176c02b2f1ac31825915f6`
- Target: VIVE XR, arm64, Gecko Generic Debug

## Package Contents

- `Wolvic-vivexr-stage-11-v202000921.apk` — APK successfully installed on a VIVE XR Elite.
- `Wolvic-VIVE-XR-Elite-stage-11-source.zip` — Complete source tree, including the checked-out vrb, tinygltf, and KTX-Software submodule sources.
- `SHA256SUMS.txt` — SHA-256 checksums for integrity verification.

The source archive excludes `.git`, Gradle/Kotlin/CMake build caches, generated `build` directories, and the machine-specific `local.properties` file. After extracting the source, create `local.properties` and configure the Android SDK path before building.

Example:

```properties
sdk.dir=C:/path/to/Android/sdk
```

## Build Instructions

Use JDK 17. From the extracted `wolvic` directory, run:

```powershell
.\gradlew.bat assembleVivexrArm64GeckoGenericDebug --no-daemon
```

The generated APK will be located at:

```text
app/build/outputs/apk/vivexrArm64GeckoGeneric/debug/Wolvic-vivexr-arm64-gecko-generic-debug.apk
```

## Stage 11 Status

- VIVE XR Elite OpenXR integration and Focus 3 controller mapping.
- Gecko 140 External VR ABI v19 compatibility.
- Functional immersive VR session entry.
- WebXR eye resolution set to 1280 × 1280 per eye.
- OpenXR configured for 90 Hz with HTC Prompt frame synchronization.
- When an OpenXR display interval is missed, the submission time is corrected to the runtime's current target time so asynchronous reprojection can handle the missed frame.
- Out-of-Process WebGL should remain disabled in this build.

## Known Issues

- Noticeable head-rotation latency remains in immersive VR.
- Distant scenery may flicker in immersive VR.
- These issues are intended to be addressed in later stages and no unfinished Stage 12 changes are included in this package.
