# VIVE XR Elite support

This fork contains two VIVE XR Elite platform backends:

- `wavevr` is the currently tested backend. It uses HTC Wave SDK 5.6 and is the
  backend used by the Stage 51 through Stage 65 APKs.
- `vivexr` is the earlier experimental backend based on the standard Android
  OpenXR loader. It remains in the tree for comparison and future work.

## Current Stage 65 status

The Wave build has been tested directly on a standalone VIVE XR Elite. It
includes:

- 6DoF head and controller tracking
- Wolvic's 2D browser UI, multiple windows and add-on support
- XR Elite-specific window sizes, density and UI placement
- recenter handling for the headset power-button action
- immersive WebXR entry and native Wave stereo submission
- Wave customized asynchronous timewarp when exposed by the installed runtime
- recovery from headset sleep while an immersive page is active
- left-controller Menu button exit from immersive WebXR
- runtime-provided left and right controller models
- Wave component `localMat` transforms for controller-model alignment
- optional real-world browser background with controls in Environment

The current APK is Wolvic 2.0, versionCode 202582032. The latest changes add
passthrough lifecycle handling, correct UI alpha coverage, per-eye texture
regions, guarded Wave texture indices and shader cleanup. Frame completion is
published with the next fresh pose after surface release.

See [current release notes](docs/RELEASE_STAGE65.md),
[frame safeguards](docs/VIVE_STAGE63.md), [UI compositing](docs/VIVE_STAGE64.md)
and [Environment settings](docs/VIVE_STAGE65.md).

Stage 52 additionally uses an optimized release build, keeps Wave debug logging
opt-in, and avoids installing the debug session-store observer unless verbose
logging is enabled. A 4K60 YouTube stream was confirmed to use Qualcomm's
hardware VP9 decoder and maintained 89-90 compositor FPS during testing.

Stage 53 switches remote debugging off once when an existing Wave profile first
starts the release build. Users can turn it back on explicitly afterward. It
also adds **Reduce Background Window Frame Rate** to Developer Options. With
the option enabled, only the focused visible Gecko session remains active;
other visible windows keep their last frame and resume immediately when
focused. The option is enabled by default on Wave and applies without an app
restart. This behavior does not alter immersive WebXR or compositor timing.

In a three-window device test, an unfocused tab sampled at 0 percent CPU while
the compositor remained at 89-90 FPS. The Firefox remote-debugger socket was
also absent. These measurements verify the Stage 53 background-window behavior
but are not a confirmed 4K60 playback result.

Stage 54 fixes parked background pages becoming plain white. Wolvic now
disconnects Gecko from the window surface before deactivating the background
session, allowing Wave to retain the last submitted page frame. Focusing the
window reactivates the session and reconnects the same surface. No extra bitmap
is allocated, and immersive WebXR rendering is unaffected.

Stage 55 also suppresses the one-frame white initialization flash that could
appear while reconnecting any parked web window. Wolvic keeps the toolbar
visible but withholds the browser content layer until the first restarted
composite has settled. A short fallback always restores the layer if the
callback is unavailable. YouTube was used only as a reproducible test case;
the behavior is implemented in the shared window lifecycle and is not
site-specific.

Stage 56 fixes joystick scroll delivery for pages and side windows that did
not receive the event reliably. Scrolling over an inactive side window first
focuses and resumes its Gecko session, and Wolvic now keeps Gecko's document
focus synchronized with the active browser window. The Wave controller axes
are initialized to zero before input is sampled. Scroll speed and direction
are unchanged, and there are no site-specific branches.

Stage 57 adds a VIVE-only **Updates** settings page backed by the public GitHub
Releases API for `Stavlysa/wolvic_Vive_XR_Elite`. Users can disable automatic
checks, run a manual check at any time and toggle the channel between
**Release** and **Release + Prerelease**. Checks compare the APK asset's Android
`versionCode` against the installed build, never download or install an APK,
and open the selected Release page in Wolvic. Automatic checks are limited to
once per 24 hours and do not display a prompt during immersive WebXR.

Stage 58 adds an always-visible GitHub project link to the Updates page. This
opens the fork's source and Releases page independently of whether a newer APK
has been detected.

Stage 59 sends joystick wheel events as genuine generic mouse input, including
`SOURCE_MOUSE` and a mouse pointer tool type. This improves compatibility with
sites whose custom scroll handlers ignore touchscreen-shaped generic events.
The change is shared and does not contain site-specific rules.

Stage 60 makes the add-ons and add-on-permissions lists take focus when an
incoming generic joystick event reaches them. This prevents a surrounding
settings view or previously focused browser page from swallowing the wheel
event.

Stage 61 removes the RecyclerView scroll listener that repeatedly requested
focus while a list was already moving. Focus is now established only at the
input boundary, eliminating the brief upward correction and the later
stuttering introduced by the first add-ons-specific workaround.

Stage 62 optimizes the Wave immersive copy pass. Gecko already supplies a
finished stereo texture, so the Wave eye FBOs no longer allocate redundant
multisampling or depth attachments in immersive mode. The normal browser UI
continues to use a depth buffer and 4x MSAA. Texture queues are rebuilt when
the render mode changes so each mode receives the correct FBO attributes.

In an approximately 80-second Moon Rider sample on the XR Elite, Stage 62
averaged 87.91 fresh application frames per second against an 89.83 Hz Wave
compositor. The sample reported no skipped or discontinuous frames and no
crash; one short drop to about 76 FPS recovered without intervention. Casting
does not reproduce the headset's compositor output reliably in immersive mode,
so visual quality must still be checked inside the headset.

Known issue: some distant WebXR background geometry can still flicker while
nearby menus remain stable. A fix for that visual issue is not established.

## External HTC dependency

HTC Wave SDK is license-gated and is intentionally not committed to this
repository or included in the source archive. Obtain Wave Native SDK 5.6.0
from HTC and place its local Maven repository next to the Wolvic checkout using
this layout:

```text
parent-directory/
|-- wolvic/
`-- vive-wave-sdk-5.6.0/
    `-- repo/
        `-- com/htc/vr/wvr_client/5.6.0/
            `-- wvr_client-5.6.0.aar
```

The Gradle configuration extracts the public native headers and the arm64
linker library from that AAR only for Wave build tasks. Do not publish HTC SDK
files unless your HTC license explicitly permits it.

## Build the tested Wave APK

Requirements:

- JDK 17
- Android SDK with API 36 installed
- the Android NDK and CMake versions requested by the Gradle project
- HTC Wave Native SDK 5.6.0 in the external location above

From the repository root on Windows PowerShell:

```powershell
.\gradlew.bat assembleWavevrArm64GeckoGenericDebug --no-daemon
```

To build the locally signed Wave release on PowerShell:

```powershell
.\gradlew.bat assembleWavevrArm64GeckoGenericRelease '-PuserProperties.useDebugSigningOnRelease=true' --no-daemon
```

The local release build uses R8 and resource shrinking. Its debug certificate
is for upgrade-compatible testing only and must not be used for a production
release.

On Linux or macOS:

```bash
./gradlew assembleWavevrArm64GeckoGenericDebug --no-daemon
```

The APK is written to:

```text
app/build/outputs/apk/wavevrArm64GeckoGeneric/debug/
Wolvic-wavevr-arm64-gecko-generic-debug.apk
```

The build uses the normal Wolvic application ID, `com.igalia.wolvic`, so an
`adb install -r` update preserves the existing browser profile.

## Install and launch

Enable USB debugging on the headset, accept the computer's debugging key and
verify the connection:

```powershell
adb devices
```

Install the APK:

```powershell
adb install -r app/build/outputs/apk/wavevrArm64GeckoGeneric/debug/Wolvic-wavevr-arm64-gecko-generic-debug.apk
```

Launch it from the XR Elite VR application list, or directly over ADB:

```powershell
adb shell am start -n com.igalia.wolvic/.VRBrowserActivity
```

## Experimental OpenXR build

The alternative `vivexr` backend does not require Wave SDK:

```powershell
.\gradlew.bat assembleVivexrArm64GeckoGenericDebug --no-daemon
```

It uses the standard Khronos Android OpenXR loader and the Focus 3 / XR Elite
controller interaction profile. It is not the backend used to produce the
current APK and has not received all of the Wave-specific fixes listed above.

## GitHub release packaging

Keep the source archive and APK as separate GitHub assets:

- upload the source tree to the repository without local SDKs, build outputs,
  signing files or `local.properties`;
- attach the current APK and the explicitly named complete-source ZIP to a
  GitHub Release;
- state clearly that this is an unofficial device adaptation and that the HTC
  Wave SDK must be downloaded separately.

The VIVE changes are committed to the public branch and Release tag. GitHub's
automatic source archives omit submodule contents. The separate complete-source
ZIP includes the pinned open-source submodules. The external HTC SDK remains
excluded; SHA256SUMS.txt identifies the uploaded APK and complete source ZIP.
