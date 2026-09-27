# VIVE XR Elite: background passthrough and rendering safeguards

This is a local development build following Stage 62. It is not an upstream
Wolvic release. The changes do not claim to fix the remaining distant-scene
flicker until they are verified through the headset lenses.

## Background passthrough

- Use **Settings > Display > Passthrough background** for an immediate toggle,
  or the existing passthrough item in the browser's three-dot menu.
- **Start with Passthrough Mode** controls the next launch separately.
- Wave passthrough underlay runs behind browser windows. Opaque immersive VR
  temporarily hides it; leaving immersive VR restores the selected background.
- Camera-service calls run on a serialized worker, not the render thread.
- Unsupported/failed requests retain the virtual background and show an error.
- System passthrough and suspend/resume are handled separately from the user's
  selected background. The underlay is disabled during native shutdown.
- This does **not** add immersive WebXR AR support.

## Rendering fixes

- The external-texture copy now uses the producer's normalized left/right eye
  rectangles instead of always sampling fixed stereo halves. Invalid rectangles
  are discarded. Existing full-height side-by-side content keeps its orientation.
- Surface lookup failures are no longer cached permanently. JNI lookup passes
  the surface handle using its declared Java `long` argument type.
- Wave texture queues are rejected if any FBO creation fails, preserving the
  runtime's slot-to-framebuffer mapping. Unavailable (`-1`) or out-of-range slots
  cannot be indexed or submitted. A mid-frame resize acquires new slots without
  changing that frame's pose.
- Shader shutdown now checks the fragment shader handle before deleting it.
- Wave tracks acquired frames explicitly and completes them only after texture
  release, including frames discarded during transitions.

### Why the Wave ACK was not simply moved earlier

Gecko's Android `VRManager::SubmitFrameInternal` starts the next RAF immediately
after a successful submit acknowledgement. Publishing that acknowledgement with
the previous sensor state can start the new frame with an old head pose.

Wave therefore records completion after release and publishes it with the next
`GetSyncPose` in `PushFramePoses`. Its existing pose prediction, submit ordering,
and browser wait timeout are retained. No new `glFinish` or CPU wait was added.
The earlier suspicion that this ACK boundary was an unnecessary delay is not a
proven performance diagnosis.

## Automated checks

`app/src/test/native/WaveRenderingTests.cpp` tests standard/cropped eye rectangles,
invalid rectangles, queue bounds, passthrough on/off, failure handling, rapid
toggle coalescing, unsupported devices, and shutdown. The Wave service calls in
this test executable are mocked: passing these tests is not a camera or visual
quality test. Run it on an attached Android device with:

```powershell
./tools/tests/run-wave-rendering-tests.ps1 -NdkRoot <NDK-directory> -Adb <adb.exe> -Serial <device-serial>
```

## References

- [HTC: passthrough underlay](https://hub.vive.com/storage/docs/en-us/WVR_ShowPassthroughUnderlay.html)
- [HTC: MR content tutorial](https://hub.vive.com/storage/docs/en-us/TutorialMR/TutorialForMRContents.html)
- [Mozilla: VRManager frame submission](https://github.com/mozilla/gecko-dev/blob/master/gfx/vr/VRManager.cpp)
