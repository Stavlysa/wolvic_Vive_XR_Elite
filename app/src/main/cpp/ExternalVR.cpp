/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "ExternalVR.h"
#include "VRBrowser.h"

#include "vrb/Matrix.h"
#include "vrb/Quaternion.h"
#include "vrb/Vector.h"
#include "moz_external_vr.h"
#include "Assertions.h"
#include <algorithm>
#include <assert.h>
#include <chrono>
#include <pthread.h>
#include <unistd.h>

namespace {

const float SecondsToNanoseconds = 1e9f;
const int SecondsToNanosecondsI32 = int(1e9);
const int MicrosecondsToNanoseconds = 1000;

class Lock {
  pthread_mutex_t* mMutex;
  bool mLocked;
public:
  Lock() = delete;
  explicit Lock(pthread_mutex_t* aMutex) : mMutex(aMutex), mLocked(false) {
    if (pthread_mutex_lock(mMutex) == 0) {
      mLocked = true;
    }
  }

  ~Lock() {
    if (mLocked) {
      pthread_mutex_unlock(mMutex);
    }
  }

  bool IsLocked() {
    return mLocked;
  }

private:
  VRB_NO_DEFAULTS(Lock)
  VRB_NO_NEW_DELETE
};

class Wait {
  pthread_mutex_t* mMutex;
  pthread_cond_t* mCond;
  bool mLocked;
public:
  Wait() = delete;
  Wait(pthread_mutex_t* aMutex, pthread_cond_t* aCond)
      : mMutex(aMutex)
      , mCond(aCond)
      , mLocked(false)
  {}

  ~Wait() {
    if (mLocked) {
      pthread_mutex_unlock(mMutex);
    }
  }

  bool DoWait(const float aWait) {
    if (mLocked || pthread_mutex_lock(mMutex) == 0) {
      mLocked = true;
      if (aWait == 0.0f) {
        return pthread_cond_wait(mCond, mMutex) == 0;
      } else {
        float sec = 0;
        float nsec = modff(aWait, &sec);
        struct timeval tv = {};
        struct timespec ts = {};
        gettimeofday(&tv, nullptr);
        ts.tv_sec = tv.tv_sec + int(sec);
        ts.tv_nsec = (tv.tv_usec * MicrosecondsToNanoseconds) + int(SecondsToNanoseconds * nsec);
        if (ts.tv_nsec >= SecondsToNanosecondsI32) {
          ts.tv_nsec -= SecondsToNanosecondsI32;
          ts.tv_sec++;
        }
        return pthread_cond_timedwait(mCond, mMutex, &ts) == 0;
      }
    }
    return false;
  }

  bool IsLocked() {
    return mLocked;
  }

  void Lock() {
    if (mLocked) {
      return;
    }

    if (pthread_mutex_lock(mMutex) == 0) {
      mLocked = true;
    }
  }
  void Unlock() {
    if (mLocked) {
      mLocked = false;
      pthread_mutex_unlock(mMutex);
    }
  }

private:
  VRB_NO_DEFAULTS(Wait)
  VRB_NO_NEW_DELETE
};

// template method that returns the data size of a std::array of type T
template <typename T, size_t N>
size_t
arraySize(const std::array<T, N>&) {
  return sizeof(T) * N;
}

} // namespace

namespace crow {

struct ExternalVR::State {
  static ExternalVR::State* sState;
  pthread_mutex_t* browserMutex = nullptr;
  pthread_cond_t* browserCond = nullptr;
  mozilla::gfx::VRBrowserState* sourceBrowserState = nullptr;
  mozilla::gfx::VRExternalShmem data = {};
  mozilla::gfx::VRSystemState system = {};
  mozilla::gfx::VRBrowserState browser = {};
  // device::CapabilityFlags deviceCapabilities = 0;
  vrb::Matrix eyeTransforms[device::EyeCount];
  uint64_t lastFrameId = 0;
  uint64_t pendingFrameId = 0;
  uint64_t lastAcknowledgedFrameId = 0;
  bool frameOwned = false;
  std::chrono::steady_clock::time_point frameAcquireTime;
  uint64_t frameAcquireToAckNanoseconds = 0;
  uint64_t frameAcquireToAckMaxNanoseconds = 0;
  uint32_t frameOwnershipCompletedCount = 0;
  uint32_t frameOwnershipFreshCount = 0;
  uint32_t frameOwnershipTimeoutCount = 0;
  uint32_t frameOwnershipViolationCount = 0;
  bool firstPresentingFrame = false;
  bool compositorEnabled = true;
  bool waitingForExit = false;

  State() {
    pthread_mutex_init(&data.systemMutex, nullptr);
    pthread_mutex_init(&data.geckoMutex, nullptr);
    pthread_mutex_init(&data.servoMutex, nullptr);
    pthread_cond_init(&data.systemCond, nullptr);
    pthread_cond_init(&data.geckoCond, nullptr);
    pthread_cond_init(&data.servoCond, nullptr);
  }

  ~State() {
    pthread_mutex_destroy(&(data.systemMutex));
    pthread_mutex_destroy(&(data.geckoMutex));
    pthread_mutex_destroy(&(data.servoMutex));
    pthread_cond_destroy(&(data.systemCond));
    pthread_cond_destroy(&(data.geckoCond));
    pthread_cond_destroy(&(data.servoCond));
  }

  void ResetFrameOwnership() {
    pendingFrameId = 0;
    lastAcknowledgedFrameId = 0;
    frameOwned = false;
    frameAcquireTime = {};
    frameAcquireToAckNanoseconds = 0;
    frameAcquireToAckMaxNanoseconds = 0;
    frameOwnershipCompletedCount = 0;
    frameOwnershipFreshCount = 0;
    frameOwnershipTimeoutCount = 0;
    frameOwnershipViolationCount = 0;
  }

  void Reset() {
    memset(&data, 0, sizeof(mozilla::gfx::VRExternalShmem));
    memset(&system, 0, sizeof(mozilla::gfx::VRSystemState));
    memset(&browser, 0, sizeof(mozilla::gfx::VRBrowserState));
    data.version = mozilla::gfx::kVRExternalVersion;
    data.size = sizeof(mozilla::gfx::VRExternalShmem);
    system.displayState.isConnected = true;
    system.displayState.isMounted = true;
    system.displayState.nativeFramebufferScaleFactor = 1.0f;
    const vrb::Matrix identity = vrb::Matrix::Identity();
    memcpy(system.sensorState.leftViewMatrix.data(), identity.Data(), arraySize(system.sensorState.leftViewMatrix));
    memcpy(system.sensorState.rightViewMatrix.data(), identity.Data(), arraySize(system.sensorState.rightViewMatrix));
    system.sensorState.pose.orientation[3] = 1.0f;
    lastFrameId = 0;
    ResetFrameOwnership();
    firstPresentingFrame = false;
    waitingForExit = false;
    SetSourceBrowser(VRBrowserType::Gecko);
  }

  static ExternalVR::State& Instance() {
    if (!sState) {
      sState = new State();
    }

    return *sState;
  }

  void PullBrowserStateWhileLocked() {
    const bool wasPresenting = IsPresenting();
    memcpy(&browser, sourceBrowserState, sizeof(mozilla::gfx::VRBrowserState));


    if ((!wasPresenting && IsPresenting()) || browser.navigationTransitionActive) {
      firstPresentingFrame = true;
    }
    if (wasPresenting && !IsPresenting()) {
      lastFrameId = browser.layerState[0].layer_stereo_immersive.frameId;
      ResetFrameOwnership();
      waitingForExit = false;
    }
  }

  bool IsPresenting() const {
    return browser.presentationActive || browser.navigationTransitionActive || browser.layerState[0].type == mozilla::gfx::VRLayerType::LayerType_Stereo_Immersive;
  }

  void SetSourceBrowser(VRBrowserType aBrowser) {
    if (aBrowser == VRBrowserType::Gecko) {
      browserCond = &data.geckoCond;
      browserMutex = &data.geckoMutex;
      sourceBrowserState = &data.geckoState;
    } else {
      browserCond = &data.servoCond;
      browserMutex = &data.servoMutex;
      sourceBrowserState = &data.servoState;
    }
  }
};

ExternalVR::State * ExternalVR::State::sState = nullptr;

mozilla::gfx::VRControllerType GetVRControllerTypeByDevice(device::DeviceType aType) {
  mozilla::gfx::VRControllerType result = mozilla::gfx::VRControllerType::_empty;

  switch (aType) {
    case device::OculusGo:
      result = mozilla::gfx::VRControllerType::OculusGo;
      break;
    case device::OculusQuest:
      result = mozilla::gfx::VRControllerType::OculusTouch2;
      break;
    case device::OculusQuest2:
      result = mozilla::gfx::VRControllerType::OculusTouch3;
      break;
    case device::MetaQuest3:
      // Gecko 140 external VR ABI predates the Quest 3 controller enum.
      result = mozilla::gfx::VRControllerType::OculusTouch3;
      break;
    case device::MetaQuestPro:
      // FIXME: GeckoView does not support Quest Pro yet. Pretend to be the Quest2
      result = mozilla::gfx::VRControllerType::OculusTouch3;
          break;
    case device::HVR3DoF:
    case device::HVR6DoF:
    case device::VisionGlass:
    case device::ViveFocus:
      result = mozilla::gfx::VRControllerType::HTCViveFocus;
      break;
    // FIXME: Gecko does not support VRX. Controllers look similar to ViveFocusPlus
    case device::LenovoVRX:
    case device::ViveFocusPlus:
    case device::ViveXRElite:
      result = mozilla::gfx::VRControllerType::HTCViveFocusPlus;
      break;
    case device::PicoGaze:
      result = mozilla::gfx::VRControllerType::PicoGaze;
      break;
    case device::PicoNeo2:
      result = mozilla::gfx::VRControllerType::PicoNeo2;
      break;
    case device::PicoG2:
      result = mozilla::gfx::VRControllerType::PicoG2;
      break;
    case device::PicoNeo3:
      // Gecko 140 external VR ABI only exposes PicoNeo2.
      result = mozilla::gfx::VRControllerType::PicoNeo2;
      break;
    case device::Pico4x:
    case device::Pico4U:
      // Gecko 140 external VR ABI only exposes PicoNeo2.
      result = mozilla::gfx::VRControllerType::PicoNeo2;
      break;
    case device::MagicLeap2:
      // FIXME: Gecko does not support ML2 device yet, so let's use a similar one for WebXR.
      result = mozilla::gfx::VRControllerType::OculusGo;
      break;
    case device::LynxR1:
      // FIXME: Gecko does not support LynxR1 device yet, so let's use a similar one for WebXR.
      result = mozilla::gfx::VRControllerType::OculusTouch3;
      break;
    case device::PfdmYVR1:
    case device::PfdmYVR2:
      result = mozilla::gfx::VRControllerType::OculusTouch3;
      break;
    case device::PfdmMR:
      result = mozilla::gfx::VRControllerType::OculusTouch3;
      break;
    case device::UnknownType:
    default:
      result = mozilla::gfx::VRControllerType::_empty;
#ifndef NOAPI
      assert(!"Unknown controller type.");
#endif
      break;
  }
  return  result;
}

ExternalVRPtr
ExternalVR::Create() {
  return std::make_shared<ExternalVR>();
}

mozilla::gfx::VRExternalShmem*
ExternalVR::GetSharedData() {
  return &(m.data);
}

void
ExternalVR::SetDeviceName(const std::string& aName) {
  if (aName.length() == 0) {
    return;
  }
  strncpy(m.system.displayState.displayName.data(), aName.c_str(),
          mozilla::gfx::kVRDisplayNameMaxLen - 1);
  m.system.displayState.displayName[mozilla::gfx::kVRDisplayNameMaxLen - 1] = '\0';
}

void
ExternalVR::SetCapabilityFlags(const device::CapabilityFlags aFlags) {
  uint16_t result = 0;
  if (device::Position & aFlags) {
    result |= static_cast<uint16_t>(mozilla::gfx::VRDisplayCapabilityFlags::Cap_Position);
  }
  if (device::Orientation & aFlags) {
    result |= static_cast<uint16_t>(mozilla::gfx::VRDisplayCapabilityFlags::Cap_Orientation);
  }
  if (device::Present & aFlags) {
    result |= static_cast<uint16_t>(mozilla::gfx::VRDisplayCapabilityFlags::Cap_Present);
  }
  if (device::AngularAcceleration & aFlags) {
    result |= static_cast<uint16_t>(mozilla::gfx::VRDisplayCapabilityFlags::Cap_AngularAcceleration);
  }
  if (device::LinearAcceleration & aFlags) {
    result |= static_cast<uint16_t>(mozilla::gfx::VRDisplayCapabilityFlags::Cap_LinearAcceleration);
  }
  if (device::StageParameters & aFlags) {
    result |= static_cast<uint16_t>(mozilla::gfx::VRDisplayCapabilityFlags::Cap_StageParameters);
  }
  if (device::MountDetection & aFlags) {
    result |= static_cast<uint16_t>(mozilla::gfx::VRDisplayCapabilityFlags::Cap_MountDetection);
  }
  if (device::PositionEmulated & aFlags) {
    result |= static_cast<uint16_t>(mozilla::gfx::VRDisplayCapabilityFlags::Cap_PositionEmulated);
  }
  if (device::InlineSession & aFlags) {
    result |= static_cast<uint16_t>(mozilla::gfx::VRDisplayCapabilityFlags::Cap_Inline);
  }
  if (device::ImmersiveVRSession & aFlags) {
    result |= static_cast<uint16_t>(mozilla::gfx::VRDisplayCapabilityFlags::Cap_ImmersiveVR);
  }
  if (device::ImmersiveARSession & aFlags) {
    result |= static_cast<uint16_t>(mozilla::gfx::VRDisplayCapabilityFlags::Cap_ImmersiveAR);
  }
  //m.deviceCapabilities = aFlags;
  m.system.displayState.capabilityFlags = static_cast<mozilla::gfx::VRDisplayCapabilityFlags>(result);
  m.system.sensorState.flags = m.system.displayState.capabilityFlags;
}

void
ExternalVR::SetFieldOfView(const device::Eye aEye, const double aLeftDegrees,
                           const double aRightDegrees,
                           const double aTopDegrees,
                           const double aBottomDegrees) {
  mozilla::gfx::VRDisplayState::Eye which = (aEye == device::Eye::Right
                                             ? mozilla::gfx::VRDisplayState::Eye_Right
                                             : mozilla::gfx::VRDisplayState::Eye_Left);
  m.system.displayState.eyeFOV[which].upDegrees = aTopDegrees;
  m.system.displayState.eyeFOV[which].rightDegrees = aRightDegrees;
  m.system.displayState.eyeFOV[which].downDegrees = aBottomDegrees;
  m.system.displayState.eyeFOV[which].leftDegrees = aLeftDegrees;
}

void
ExternalVR::SetEyeOffset(const device::Eye aEye, const float aX, const float aY, const float aZ) {
  SetEyeTransform(aEye, vrb::Matrix::Translation(vrb::Vector(aX, aY, aZ)));
}

void
ExternalVR::SetEyeTransform(const device::Eye aEye, const vrb::Matrix& aTransform) {
  mozilla::gfx::VRDisplayState::Eye which = (aEye == device::Eye::Right
                                              ? mozilla::gfx::VRDisplayState::Eye_Right
                                              : mozilla::gfx::VRDisplayState::Eye_Left);
  const vrb::Vector translation = aTransform.GetTranslation();
  m.system.displayState.eyeTranslation[which].x = translation.x();
  m.system.displayState.eyeTranslation[which].y = translation.y();
  m.system.displayState.eyeTranslation[which].z = translation.z();
  m.eyeTransforms[device::EyeIndex(aEye)] = aTransform;
}

void
ExternalVR::SetEyeResolution(const int32_t aWidth, const int32_t aHeight) {
  m.system.displayState.eyeResolution.width = aWidth;
  m.system.displayState.eyeResolution.height = aHeight;
}

void
ExternalVR::SetNativeFramebufferScaleFactor(const float aScale) {
  m.system.displayState.nativeFramebufferScaleFactor = aScale;
}

void
ExternalVR::SetStageSize(const float aWidth, const float aDepth) {
  m.system.displayState.stageSize.width = aWidth;
  m.system.displayState.stageSize.height = aDepth;
}

void
ExternalVR::SetSittingToStandingTransform(const vrb::Matrix& aTransform) {
  memcpy(m.system.displayState.sittingToStandingTransform.data(), aTransform.Data(), arraySize(m.system.displayState.sittingToStandingTransform));
}

void
ExternalVR::SetBlendModes(std::vector<device::BlendMode> aBlendModes) {
  // Gecko 140 external VR ABI advertises one blend mode rather than a list.
  // Prefer opaque when the runtime supports it.
  device::BlendMode blendMode = device::BlendMode::Opaque;
  if (std::find(aBlendModes.begin(), aBlendModes.end(), device::BlendMode::Opaque) == aBlendModes.end() &&
      !aBlendModes.empty()) {
    blendMode = aBlendModes.front();
  }

  switch (blendMode) {
    case device::BlendMode::Opaque:
      m.system.displayState.blendMode = mozilla::gfx::VRDisplayBlendMode::Opaque;
      break;
    case device::BlendMode::Additive:
      m.system.displayState.blendMode = mozilla::gfx::VRDisplayBlendMode::Additive;
      break;
    case device::BlendMode::AlphaBlend:
      m.system.displayState.blendMode = mozilla::gfx::VRDisplayBlendMode::AlphaBlend;
      break;
    default:
      THROW(Fmt("Unknown blend mode", (int) blendMode));
      break;
  }
}

void
ExternalVR::PushSystemState() {
  Lock lock(&(m.data.systemMutex));
  if (lock.IsLocked()) {
    memcpy(&(m.data.state), &(m.system), sizeof(mozilla::gfx::VRSystemState));
    pthread_cond_signal(&m.data.systemCond);
  }
}

void
ExternalVR::PullBrowserState() {
  Lock lock(m.browserMutex);
  if (lock.IsLocked()) {
   m.PullBrowserStateWhileLocked();
  }
}

void
ExternalVR::SetSourceBrowser(VRBrowserType aBrowser) {
  m.SetSourceBrowser(aBrowser);
}

uint64_t
ExternalVR::GetFrameId() const {
  return m.lastFrameId;
}

void
ExternalVR::SetCompositorEnabled(bool aEnabled) {
  if (aEnabled == m.compositorEnabled) {
    return;
  }
  m.compositorEnabled = aEnabled;
  if (aEnabled) {
    // Set suppressFrames to avoid a deadlock between the sync surfaceChanged call
    // and the gecko VRManager SubmitFrame result wait.
    m.system.displayState.suppressFrames = true;
    PushSystemState();
    VRBrowser::OnExitWebXR([=]{
        m.system.displayState.suppressFrames = false;
        PushSystemState();
    });
  } else {
    // Set suppressFrames to avoid a deadlock between the compositor sync pause call
    // and the gecko VRManager SubmitFrame result wait.
    m.system.displayState.suppressFrames = true;
    m.system.displayState.lastSubmittedFrameId = 0;
    m.lastFrameId = 0;
    m.ResetFrameOwnership();
    PushSystemState();
    VRBrowser::OnEnterWebXR();
    m.system.displayState.suppressFrames = false;
    PushSystemState();
  }
}

bool
ExternalVR::IsPresenting() const {
  return m.IsPresenting();
}

uint16_t
ExternalVR::GetControllerCapabilityFlags(device::CapabilityFlags aFlags) {
  uint16_t result = 0;
  if (device::Position & aFlags) {
    result |= static_cast<uint16_t>(mozilla::gfx::ControllerCapabilityFlags::Cap_Position);
  }
  if (device::Orientation & aFlags) {
    result |= static_cast<uint16_t>(mozilla::gfx::ControllerCapabilityFlags::Cap_Orientation);
  }
  if (device::AngularAcceleration & aFlags) {
    result |= static_cast<uint16_t>(mozilla::gfx::ControllerCapabilityFlags::Cap_AngularAcceleration);
  }
  if (device::LinearAcceleration & aFlags) {
    result |= static_cast<uint16_t>(mozilla::gfx::ControllerCapabilityFlags::Cap_LinearAcceleration);
  }
  if (device::PositionEmulated & aFlags) {
    result |= static_cast<uint16_t>(mozilla::gfx::ControllerCapabilityFlags::Cap_PositionEmulated);
  }
  if (device::GripSpacePosition & aFlags) {
    result |= static_cast<uint16_t>(mozilla::gfx::ControllerCapabilityFlags::Cap_GripSpacePosition);
  }

  return result;
}

ExternalVR::VRState
ExternalVR::GetVRState() const {
  if (!IsPresenting()) {
    return VRState::NotPresenting;
  } else if (m.browser.navigationTransitionActive) {
    return VRState::LinkTraversal;
  } else if (m.firstPresentingFrame || m.waitingForExit || m.browser.layerState[0].type != mozilla::gfx::VRLayerType::LayerType_Stereo_Immersive) {
    return VRState::Loading;
  }

  return VRState::Rendering;
}

uint64_t
ExternalVR::PushFramePoses(const vrb::Matrix& aHeadTransform, const std::vector<Controller>& aControllers, const double aTimestamp) {
  const vrb::Matrix inverseHeadTransform = aHeadTransform.Inverse();
  vrb::Quaternion quaternion(inverseHeadTransform);
  vrb::Vector translation = aHeadTransform.GetTranslation();
  memcpy(m.system.sensorState.pose.orientation.data(), quaternion.Data(), arraySize(m.system.sensorState.pose.orientation));
  memcpy(m.system.sensorState.pose.position.data(), translation.Data(), arraySize(m.system.sensorState.pose.position));
  m.system.sensorState.inputFrameID++;
#if defined(VIVEXR) || defined(WAVEVR)
  // Never acknowledge a single-buffer SurfaceTexture until it is released.
  m.system.displayState.lastSubmittedFrameId = m.lastAcknowledgedFrameId;
#else
  // Keep the legacy acknowledgement flow on other backends.
  m.system.displayState.lastSubmittedFrameId = m.lastFrameId;
#endif

  vrb::Matrix leftView = m.eyeTransforms[device::EyeIndex(device::Eye::Left)].Inverse().PostMultiply(inverseHeadTransform);
  vrb::Matrix rightView = m.eyeTransforms[device::EyeIndex(device::Eye::Right)].Inverse().PostMultiply(inverseHeadTransform);
  memcpy(m.system.sensorState.leftViewMatrix.data(), leftView.Data(), arraySize(m.system.sensorState.leftViewMatrix));
  memcpy(m.system.sensorState.rightViewMatrix.data(), rightView.Data(), arraySize(m.system.sensorState.rightViewMatrix));

  memset(m.system.controllerState.data(), 0, arraySize(m.system.controllerState));
  for (int i = 0; i < aControllers.size(); ++i) {
    const Controller& controller = aControllers[i];
    if (controller.immersiveName.empty() || !controller.enabled) {
      continue;
    }
    mozilla::gfx::VRControllerState& immersiveController = m.system.controllerState[i];
    memcpy(immersiveController.controllerName.data(), controller.immersiveName.c_str(), controller.immersiveName.size() + 1);
    immersiveController.numButtons = controller.numButtons;
    immersiveController.buttonPressed = controller.immersivePressedState;
    immersiveController.buttonTouched = controller.immersiveTouchedState;
    for (int j = 0; j < controller.numButtons; ++j) {
      immersiveController.triggerValue[j] = controller.immersiveTriggerValues[j];
    }
    immersiveController.numAxes = controller.numAxes;
    for (int j = 0; j < controller.numAxes; ++j) {
      immersiveController.axisValue[j] = controller.immersiveAxes[j];
    }
    immersiveController.numHaptics = controller.numHaptics;
    immersiveController.hand = controller.leftHanded ? mozilla::gfx::ControllerHand::Left : mozilla::gfx::ControllerHand::Right;
    immersiveController.type = GetVRControllerTypeByDevice(controller.type);

    const uint16_t flags = GetControllerCapabilityFlags(controller.deviceCapabilities);
    immersiveController.flags = static_cast<mozilla::gfx::ControllerCapabilityFlags>(flags);

    if (flags & static_cast<uint16_t>(mozilla::gfx::ControllerCapabilityFlags::Cap_Orientation)) {
      immersiveController.isOrientationValid = true;

      vrb::Quaternion rotate(controller.transformMatrix.AfineInverse());
      memcpy(immersiveController.targetRayPose.orientation.data(), rotate.Data(), arraySize(immersiveController.targetRayPose.orientation));

      if (flags & static_cast<uint16_t>(mozilla::gfx::ControllerCapabilityFlags::Cap_Position) || flags & static_cast<uint16_t>(mozilla::gfx::ControllerCapabilityFlags::Cap_PositionEmulated)) {
        vrb::Vector position(controller.transformMatrix.GetTranslation());
        memcpy(immersiveController.targetRayPose.position.data(), position.Data(), arraySize(immersiveController.targetRayPose.position));
      }
    }

    if (flags & static_cast<uint16_t>(mozilla::gfx::ControllerCapabilityFlags::Cap_GripSpacePosition)) {
#ifdef OPENXR
      auto immersiveBeamTransform = controller.immersiveBeamTransform;
#else
      auto immersiveBeamTransform = controller.transformMatrix.PostMultiply(controller.immersiveBeamTransform);
#endif
      vrb::Vector position(immersiveBeamTransform.GetTranslation());
      vrb::Quaternion rotate(immersiveBeamTransform.AfineInverse());
      memcpy(immersiveController.pose.position.data(), position.Data(), arraySize(immersiveController.pose.position));
      memcpy(immersiveController.pose.orientation.data(), rotate.Data(), arraySize(immersiveController.pose.orientation));
    }

    // TODO:: We should add TargetRayMode::_end in moz_external_vr.h to help this check.
    assert((uint8_t)mozilla::gfx::TargetRayMode::Screen == (uint8_t)device::TargetRayMode::Screen);
    immersiveController.targetRayMode = (mozilla::gfx::TargetRayMode)controller.targetRayMode;
    immersiveController.mappingType = mozilla::gfx::GamepadMappingType::XRStandard;
    immersiveController.selectActionStartFrameId = controller.selectActionStartFrameId;
    immersiveController.selectActionStopFrameId = controller.selectActionStopFrameId;
    immersiveController.squeezeActionStartFrameId = controller.squeezeActionStartFrameId;
    immersiveController.squeezeActionStopFrameId = controller.squeezeActionStopFrameId;

#if CHROMIUM
    // WebXR hand-tracking support
    if (controller.mode == ControllerMode::Hand) {
      immersiveController.hasHandTrackingData = true;

      // While XR_EXT_hand_tracking extension specifies 26 joints, WebXR Hand input defines only
      // 25, being the palm joint the one omitted.
      // See https://registry.khronos.org/OpenXR/specs/1.0/html/xrspec.html#convention-of-hand-joints
      // vs. https://www.w3.org/TR/webxr-hand-input-1/#skeleton-joints-section.
      // Since the palm joint is at index zero, we pass joints from 1 to 25 (omitting the palm).
      assert(controller.handJointTransforms.size() == mozilla::gfx::kHandTrackingNumJoints + 1);
      for (int j = 0; j < mozilla::gfx::kHandTrackingNumJoints; j++) {
        memcpy(immersiveController.handTrackingData.handJointData[j].transform.data(),
               &controller.handJointTransforms[j + 1], sizeof(vrb::Matrix));
        immersiveController.handTrackingData.handJointData[j].radius = controller.handJointRadii[j + 1];
      }
    }
#endif
  }

  m.system.sensorState.timestamp = aTimestamp;

  PushSystemState();
  return m.system.sensorState.inputFrameID;
}

bool
ExternalVR::WaitFrameResult() {
  Wait wait(m.browserMutex, m.browserCond);
  wait.Lock();
  // browserMutex is locked in wait.lock().
  m.PullBrowserStateWhileLocked();
  while (true) {
    const uint64_t browserFrameId =
        m.browser.layerState[0].layer_stereo_immersive.frameId;
    if (!IsPresenting()) {
      m.firstPresentingFrame = false;
      break;
    }
    if (browserFrameId != m.lastFrameId) {
      m.firstPresentingFrame = false;
#if defined(VIVEXR) || defined(WAVEVR)
      if (m.frameOwned) {
        ++m.frameOwnershipViolationCount;
        VRB_WARN("VIVE XR Gecko frame ownership violation: pending=%llu incoming=%llu",
                 static_cast<unsigned long long>(m.pendingFrameId),
                 static_cast<unsigned long long>(browserFrameId));
      }
      m.pendingFrameId = browserFrameId;
      m.frameOwned = true;
      m.frameAcquireTime = std::chrono::steady_clock::now();
      ++m.frameOwnershipFreshCount;
#else
      m.system.displayState.lastSubmittedFrameSuccessful = true;
      m.system.displayState.lastSubmittedFrameId = browserFrameId;
#endif
      break;
    }

#if CHROMIUM
    if(m.browser.dropFrame) {
       m.system.displayState.droppedFrameCount++;
       return false;
    }
#endif

    if (m.firstPresentingFrame || m.waitingForExit) {
      return true; // Do not block to show loading screen until the first frame arrives.
    }
    // VRB_LOG("RequestFrame ABOUT TO WAIT FOR FRAME %llu %llu",m.browser.layerState[0].layer_stereo_immersive.frameId, m.lastFrameId);
#if defined(VIVEXR)
    // Do not let Gecko's WebXR frame rate pace the OpenXR frame loop. A short
    // wait still gives a completed browser frame priority, but on timeout the
    // VIVE path submits the previously released swapchain image again so the
    // runtime can keep asynchronous head-pose reprojection running at the
    // display refresh rate. A zero timeout is not valid here: Wait::DoWait(0)
    // means wait indefinitely.
    const float kConditionTimeout = 0.001f;
#else
    const float kConditionTimeout = 0.25f;
#endif
    // Wait causes the current thread to block until the condition variable is notified or the timeout happens.
    // Waiting for the condition variable releases the mutex atomically. So GV can modify the browser data.
    if (!wait.DoWait(kConditionTimeout)) {
#if defined(VIVEXR)
      ++m.frameOwnershipTimeoutCount;
#endif
      return false;
    }
    // VRB_LOG("RequestFrame DONE TO WAIT FOR FRAME");

    // browserMutex lock is reacquired again after the condition variable wait exits.
    m.PullBrowserStateWhileLocked();
  }
  m.lastFrameId = m.browser.layerState[0].layer_stereo_immersive.frameId;
  return true;
}

void
ExternalVR::CompleteFrameResult(bool aSuccessful) {
  if (!m.frameOwned) {
    return;
  }

  if (m.pendingFrameId != m.lastFrameId) {
    ++m.frameOwnershipViolationCount;
    VRB_WARN("VIVE XR Gecko frame ACK mismatch: pending=%llu acquired=%llu",
             static_cast<unsigned long long>(m.pendingFrameId),
             static_cast<unsigned long long>(m.lastFrameId));
  }

  m.system.displayState.lastSubmittedFrameSuccessful = aSuccessful;
  m.system.displayState.lastSubmittedFrameId = m.pendingFrameId;
  m.lastAcknowledgedFrameId = m.pendingFrameId;

#if defined(VIVEXR)
  if (m.frameAcquireTime.time_since_epoch().count() != 0) {
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - m.frameAcquireTime).count();
    if (elapsed > 0) {
      const uint64_t elapsedNanoseconds = static_cast<uint64_t>(elapsed);
      m.frameAcquireToAckNanoseconds += elapsedNanoseconds;
      m.frameAcquireToAckMaxNanoseconds =
          std::max(m.frameAcquireToAckMaxNanoseconds, elapsedNanoseconds);
    }
  }
  if (++m.frameOwnershipCompletedCount == 90) {
    VRB_LOG("VIVE XR Gecko frame ownership: fresh=%u timeouts=%u violations=%u avgAcquireToAck=%.3fms maxAcquireToAck=%.3fms latestInput=%llu renderedInput=%llu",
            m.frameOwnershipFreshCount, m.frameOwnershipTimeoutCount,
            m.frameOwnershipViolationCount,
            static_cast<double>(m.frameAcquireToAckNanoseconds) / 90000000.0,
            static_cast<double>(m.frameAcquireToAckMaxNanoseconds) / 1000000.0,
            static_cast<unsigned long long>(m.system.sensorState.inputFrameID),
            static_cast<unsigned long long>(
                m.browser.layerState[0].layer_stereo_immersive.inputFrameId));
    m.frameAcquireToAckNanoseconds = 0;
    m.frameAcquireToAckMaxNanoseconds = 0;
    m.frameOwnershipCompletedCount = 0;
    m.frameOwnershipFreshCount = 0;
    m.frameOwnershipTimeoutCount = 0;
    m.frameOwnershipViolationCount = 0;
  }
#endif

  m.pendingFrameId = 0;
  m.frameOwned = false;
  m.frameAcquireTime = {};

  // Gecko immediately starts its next RAF on a successful ACK. Wave must
  // publish that ACK together with the next GetSyncPose in PushFramePoses;
  // otherwise the new RAF can use the previous frame's sensor state.
#if !defined(WAVEVR)
  PushSystemState();
#endif
}

void
ExternalVR::CompleteEnumeration()
{
  m.system.enumerationCompleted = true;
  VRB_LOG("ExternalVR enumeration complete: version=%d size=%zu caps=0x%x blend=%d eye=%dx%d connected=%d mounted=%d",
          mozilla::gfx::kVRExternalVersion,
          sizeof(mozilla::gfx::VRExternalShmem),
          static_cast<unsigned int>(m.system.displayState.capabilityFlags),
          static_cast<int>(m.system.displayState.blendMode),
          m.system.displayState.eyeResolution.width,
          m.system.displayState.eyeResolution.height,
          m.system.displayState.isConnected ? 1 : 0,
          m.system.displayState.isMounted ? 1 : 0);
  // CompleteEnumeration can run immediately after the external context is
  // registered. Publish here instead of waiting for the first world frame so
  // Gecko cannot cache the earlier empty runtime state.
  PushSystemState();
}


void
ExternalVR::GetFrameResult(int32_t& aSurfaceHandle, int32_t& aTextureWidth, int32_t& aTextureHeight,
    uint64_t& aInputFrameId,
    device::EyeRect& aLeftEye, device::EyeRect& aRightEye) const {
  aSurfaceHandle = (int32_t)m.browser.layerState[0].layer_stereo_immersive.textureHandle;
  mozilla::gfx::VRLayerEyeRect& left = m.browser.layerState[0].layer_stereo_immersive.leftEyeRect;
  mozilla::gfx::VRLayerEyeRect& right = m.browser.layerState[0].layer_stereo_immersive.rightEyeRect;
  aLeftEye = device::EyeRect(left.x, left.y, left.width, left.height);
  aRightEye = device::EyeRect(right.x, right.y, right.width, right.height);
  aTextureWidth = (int32_t)m.browser.layerState[0].layer_stereo_immersive.textureSize.width;
  aTextureHeight = (int32_t)m.browser.layerState[0].layer_stereo_immersive.textureSize.height;
  aInputFrameId = m.browser.layerState[0].layer_stereo_immersive.inputFrameId;
}

void
ExternalVR::SetHapticState(ControllerContainerPtr aControllerContainer) const {
  const uint32_t count = aControllerContainer->GetControllerCount();
  uint32_t i = 0, j = 0;
  for (i = 0; i < count; ++i) {
    for (j = 0; j < mozilla::gfx::kVRHapticsMaxCount; ++j) {
      if (m.browser.hapticState[j].controllerIndex == i && m.browser.hapticState[j].inputFrameID) {
        aControllerContainer->SetHapticFeedback(i, m.browser.hapticState[j].inputFrameID,
                m.browser.hapticState[j].pulseDuration + m.browser.hapticState[j].pulseStart,
                m.browser.hapticState[j].pulseIntensity);
        break;
      }
    }
  }
}

void
ExternalVR::OnPause() {
  if (m.system.displayState.presentingGeneration == 0) {
    // Do not call PushSystemState() until correctly initialized.
    // Fixes WebXR Display not found error due to some superfluous pause/resume life cycle events.
    return;
  }
  m.system.displayState.isConnected = false;
  PushSystemState();
}

void
ExternalVR::OnResume() {
  if (m.system.displayState.presentingGeneration == 0) {
    // Do not call PushSystemState() until correctly initialized.
    // Fixes WebXR Display not found error due to some superfluous pause/resume life cycle events.
    return;
  }
  m.system.displayState.isConnected = true;
  PushSystemState();
}

void
ExternalVR::StopPresenting() {
  m.system.displayState.presentingGeneration++;
  PushSystemState();
  m.waitingForExit = true;
}

device::BlendMode
ExternalVR::GetImmersiveBlendMode() const {
  ASSERT(IsPresenting());
  switch (m.system.displayState.blendMode) {
    case mozilla::gfx::VRDisplayBlendMode::Opaque:
      return device::BlendMode::Opaque;
    case mozilla::gfx::VRDisplayBlendMode::Additive:
      return device::BlendMode::Additive;
    case mozilla::gfx::VRDisplayBlendMode::AlphaBlend:
      return device::BlendMode::AlphaBlend;
  }
}

DeviceDelegate::ImmersiveXRSessionType
ExternalVR::GetImmersiveXRSessionType() const {
  ASSERT(IsPresenting());
  // Gecko 140's external VR browser state does not carry a session type.
  // Its external presentation path is therefore treated as immersive-vr.
  return DeviceDelegate::ImmersiveXRSessionType::VR;
}

ExternalVR::ExternalVR(): m(State::Instance()) {
  m.Reset();
  PushSystemState();
}

}
