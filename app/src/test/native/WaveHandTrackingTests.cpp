/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "WaveHandTracking.h"
#include <cassert>
#include <cstdio>
#include <limits>

static bool connected = true, failData = false, poseValid = true, handSupported = true;
static uint32_t jointCount = 26;
static float pinch = 0;
static int starts = 0, stops = 0;
extern "C" uint64_t WVR_GetSupportedFeatures() {
  return handSupported ? WVR_SupportedFeature_HandTracking : 0;
}
extern "C" bool WVR_IsDeviceConnected(WVR_DeviceType) { return connected; }
extern "C" WVR_Result WVR_StartHandTracking(WVR_HandTrackerType) { ++starts; return WVR_Success; }
extern "C" void WVR_StopHandTracking(WVR_HandTrackerType) { ++stops; }
extern "C" WVR_Result WVR_GetHandJointCount(WVR_HandTrackerType, uint32_t* count) {
  *count = jointCount; return WVR_Success;
}
extern "C" WVR_Result WVR_GetHandTrackerInfo(WVR_HandTrackerType, WVR_HandTrackerInfo_t* info) {
  info->handModelTypeBitMask = WVR_HandModelType_WithoutController;
  info->pinchTHR = .8f; info->pinchOff = .5f;
  for (uint32_t i = 0; i < info->jointCount; ++i) {
    info->jointMappingArray[i] = static_cast<WVR_HandJoint>(25 - i);
    info->jointValidFlagArray[i] = WVR_HandJointValidFlag_PositionValid | WVR_HandJointValidFlag_RotationValid;
  }
  return WVR_Success;
}
extern "C" WVR_Result WVR_GetHandTrackingData(WVR_HandTrackerType, WVR_HandModelType,
    WVR_PoseOriginModel origin, WVR_HandTrackingData* data, WVR_HandPoseData_t* pose) {
  assert(origin == WVR_PoseOriginModel_OriginOnHead);
  if (failData) return WVR_Error_FeatureNotSupport;
  for (auto* h : {&data->right, &data->left}) {
    h->isValidPose = poseValid; h->confidence = 1;
    for (uint32_t i = 0; i < h->jointCount; ++i) {
      h->joints[i] = {}; h->joints[i].position.v[0] = static_cast<float>(25 - i);
      h->joints[i].rotation.w = 1;
    }
  }
  for (auto* p : {&pose->right, &pose->left}) {
    p->pinch.base.type = WVR_HandPoseType_Pinch;
    p->pinch.finger = WVR_FingerType_Index;
    p->pinch.strength = pinch;
    p->pinch.direction.v[2] = -1;
  }
  return WVR_Success;
}
int main() {
  crow::WaveHandTracking hands;
  assert(hands.Initialize());
  assert(!hands.Update(false) && starts == 0);
  connected = false;
  assert(!hands.Update(true) && starts == 0);
  connected = true;
  pinch = 1;
  assert(hands.Update(true) && starts == 1);
  assert(hands.Valid(0) && hands.Valid(1));
  for (int i = 0; i < 26; ++i) assert(hands.Joint(0, i).position.v[0] == i);
  assert(!hands.Pinched(0)); // held pinch on first acquisition must not click
  pinch = 0;
  assert(hands.Update(true) && !hands.Pinched(0));
  pinch = .85f;
  assert(hands.Update(true) && hands.Pinched(0));
  assert(!hands.Pinched(1)); // independently armed per hand
  pinch = .65f;
  assert(hands.Update(true) && hands.Pinched(0)); // hysteresis
  pinch = .45f;
  assert(hands.Update(true) && !hands.Pinched(0));
  pinch = std::numeric_limits<float>::quiet_NaN();
  assert(hands.Update(true) && hands.Strength(0) == 0 && !hands.Pinched(0));
  failData = true;
  assert(!hands.Update(true) && !hands.Valid(0) && !hands.Pinched(0));
  failData = false; pinch = 1;
  assert(hands.Update(true) && !hands.Pinched(0));
  poseValid = false;
  assert(hands.Update(true) && !hands.Valid(1));
  poseValid = true;
  assert(!hands.Update(false) && stops == 1 && !hands.Valid(0));
  hands.Stop(); assert(stops == 1);
  crow::WaveHandTracking invalid;
  jointCount = 129;
  assert(invalid.Initialize() && !invalid.Update(true));
  assert(starts == 2 && stops == 2); // reject excessive buffers and release tracker
  handSupported = false;
  crow::WaveHandTracking unsupported;
  assert(!unsupported.Initialize() && !unsupported.Update(true) && starts == 2);
  std::puts("Wave hand tracking lifecycle, joint mapping and pinch tests passed");
}
