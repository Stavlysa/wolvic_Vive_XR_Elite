# Stage 66: Wave natural-hand input

Adds natural hand tracking to the VIVE XR Elite Wave backend. The VIVE Browser
2.6 native import inventory confirmed its use of the Wave natural-hand APIs.
This implementation uses the installed Wave 5.6 headers and Wolvic's own hand
renderer, not HTC Browser code or proprietary model assets.

- Automatically switches away from controller input when Wave selects hand
  interaction, or neither controller is connected.
- Reads both hands after the synchronized headset pose, using the same origin.
- Maps the runtime joint array into Wolvic's 26-joint ordering.
- Uses the runtime pinch ray for pointing and index/thumb pinch for selecting
  and dragging. Separate hysteresis/arming prevents false clicks on recovery.
- Left upright palm facing the user exposes the back/exit-immersive action.
- Stops tracking while paused or disabled; releases actions on lost tracking.
- Exposes the existing Settings > Controllers hand-tracking switch.

Gecko receives emulated tracked-pointer/select input. This does not add the
WebXR `XRHand` API to Gecko. Controller-free game support remains dependent on
the website's input requirements.

Validation: native lifecycle, shuffled joint mapping, pinch hysteresis,
independent hand state, failed pose reads and oversized-buffer rejection are
covered by `tools/tests/run-wave-hand-tests.ps1`. Physical pointing, hand-model
alignment and left-palm gestures require wearer testing.

## Build and device validation

- Final APK: version code `202710447`, version name `2.0`; archived as
  `Wolvic-vivexr-stage-66-hand-tracking-v202710447.apk`.
- SHA-256: `5436942d86af3275faae687f5fb6ae54c1ddb9476b1ae77fff46eda0c6a04594`.
- Release build, native hand tests, existing Wave rendering/GPU alpha tests,
  and the Android sparse-input-ID regression test passed.
- ADB update installation succeeded without clearing application data.
- Startup reported `Wave natural hand tracking supported=1`.
- Runtime requested hand poses; the initial sample reported invalid hand poses.
  This does not establish wearer-visible pointing or model alignment correctness.
- No GitHub source push or release publication was performed for this stage.
