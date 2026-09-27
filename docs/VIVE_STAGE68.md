# Stage 68: foreground UI hands

Wave natural-hand models are drawn after the keyboard, controller UI and
transparent browser widgets. Their UI draw pass ignores the browser scene
depth buffer, without writing depth, and restores the previous depth-test
and depth-write state afterward. Other backends retain their existing order.

This fixes pages/toolbars covering the user's hand representation when the
hand intersects a browser surface. It does not move the hand pose or change
pinch input, WebXR page rendering, or the native model geometry.

The headset GPU regression test now includes a nearer simulated page depth
and verifies that the hands remain visible and GL depth state is restored.

Validation: release build passed; hand-model GPU occlusion/state tests and
existing Wave rendering/passthrough-alpha tests passed on the headset.
ADB installation confirmed version code `202710517` (version name `2.0`).
Archived APK: `Wolvic-vivexr-stage-68-foreground-hands-v202710517.apk`.
SHA-256: `79bb8bed7e0414477ba262505dac1a52ce0133d3798dcf6a5cef628024490b28`.
Wearer confirmation is still needed. No GitHub upload was performed.
