# Wolvic for VIVE XR Elite

> **Unofficial community fork.** This project is not affiliated with or
> supported by HTC or the upstream Wolvic maintainers.

This fork adapts [Wolvic](https://github.com/Igalia/wolvic) for the standalone
VIVE XR Elite using HTC Wave Native SDK 5.6. The current tested build is
**Stage 68** (`versionCode 202710517`, Wolvic `2.0`).

[Download Stage 68 APK](https://github.com/Stavlysa/wolvic_Vive_XR_Elite/releases/download/vive-xr-elite-wave-stage-68/Wolvic-vivexr-stage-68-v202710517.apk)
· [Release notes](https://github.com/Stavlysa/wolvic_Vive_XR_Elite/releases/tag/vive-xr-elite-wave-stage-68)
· [Detailed VIVE build notes](VIVE_XR_ELITE.md)

## What works

- 6DoF headset and controller tracking on VIVE XR Elite
- Wolvic's 2D browser, multiple windows, add-ons and immersive WebXR
- Wave-native stereo submission and asynchronous timewarp when exposed by the
  installed runtime
- sleep/resume recovery while an immersive page is active
- headset power-button recentering and controller Menu-button WebXR exit
- runtime-provided left and right controller models
- VIVE-specific window sizes, UI placement and joystick scrolling fixes
- optional background-window throttling and in-app GitHub update checks
- optional real-world passthrough behind browser windows
- initial controller-free hand pointing, pinch selection and dragging
- Wave-native hand models drawn in front of browser windows and toolbars

The current update adds passthrough controls and initial natural-hand input,
corrects transparent UI compositing and strengthens stereo frame handling.
Distant immersive background flicker remains a known issue. Hand input is still
early support; Gecko does not expose the full WebXR hand-joint API, and custom
gesture mappings are not available yet. See the [current changelog](docs/RELEASE_STAGE68.md).

## Passthrough background

Open **Settings > Environment**:

- **Passthrough background** switches the current background immediately.
- **Start with Passthrough Mode** controls the next launch separately.
- Selecting a virtual environment turns passthrough off for the current session.

The three-dot menu retains its passthrough shortcut. Passthrough is hidden during
opaque immersive VR and restored afterward. This does not add immersive WebXR AR.

## Install

Enable USB debugging on the headset, connect it with a USB cable and run:

```powershell
adb devices
adb install -r Wolvic-vivexr-stage-68-v202710517.apk
```

The APK uses the normal Wolvic application ID, `com.igalia.wolvic`, so
`adb install -r` preserves an existing compatible profile. Release APKs in
this repository are locally debug-signed for upgrade-compatible testing; they
are not production-signed store builds.

## Updates

Open **Settings > Updates** in the VIVE build to:

- enable or disable automatic update checks;
- manually check for a newer GitHub Release;
- choose **Release** or **Release + Prerelease** as the update channel; and
- open this project or a matching Release in Wolvic.

Wolvic never downloads or installs an APK automatically.

## Source package notice

The Stage 68 source is committed to this repository and its Release tag.
`Wolvic-vivexr-stage-68-complete-source-v202710517.zip` also includes the pinned
open-source submodules. GitHub's automatic **Source code (zip/tar.gz)** archives
omit submodule contents. Use the complete-source ZIP, or clone with:

```bash
git clone --recurse-submodules https://github.com/Stavlysa/wolvic_Vive_XR_Elite.git
```

The source package excludes local SDKs, build outputs, signing files,
`local.properties` and caches.

## Build the Wave version

Requirements:

- JDK 17
- Android SDK with API 36
- the NDK and CMake versions requested by the Gradle project
- HTC Wave Native SDK 5.6.0, obtained separately under its own licence

Place the Wave client Maven repository next to the checkout:

```text
parent-directory/
|-- wolvic/
`-- vive-wave-sdk-5.6.0/
    `-- repo/
        `-- com/htc/vr/wvr_client/5.6.0/
            `-- wvr_client-5.6.0.aar
```

Then build the tested release variant on Windows:

```powershell
.\gradlew.bat assembleWavevrArm64GeckoGenericRelease '-PuserProperties.useDebugSigningOnRelease=true' --no-daemon
```

For normal development builds:

```powershell
.\gradlew.bat assembleWavevrArm64GeckoGenericDebug --no-daemon
```

The HTC Wave SDK is licence-gated and is intentionally not committed or
included in the source archive. Do not redistribute HTC SDK files unless your
licence explicitly permits it. See [VIVE_XR_ELITE.md](VIVE_XR_ELITE.md) for
backend details, validation notes and the cumulative Stage history.

## Known issues

- Some distant immersive WebXR background geometry can still flicker while
  nearby menus remain stable.
- Very large controller rotations while dragging a browser window can still
  feel nonlinear.
- This is a device-specific test build and may contain unfinished changes.

## Upstream and licence

Wolvic is developed by [Igalia and the Wolvic community](https://github.com/Igalia/wolvic).
This fork keeps the upstream MPL-2.0 licence; see [LICENSE](LICENSE). Please
report fork-specific problems in this repository and upstream-wide problems to
the upstream Wolvic project.
