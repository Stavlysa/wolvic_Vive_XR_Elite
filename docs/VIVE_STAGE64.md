# Stage 64 — Passthrough UI compositing

Follow-up to Stage 63 after a report of abnormal web-page or toolbar rendering
with passthrough enabled. The exact through-the-lens symptom still needs user
confirmation; these changes fix independently reproducible alpha-compositing
errors and are not yet a claim that the reported visual issue is resolved.

## Changes

- Wave eye rendering now uses separate RGB and alpha blend factors. Coverage is
  accumulated as `srcAlpha + dstAlpha * (1 - srcAlpha)`, not
  `srcAlpha * srcAlpha + dstAlpha * (1 - srcAlpha)`. For example, a 50% translucent
  toolbar drawn over an opaque page now leaves the output alpha at 1, not 0.75.
- Wave widget surfaces convert Android's premultiplied texture colors to the
  straight colors expected by the scene blend function. This avoids applying
  alpha twice to text edges, rounded corners, and translucent UI colors.
- Both flat and curved widgets use the same correction. Loading-page fallback
  colors retain their existing solid-color shader. Transparent regions remain
  transparent; opaque black content remains opaque.
- No pose, recentering, window-placement, WebXR frame timing, resolution, or
  passthrough lifecycle changes in this stage. No extra scene render pass.

## Verification

`tools/tests/run-wave-rendering-tests.ps1` now also compiles the production
external-OES widget shader and runs offscreen EGL/GLES tests on the connected
headset GPU. These check transparent background/corners, partial coverage,
overlapping translucent widgets, an opaque page, a translucent toolbar over an
opaque page, widget fading, and opaque black content.

The GPU pixel tests and existing Wave tests passed on the XR Elite's Adreno 650.
This verifies the app-side framebuffer math, not Wave's final camera composition
or the through-the-lens appearance. Headset visual testing remains necessary.

## References

- [Android Bitmap premultiplication](https://developer.android.com/reference/android/graphics/Bitmap#setPremultiplied(boolean))
- [Wave passthrough underlay](https://hub.vive.com/storage/docs/en-us/UnrealPlugin/Unreal_PassthroughUnderlay.html)

Local APK archiving and in-place installation only; no GitHub publication.

The verified local build is version `2.0 / 202581613`, installed with `adb install
-r` on the connected XR Elite. The APK is archived as
`Wolvic-vivexr-stage-64-passthrough-ui-v202581613.apk` (SHA-256:
`3CF44878C0C218826EA6D8BEB6E7B7C1E3483DDF5C345BD4E9F434BF770C076D`).
