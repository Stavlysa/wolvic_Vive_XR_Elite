# Stage 67: device-provided Wave hand models

Replaces the generic Wolvic hand assets introduced in Stage 66 with the
headset's natural-hand models, fetched with `WVR_GetCurrentNaturalHandModel`.
The VIVE Browser 2.6 native library imports this same model API.

The independent renderer uses the device-provided left/right geometry,
inverse bind transforms, bone hierarchy, hand scale and alpha texture.
The appearance is translucent with a cool gray/blue tint and wrist fade.
Geometry comes from the same runtime source as VIVE Browser; the material
is not an exact reproduction of its proprietary rendering effects.

Models are loaded asynchronously, cached on the GPU, and the SDK-owned
CPU buffers are released after upload. No proprietary APK model assets
are extracted or included in the source tree or APK. Input and pointer
behavior remain the Stage 66 implementation.

## Validation and archive

- Release build passed; ADB update installed version code `202710459`.
- Archived APK: `Wolvic-vivexr-stage-67-wave-native-hands-v202710459.apk`.
- SHA-256: `52841c334d3f1685d681932c88e442830e1fa3861911c12e5e575acfb8ab86c9`.
- Headset GPU test passed for the production shaders, asynchronous model
  upload/release, both meshes, alpha blending and disabled-hand visibility,
  using synthetic SDK model data. This does not verify device mesh alignment.
- Startup confirms hand tracking support. At the initial post-install check,
  no valid wearer hand sample/native-model upload log had yet been observed.
  Physical appearance and alignment still require wearer confirmation.
- No GitHub upload was performed.
