/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "DeviceDelegateOpenXR.h"
#include "DeviceUtils.h"
#include "ElbowModel.h"
#include "BrowserEGLContext.h"
#include "HandMeshRenderer.h"
#include "VRBrowser.h"
#include "VRLayer.h"

#include <EGL/egl.h>
#include "vrb/CameraEye.h"
#include "vrb/Color.h"
#include "vrb/ConcreteClass.h"
#include "vrb/FBO.h"
#include "vrb/GLError.h"
#include "vrb/Matrix.h"
#include "vrb/Quaternion.h"
#include "vrb/RenderContext.h"
#include "vrb/Vector.h"

#include <vector>
#include <deque>
#include <array>
#include <algorithm>
#include <cmath>
#include <assert.h>
#include <cstdlib>
#include <unistd.h>
#include <sstream>
#include <string.h>

#include "VRBrowser.h"

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>
#include "OpenXRHelpers.h"
#include "OpenXRSwapChain.h"
#include "OpenXRInput.h"
#include "OpenXRInputMappings.h"
#include "OpenXRExtensions.h"
#include "OpenXRLayers.h"
#include "OpenXRPassthroughStrategy.h"

#include "OpenXRActionSet.h"

namespace crow {

#if defined(VIVEXR)
// XR_HTC_frame_synchronization was added after the OpenXR 1.0.34 headers
// used by the Android loader in this project. Keep the small vendor ABI local
// until the extension is published in the Khronos headers.
static constexpr const char* kViveFrameSynchronizationExtensionName =
    "XR_HTC_frame_synchronization";
static constexpr XrStructureType kViveFrameSynchronizationSessionBeginInfoType =
    static_cast<XrStructureType>(1000329000);

enum XrFrameSynchronizationModeHTC : uint32_t {
  XR_FRAME_SYNCHRONIZATION_MODE_STABILIZED_HTC = 1,
  XR_FRAME_SYNCHRONIZATION_MODE_PROMPT_HTC = 2,
};

struct XrFrameSynchronizationSessionBeginInfoHTC {
  XrStructureType type;
  const void* next;
  XrFrameSynchronizationModeHTC mode;
};

static double
QuaternionAngularDistanceDegrees(const XrQuaternionf& a, const XrQuaternionf& b) {
  double dot = static_cast<double>(a.x) * b.x +
               static_cast<double>(a.y) * b.y +
               static_cast<double>(a.z) * b.z +
               static_cast<double>(a.w) * b.w;
  dot = std::min(1.0, std::max(-1.0, std::fabs(dot)));
  return 2.0 * std::acos(dot) * 57.29577951308232;
}

static double
PosePositionDistanceMillimeters(const XrPosef& a, const XrPosef& b) {
  const double x = static_cast<double>(a.position.x) - b.position.x;
  const double y = static_cast<double>(a.position.y) - b.position.y;
  const double z = static_cast<double>(a.position.z) - b.position.z;
  return std::sqrt(x * x + y * y + z * z) * 1000.0;
}
#endif

struct HandMeshPropertiesMSFT {
    uint32_t indexCount = 0;
    uint32_t vertexCount = 0;
};
typedef std::unique_ptr<HandMeshPropertiesMSFT> HandMeshPropertiesMSFTPtr;

struct ImmersiveFramePoseOpenXR {
  uint64_t inputFrameId = 0;
  XrTime predictedDisplayTime = 0;
  std::vector<XrView> views;
};

struct DeviceDelegateOpenXR::State {
  vrb::RenderContextWeak context;
  JavaContext* javaContext = nullptr;
  bool layersEnabled = true;
  XrInstanceCreateInfoAndroidKHR java;
  XrInstance instance = XR_NULL_HANDLE;
  XrSystemId system = XR_NULL_SYSTEM_ID;
  XrSession session = XR_NULL_HANDLE;
  XrSessionState sessionState = XR_SESSION_STATE_UNKNOWN;
  bool vrReady = false;
  XrGraphicsBindingOpenGLESAndroidKHR graphicsBinding{XR_TYPE_GRAPHICS_BINDING_OPENGL_ES_ANDROID_KHR};
  XrSystemProperties systemProperties{XR_TYPE_SYSTEM_PROPERTIES};
  XrEventDataBuffer eventBuffer;
  XrViewConfigurationType viewConfigType{XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO};
  std::vector<XrViewConfigurationView> viewConfig;
  std::vector<XrView> views;
  std::vector<XrView> prevViews;
  std::vector<OpenXRSwapChainPtr> eyeSwapChains;
  OpenXRSwapChainPtr boundSwapChain;
  OpenXRSwapChainPtr previousBoundSwapchain;
  XrSpace viewSpace = XR_NULL_HANDLE;
  XrSpace localSpace = XR_NULL_HANDLE;
  XrSpace layersSpace = XR_NULL_HANDLE;
  XrSpace stageSpace = XR_NULL_HANDLE;
  std::vector<int64_t> swapchainFormats;
  OpenXRInputPtr input;
  OpenXRLayerCubePtr cubeLayer;
  OpenXRLayerEquirectPtr equirectLayer;
  std::vector<OpenXRLayerPtr> uiLayers;
  device::RenderMode renderMode = device::RenderMode::StandAlone;
  vrb::CameraEyePtr cameras[2];
  FramePrediction framePrediction = FramePrediction::NO_FRAME_AHEAD;
  XrTime prevPredictedDisplayTime = 0;
  XrTime predictedDisplayTime = 0;
  XrTime runtimePredictedDisplayTime = 0;
  XrDuration predictedDisplayPeriod = 0;
  uint32_t frameTimingDiagnosticCount = 0;
  XrDuration frameTimingDeltaSum = 0;
  XrDuration frameTimingDeltaMin = 0;
  XrDuration frameTimingDeltaMax = 0;
  uint32_t missedFrameTimingCount = 0;
  std::deque<ImmersiveFramePoseOpenXR> immersiveFramePoseHistory;
  std::vector<XrView> selectedImmersiveFrameViews;
  uint64_t selectedImmersiveFrameId = 0;
  std::vector<XrView> runtimeTargetViews;
  bool runtimeTargetViewsValid = false;
  XrTime selectedImmersiveFramePoseTime = 0;
  uint64_t latestImmersiveInputFrameId = 0;
  uint32_t poseMatchDiagnosticCount = 0;
  uint64_t poseMatchLagSum = 0;
  uint64_t poseMatchLagMax = 0;
  uint32_t poseMatchMissCount = 0;
  XrDuration poseMatchAgeSum = 0;
  XrDuration poseMatchAgeMax = 0;
  uint32_t latePoseSampleCount = 0;
  uint32_t latePoseInvalidCount = 0;
  double latePoseRotationSum = 0.0;
  double latePoseRotationMax = 0.0;
  double latePosePositionSum = 0.0;
  double latePosePositionMax = 0.0;
  XrTime lastSubmittedDisplayTime = 0;
  uint32_t submittedTimeDiagnosticCount = 0;
  uint32_t submittedTimeNonMonotonicCount = 0;
  uint32_t submittedTimeIrregularCount = 0;
  XrDuration submittedTimeStepSum = 0;
  XrDuration submittedTimeStepMin = 0;
  XrDuration submittedTimeStepMax = 0;
  bool hasLastStageTransformPose = false;
  XrPosef lastStageTransformPose = {};
  uint32_t stageTransformDiagnosticCount = 0;
  double stageTransformDriftSum = 0.0;
  double stageTransformDriftMax = 0.0;
  XrPosef predictedPose = {};
  XrPosef prevPredictedPose = {};
  uint32_t discardedFrameIndex = 0;
  int discardCount = 0;
  vrb::Color clearColor;
  float near = 0.1f;
  float far = 100.f;
  float nearMin { 0.1f };
  float farMax { std::numeric_limits<float>::max() };
  bool hasEventFocus = true;
  crow::ElbowModelPtr elbow;
  ControllerDelegatePtr controller;
  ImmersiveDisplayPtr immersiveDisplay;
  int reorientCount = -1;
  vrb::Matrix reorientMatrix = vrb::Matrix::Identity();
  device::CPULevel minCPULevel = device::CPULevel::Normal;
  device::DeviceType deviceType = device::UnknownType;
  std::vector<const XrCompositionLayerBaseHeader*> frameEndLayers;
  std::function<void()> controllersCreatedCallback;
  std::function<bool()> controllersReadyCallback;
  std::optional<XrPosef> firstPose;
  bool mHandTrackingSupported = false;
  std::vector<float> refreshRates;
  bool reorientRequested { false };
  XrTime reorientChangeTime { 0 };
  OpenXRLayerPassthroughPtr passthroughLayer { nullptr };
  PassthroughStrategyPtr passthroughStrategy;
  XrEnvironmentBlendMode defaultBlendMode { XR_ENVIRONMENT_BLEND_MODE_MAX_ENUM };
  XrEnvironmentBlendMode passthroughBlendMode { XR_ENVIRONMENT_BLEND_MODE_MAX_ENUM };
  HandMeshRendererPtr handMeshRenderer = nullptr;
  HandMeshPropertiesMSFTPtr handMeshProperties;
  bool keyboardTrackingSupported { false };
  bool renderModelLoadingSupported { false };
  float furthestHitDistance { near };
  OpenXRActionSetPtr eyeActionSet;
  PointerMode pointerMode { PointerMode::TRACKED_POINTER };
  std::vector<XrEnvironmentBlendMode> blendModes;
  XrEnvironmentBlendMode immersiveBlendMode { XR_ENVIRONMENT_BLEND_MODE_OPAQUE };
  bool isEyeTrackingSupported { false };
  bool handTrackingEnabled { true };
  bool shouldUsePassthrough { false };

  bool IsPositionTrackingSupported() {
      CHECK(system != XR_NULL_SYSTEM_ID);
      CHECK(instance != XR_NULL_HANDLE);
      return systemProperties.trackingProperties.positionTracking == XR_TRUE;
  }

  void Initialize() {
    vrb::RenderContextPtr localContext = context.lock();
    elbow = ElbowModel::Create();
    for (int i = 0; i < 2; ++i) {
      cameras[i] = vrb::CameraEye::Create(localContext->GetRenderThreadCreationContext());
    }

#ifndef HVR
    PFN_xrInitializeLoaderKHR initializeLoaderKHR;
    CHECK_XRCMD(xrGetInstanceProcAddr(nullptr, "xrInitializeLoaderKHR", reinterpret_cast<PFN_xrVoidFunction*>(&initializeLoaderKHR)));
    XrLoaderInitInfoAndroidKHR loaderData;
    memset(&loaderData, 0, sizeof(loaderData));
    loaderData.type = XR_TYPE_LOADER_INIT_INFO_ANDROID_KHR;
    loaderData.next = nullptr;
    loaderData.applicationVM = javaContext->vm;
    loaderData.applicationContext = javaContext->env->NewGlobalRef(javaContext->activity);
    initializeLoaderKHR(reinterpret_cast<XrLoaderInitInfoBaseHeaderKHR*>(&loaderData));
#endif

    // Initialize the XrInstance
    OpenXRExtensions::Initialize();

    std::vector<const char *> extensions {
      XR_KHR_ANDROID_CREATE_INSTANCE_EXTENSION_NAME,
      XR_KHR_OPENGL_ES_ENABLE_EXTENSION_NAME,
    };

    if (OpenXRExtensions::IsExtensionSupported(XR_KHR_ANDROID_SURFACE_SWAPCHAIN_EXTENSION_NAME)) {
      extensions.push_back(XR_KHR_ANDROID_SURFACE_SWAPCHAIN_EXTENSION_NAME);
    }
    if (OpenXRExtensions::IsExtensionSupported(XR_KHR_COMPOSITION_LAYER_CUBE_EXTENSION_NAME)) {
      extensions.push_back(XR_KHR_COMPOSITION_LAYER_CUBE_EXTENSION_NAME);
    }
    if (OpenXRExtensions::IsExtensionSupported(XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME)) {
      extensions.push_back(XR_KHR_COMPOSITION_LAYER_CYLINDER_EXTENSION_NAME);
    }
    if (OpenXRExtensions::IsExtensionSupported(XR_EXT_HAND_TRACKING_EXTENSION_NAME)) {
        extensions.push_back(XR_EXT_HAND_TRACKING_EXTENSION_NAME);
        if (OpenXRExtensions::IsExtensionSupported(XR_FB_HAND_TRACKING_AIM_EXTENSION_NAME)) {
            extensions.push_back(XR_FB_HAND_TRACKING_AIM_EXTENSION_NAME);
        }
        if (OpenXRExtensions::IsExtensionSupported(XR_MSFT_HAND_TRACKING_MESH_EXTENSION_NAME)) {
            extensions.push_back(XR_MSFT_HAND_TRACKING_MESH_EXTENSION_NAME);
        }
    }
    if (OpenXRExtensions::IsExtensionSupported(XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME)) {
        extensions.push_back(XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME);
    }
    if (OpenXRExtensions::IsExtensionSupported(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME)) {
        extensions.push_back(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME);
    }
#if defined(VIVEXR)
    if (OpenXRExtensions::IsExtensionSupported(kViveFrameSynchronizationExtensionName)) {
      extensions.push_back(kViveFrameSynchronizationExtensionName);
    }
#endif
#if defined(OCULUSVR) || defined(PFDMXR)
    if (OpenXRExtensions::IsExtensionSupported(XR_KHR_COMPOSITION_LAYER_EQUIRECT2_EXTENSION_NAME)) {
      extensions.push_back(XR_KHR_COMPOSITION_LAYER_EQUIRECT2_EXTENSION_NAME);
    }
    if (OpenXRExtensions::IsExtensionSupported(XR_FB_COMPOSITION_LAYER_IMAGE_LAYOUT_EXTENSION_NAME)) {
      extensions.push_back(XR_FB_COMPOSITION_LAYER_IMAGE_LAYOUT_EXTENSION_NAME);
    }
#endif
    if (OpenXRExtensions::IsExtensionSupported(XR_KHR_COMPOSITION_LAYER_COLOR_SCALE_BIAS_EXTENSION_NAME))
        extensions.push_back(XR_KHR_COMPOSITION_LAYER_COLOR_SCALE_BIAS_EXTENSION_NAME);

    if (OpenXRExtensions::IsExtensionSupported(XR_FB_PASSTHROUGH_EXTENSION_NAME)) {
      extensions.push_back(XR_FB_PASSTHROUGH_EXTENSION_NAME);
    }

    if (OpenXRExtensions::IsExtensionSupported(XR_ML_ML2_CONTROLLER_INTERACTION_EXTENSION_NAME))
        extensions.push_back(XR_ML_ML2_CONTROLLER_INTERACTION_EXTENSION_NAME);

    if (OpenXRExtensions::IsExtensionSupported(XR_EXTX_OVERLAY_EXTENSION_NAME))
        extensions.push_back(XR_EXTX_OVERLAY_EXTENSION_NAME);

    if (OpenXRExtensions::IsExtensionSupported(XR_FB_KEYBOARD_TRACKING_EXTENSION_NAME))
        extensions.push_back(XR_FB_KEYBOARD_TRACKING_EXTENSION_NAME);

    if (OpenXRExtensions::IsExtensionSupported(XR_FB_RENDER_MODEL_EXTENSION_NAME))
        extensions.push_back(XR_FB_RENDER_MODEL_EXTENSION_NAME);

    if (OpenXRExtensions::IsExtensionSupported(XR_ML_FRAME_END_INFO_EXTENSION_NAME))
        extensions.push_back(XR_ML_FRAME_END_INFO_EXTENSION_NAME);

    if (OpenXRExtensions::IsExtensionSupported(XR_EXT_HAND_INTERACTION_EXTENSION_NAME))
        extensions.push_back(XR_EXT_HAND_INTERACTION_EXTENSION_NAME);
    else if (OpenXRExtensions::IsExtensionSupported(XR_MSFT_HAND_INTERACTION_EXTENSION_NAME))
        extensions.push_back(XR_MSFT_HAND_INTERACTION_EXTENSION_NAME);

    if (OpenXRExtensions::IsExtensionSupported(XR_EXT_VIEW_CONFIGURATION_DEPTH_RANGE_EXTENSION_NAME))
        extensions.push_back(XR_EXT_VIEW_CONFIGURATION_DEPTH_RANGE_EXTENSION_NAME);

    if (OpenXRExtensions::IsExtensionSupported(XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME))
        extensions.push_back(XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME);

    if (OpenXRExtensions::IsExtensionSupported(XR_BD_CONTROLLER_INTERACTION_EXTENSION_NAME))
        extensions.push_back(XR_BD_CONTROLLER_INTERACTION_EXTENSION_NAME);

    // Required by the XR Elite runtime when the application requests OpenXR
    // 1.0. The interaction profile is promoted to core in OpenXR 1.1.
    if (OpenXRExtensions::IsExtensionSupported(XR_HTC_VIVE_FOCUS3_CONTROLLER_INTERACTION_EXTENSION_NAME))
        extensions.push_back(XR_HTC_VIVE_FOCUS3_CONTROLLER_INTERACTION_EXTENSION_NAME);

    java = {XR_TYPE_INSTANCE_CREATE_INFO_ANDROID_KHR};
    java.applicationVM = javaContext->vm;
    java.applicationActivity = javaContext->activity;

    XrInstanceCreateInfo createInfo{XR_TYPE_INSTANCE_CREATE_INFO};
    createInfo.next = (XrBaseInStructure*)&java;
    createInfo.enabledExtensionCount = (uint32_t)extensions.size();
    createInfo.enabledExtensionNames = extensions.data();
    strcpy(createInfo.applicationInfo.applicationName, "Wolvic");
    createInfo.applicationInfo.apiVersion = XR_CURRENT_API_VERSION;

    CHECK_XRCMD(xrCreateInstance(&createInfo, &instance));
    CHECK_MSG(instance != XR_NULL_HANDLE, "Failed to create XRInstance");

    XrInstanceProperties instanceProperties{XR_TYPE_INSTANCE_PROPERTIES};
    CHECK_XRCMD(xrGetInstanceProperties(instance, &instanceProperties));
    VRB_LOG("OpenXR Instance Created: RuntimeName=%s RuntimeVersion=%s", instanceProperties.runtimeName,
            GetXrVersionString(instanceProperties.runtimeVersion).c_str());

    // Load Extensions
    OpenXRExtensions::LoadExtensions(instance);

    // Initialize System
    XrSystemGetInfo systemInfo{XR_TYPE_SYSTEM_GET_INFO };
    systemInfo.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;
    CHECK_XRCMD(xrGetSystem(instance, &systemInfo, &system));
    CHECK_MSG(system != XR_NULL_SYSTEM_ID, "Failed to initialize XRSystem");

    deviceType = DeviceUtils::GetDeviceTypeFromSystem();

    // If hand tracking extension is present, query whether the runtime actually supports it
    XrSystemHandTrackingPropertiesEXT handTrackingProperties{XR_TYPE_SYSTEM_HAND_TRACKING_PROPERTIES_EXT};
    handTrackingProperties.supportsHandTracking = XR_FALSE;
    if (OpenXRExtensions::IsExtensionSupported(XR_EXT_HAND_TRACKING_EXTENSION_NAME)) {
        handTrackingProperties.next = systemProperties.next;
        systemProperties.next = &handTrackingProperties;
    }

    // If passthrough extension is present, query whether the runtime actually supports it
    XrSystemPassthroughPropertiesFB passthroughProperties{ XR_TYPE_SYSTEM_PASSTHROUGH_PROPERTIES_FB};
    passthroughProperties.supportsPassthrough = XR_FALSE;
    if (OpenXRExtensions::IsExtensionSupported(XR_FB_PASSTHROUGH_EXTENSION_NAME)) {
        passthroughProperties.next = systemProperties.next;
        systemProperties.next = &passthroughProperties;
    }

    XrSystemHandTrackingMeshPropertiesMSFT handMeshPropertiesMSFT{ XR_TYPE_SYSTEM_HAND_TRACKING_MESH_PROPERTIES_MSFT };
    handMeshPropertiesMSFT.supportsHandTrackingMesh = XR_FALSE;
    if (OpenXRExtensions::IsExtensionSupported(XR_MSFT_HAND_TRACKING_MESH_EXTENSION_NAME)) {
        handMeshPropertiesMSFT.next = systemProperties.next;
        systemProperties.next = &handMeshPropertiesMSFT;
    }

    // If XR_FB_keyboard_tracking is present, query whether the runtime actually supports it
    XrSystemKeyboardTrackingPropertiesFB kbdTrackingPropertiesFB{ XR_TYPE_SYSTEM_KEYBOARD_TRACKING_PROPERTIES_FB};
    kbdTrackingPropertiesFB.supportsKeyboardTracking = XR_FALSE;
    if (OpenXRExtensions::IsExtensionSupported(XR_FB_KEYBOARD_TRACKING_EXTENSION_NAME)) {
        kbdTrackingPropertiesFB.next = systemProperties.next;
        systemProperties.next = &kbdTrackingPropertiesFB;
    }

    // If XR_FB_render_model is supported, check whether the runtime is actually able to load models
    XrSystemRenderModelPropertiesFB renderModelPropertiesFB{ XR_TYPE_SYSTEM_RENDER_MODEL_PROPERTIES_FB };
    renderModelPropertiesFB.supportsRenderModelLoading = XR_FALSE;
    if (OpenXRExtensions::IsExtensionSupported(XR_FB_RENDER_MODEL_EXTENSION_NAME)) {
        renderModelPropertiesFB.next = systemProperties.next;
        systemProperties.next = &renderModelPropertiesFB;
    }

    XrSystemEyeGazeInteractionPropertiesEXT eyeGazeProperties{ XR_TYPE_SYSTEM_EYE_GAZE_INTERACTION_PROPERTIES_EXT };
    if (OpenXRExtensions::IsExtensionSupported(XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME)) {
        eyeGazeProperties.supportsEyeGazeInteraction = XR_FALSE;
        eyeGazeProperties.next = systemProperties.next;
        systemProperties.next = &eyeGazeProperties;
    }

    // Retrieve system info
    CHECK_XRCMD(xrGetSystemProperties(instance, system, &systemProperties))
    VRB_LOG("OpenXR system name: %s", systemProperties.systemName);

    layersEnabled = systemProperties.graphicsProperties.maxLayerCount > 1 && OpenXRExtensions::IsExtensionSupported(XR_KHR_ANDROID_SURFACE_SWAPCHAIN_EXTENSION_NAME);
#if defined(VIVEXR)
    // The VIVE XR Elite Wave runtime reports maxLayerCount=16 through OpenXR,
    // while its mobile compositor accepts only one projection layer plus three
    // overlays. Wolvic normally submits more UI layers than that and the
    // Android-surface swapchains are then reused/corrupted by the runtime.
    // Render widgets and the skybox through Wolvic's established projection
    // fallback instead. This also avoids submitting a cube layer that the
    // runtime does not advertise.
    layersEnabled = false;
    VRB_LOG("VIVE XR Elite: composition layers disabled; using projection fallback");
#endif
    if (systemProperties.graphicsProperties.maxLayerCount == 0)
        VRB_ERROR("OpenXR runtime reports 0 layers. There must be at least 1");

    mHandTrackingSupported = handTrackingProperties.supportsHandTracking;
    VRBrowser::SetHandTrackingSupported(mHandTrackingSupported);
    VRB_LOG("OpenXR runtime %s hand tracking", mHandTrackingSupported ? "does support" : "doesn't support");
    VRB_LOG("OpenXR runtime %s FB passthrough extension", passthroughProperties.supportsPassthrough ? "does support" : "doesn't support");

    InitializeBlendModes();
    if (passthroughProperties.supportsPassthrough) {
        passthroughStrategy = std::make_unique<OpenXRPassthroughStrategyFBExtension>();
    } else {
        CHECK(passthroughBlendMode != XR_ENVIRONMENT_BLEND_MODE_MAX_ENUM);
        if (passthroughBlendMode == XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND)
            passthroughStrategy = std::make_unique<OpenXRPassthroughStrategyBlendMode>();
        else
            passthroughStrategy = std::make_unique<OpenXRPassthroughStrategyUnsupported>();
    }

    if (handMeshPropertiesMSFT.supportsHandTrackingMesh) {
        handMeshProperties = std::make_unique<HandMeshPropertiesMSFT>();
        handMeshProperties->vertexCount = handMeshPropertiesMSFT.maxHandMeshVertexCount;
        handMeshProperties->indexCount = handMeshPropertiesMSFT.maxHandMeshIndexCount;
        VRB_LOG("OpenXR runtime supports XR_MSFT_hand_tracking_mesh");
    }

    if (kbdTrackingPropertiesFB.supportsKeyboardTracking) {
        keyboardTrackingSupported = true;
        VRB_LOG("OpenXR runtime supports XR_FB_keyboard_tracking");
    }

    if (renderModelPropertiesFB.supportsRenderModelLoading) {
      renderModelLoadingSupported = renderModelPropertiesFB.supportsRenderModelLoading;
      VRB_LOG("OpenXR runtime supports XR_FB_render_model");
    }

    if (OpenXRExtensions::IsExtensionSupported(XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME)) {
        isEyeTrackingSupported = eyeGazeProperties.supportsEyeGazeInteraction;
        VRB_LOG("OpenXR runtime %s support XR_EXT_eye_gaze_interaction", isEyeTrackingSupported ? "does" : "doesn't");
    }
    VRBrowser::SetEyeTrackingSupported(isEyeTrackingSupported);
  }

  // xrGet*GraphicsRequirementsKHR check must be called prior to xrCreateSession
  // xrCreateSession fails if we don't call it.
  void CheckGraphicsRequirements() {

    XrGraphicsRequirementsOpenGLESKHR graphicsRequirements{XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_ES_KHR};
    CHECK_XRCMD(OpenXRExtensions::sXrGetOpenGLESGraphicsRequirementsKHR(instance, system, &graphicsRequirements));

    GLint major = 0;
    GLint minor = 0;
    glGetIntegerv(GL_MAJOR_VERSION, &major);
    glGetIntegerv(GL_MINOR_VERSION, &minor);

    const XrVersion desiredApiVersion = XR_MAKE_VERSION(major, minor, 0);
    if (graphicsRequirements.minApiVersionSupported > desiredApiVersion) {
      THROW("Runtime does not support desired Graphics API and/or version");
    }
  }

  void InitializeSwapChainFormats() {
    uint32_t swapchainFormatCount;
    CHECK_XRCMD(xrEnumerateSwapchainFormats(session, 0, &swapchainFormatCount, nullptr));
    CHECK_MSG(swapchainFormatCount > 0, "OpenXR unexpected swapchainFormatCount");
    swapchainFormats.resize(swapchainFormatCount, 0);
    CHECK_XRCMD(xrEnumerateSwapchainFormats(session, (uint32_t)swapchainFormats.size(), &swapchainFormatCount,
                                            swapchainFormats.data()));
    VRB_LOG("OpenXR Available color formats: %d", swapchainFormatCount);
  }

  bool SupportsColorFormat(int64_t aColorFormat) {
    if (swapchainFormats.size() == 0) {
      InitializeSwapChainFormats();
    }
    return std::find(swapchainFormats.begin(), swapchainFormats.end(), aColorFormat) != swapchainFormats.end();
  }

  void InitializeViews() {
    CHECK(session != XR_NULL_HANDLE)
    // Enumerate configurations
    uint32_t viewCount;
    CHECK_XRCMD(xrEnumerateViewConfigurationViews(instance, system, viewConfigType, 0, &viewCount, nullptr));
    CHECK_MSG(viewCount > 0, "OpenXR unexpected viewCount");
    XrViewConfigurationDepthRangeEXT depthRangeExt {XR_TYPE_VIEW_CONFIGURATION_DEPTH_RANGE_EXT};
    if (OpenXRExtensions::IsExtensionSupported(XR_EXT_VIEW_CONFIGURATION_DEPTH_RANGE_EXTENSION_NAME)) {
        viewConfig.resize(viewCount, {XR_TYPE_VIEW_CONFIGURATION_VIEW, &depthRangeExt});
    } else {
        viewConfig.resize(viewCount, {XR_TYPE_VIEW_CONFIGURATION_VIEW});
    }
    CHECK_XRCMD(xrEnumerateViewConfigurationViews(instance, system, viewConfigType, viewCount, &viewCount, viewConfig.data()));
    if (OpenXRExtensions::IsExtensionSupported(XR_EXT_VIEW_CONFIGURATION_DEPTH_RANGE_EXTENSION_NAME)) {
      nearMin = depthRangeExt.minNearZ;
      farMax = depthRangeExt.maxFarZ;
      SetClipPlanes(depthRangeExt.recommendedNearZ, depthRangeExt.recommendedFarZ);
      VRB_LOG("OpenXR view configuration depth range: minNearZ=%f maxFarZ=%f recommendedNearZ=%f recommendedFarZ=%f",
              depthRangeExt.minNearZ, depthRangeExt.maxFarZ, depthRangeExt.recommendedNearZ, depthRangeExt.recommendedFarZ);
    }
    // Cache view buffer (used in xrLocateViews)
    views.resize(viewCount, {XR_TYPE_VIEW});
#if defined(VIVEXR)
    runtimeTargetViews.resize(viewCount, {XR_TYPE_VIEW});
#endif

    vrb::RenderContextPtr render = context.lock();

    // Create the main swapChain for each eye view
    for (uint32_t i = 0; i < viewCount; i++) {
      auto swapChain = OpenXRSwapChain::create();
      XrSwapchainCreateInfo info = GetSwapChainCreateInfo();
      swapChain->InitFBO(render, session, info, GetFBOAttributes());
      eyeSwapChains.push_back(swapChain);
    }
    VRB_DEBUG("OpenXR available views: %d", (int)eyeSwapChains.size());
  }

  void SetClipPlanes(float aNear, float aFar) {
      near = std::max(nearMin, aNear);
      far = std::min(aFar, farMax);
  }

  void InitializeBlendModes() {
      CHECK(instance != XR_NULL_HANDLE);
      CHECK(system != 0);

      uint32_t count;
      XrViewConfigurationType viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
      CHECK_XRCMD(xrEnumerateEnvironmentBlendModes(instance, system, viewConfigurationType, 0, &count, nullptr));
      CHECK(count > 0);

      blendModes.resize(count);
      CHECK_XRCMD(xrEnumerateEnvironmentBlendModes(instance, system, viewConfigurationType, count, &count, blendModes.data()));
      VRB_LOG("OpenXR: %d supported blend mode%c", count, count > 1 ? 's' : ' ');
      for (const auto& blendMode : blendModes)
          VRB_LOG("\t%s", to_string(blendMode));

      auto blendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
      const auto supportsOpaqueBlendMode = std::find(blendModes.begin(), blendModes.end(), blendMode) != blendModes.end();
      blendMode = XR_ENVIRONMENT_BLEND_MODE_ADDITIVE;
      const auto supportsAdditiveBlendMode = std::find(blendModes.begin(), blendModes.end(), blendMode) != blendModes.end();
      blendMode = XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND;
      const auto supportsAlphaBlendMode = std::find(blendModes.begin(), blendModes.end(), blendMode) != blendModes.end();
      CHECK(supportsOpaqueBlendMode || supportsAdditiveBlendMode || supportsAlphaBlendMode);

      passthroughBlendMode = supportsAdditiveBlendMode ? XR_ENVIRONMENT_BLEND_MODE_ADDITIVE : (supportsAlphaBlendMode ? XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND : XR_ENVIRONMENT_BLEND_MODE_OPAQUE);
      defaultBlendMode = supportsOpaqueBlendMode ? XR_ENVIRONMENT_BLEND_MODE_OPAQUE : passthroughBlendMode;
      VRB_LOG("OpenXR: selected default blend mode %s", to_string(defaultBlendMode));
      VRB_LOG("OpenXR: selected passthrough blend mode %s", to_string(passthroughBlendMode));
  }

  void InitializeImmersiveDisplay() {
    CHECK(immersiveDisplay);
    CHECK(viewConfig.size() > 0);

    immersiveDisplay->SetDeviceName(systemProperties.systemName);
#if defined(VIVEXR)
    // Gecko rendering at the full 1600x1600 recommendation per eye misses the
    // 90 Hz GPU budget on XR Elite. Render at 80% in each dimension, then let
    // the OpenXR compositor upscale into the native eye swapchains.
    constexpr uint32_t kViveWebXRResolutionNumerator = 4;
    constexpr uint32_t kViveWebXRResolutionDenominator = 5;
    const uint32_t eyeWidth = viewConfig.front().recommendedImageRectWidth *
                              kViveWebXRResolutionNumerator / kViveWebXRResolutionDenominator;
    const uint32_t eyeHeight = viewConfig.front().recommendedImageRectHeight *
                               kViveWebXRResolutionNumerator / kViveWebXRResolutionDenominator;
    immersiveDisplay->SetEyeResolution(eyeWidth, eyeHeight);
    VRB_LOG("VIVE XR Elite: WebXR eye resolution %ux%u (80%% linear scale)", eyeWidth, eyeHeight);
#else
    immersiveDisplay->SetEyeResolution(viewConfig.front().recommendedImageRectWidth, viewConfig.front().recommendedImageRectHeight);
#endif
    immersiveDisplay->SetSittingToStandingTransform(vrb::Matrix::Translation(kAverageHeight));
    auto toDeviceBlendModes = [](std::vector<XrEnvironmentBlendMode> aOpenXRBlendModes) {
        std::vector<device::BlendMode> deviceBlendModes;
        for (const auto& blendMode : aOpenXRBlendModes) {
            switch (blendMode) {
            case XR_ENVIRONMENT_BLEND_MODE_OPAQUE:
                deviceBlendModes.push_back(device::BlendMode::Opaque);
                break;
            case XR_ENVIRONMENT_BLEND_MODE_ADDITIVE:
                deviceBlendModes.push_back(device::BlendMode::Additive);
                break;
            case XR_ENVIRONMENT_BLEND_MODE_ALPHA_BLEND:
                deviceBlendModes.push_back(device::BlendMode::AlphaBlend);
                break;
            default:
                VRB_ERROR("Unsupported blend mode %d", blendMode);
                break;
            }
        }
        return deviceBlendModes;
    };
    immersiveDisplay->SetBlendModes(toDeviceBlendModes(blendModes));
    immersiveDisplay->CompleteEnumeration();
  }

  void InitializeRefreshRates() {
    CHECK(session);
    if (!OpenXRExtensions::IsExtensionSupported(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME))
      return;

    uint32_t displayRefreshRateCount = 0;
    CHECK_XRCMD(OpenXRExtensions::sXrEnumerateDisplayRefreshRatesFB(session, 0, &displayRefreshRateCount, nullptr));
    if (displayRefreshRateCount == 0) {
        VRB_ERROR("OpenXR runtime reports 0 display refresh rates but XR_FB_display_refresh_rate is supported");
        return;
    }

    refreshRates.resize(displayRefreshRateCount);
    CHECK_XRCMD(OpenXRExtensions::sXrEnumerateDisplayRefreshRatesFB(session, displayRefreshRateCount, &displayRefreshRateCount, refreshRates.data()));

    {
      std::stringstream ratesStream;
      for (auto rate : refreshRates)
          ratesStream << rate << ", ";
      std::string result = ratesStream.str().substr(0, ratesStream.str().size() - 2);
      VRB_DEBUG("OpenXR device supports %u refresh rates: %s", displayRefreshRateCount, result.c_str());
    }
  }

  XrSwapchainCreateInfo GetSwapChainCreateInfo(uint32_t w = 0, uint32_t h = 0) {
#if OCULUSVR || PFDMXR
    const int64_t colorFormat = GL_SRGB8_ALPHA8;
#else
    const int64_t colorFormat = GL_RGBA8;
#endif
    CHECK_MSG(SupportsColorFormat(colorFormat), "Runtime doesn't support selected swapChain color format");

    CHECK(viewConfig.size() > 0);

    if (w == 0 || h == 0) {
      w = viewConfig.front().recommendedImageRectWidth;
      h = viewConfig.front().recommendedImageRectHeight;
    }

    XrSwapchainCreateInfo info{XR_TYPE_SWAPCHAIN_CREATE_INFO};
    info.arraySize = 1;
    info.format = colorFormat;
    info.width = w;
    info.height = h;
    info.mipCount = 1;
    info.faceCount = 1;
    info.arraySize = 1;
    info.sampleCount = viewConfig.front().recommendedSwapchainSampleCount;
    info.usageFlags = XR_SWAPCHAIN_USAGE_SAMPLED_BIT | XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT;
    return info;
  }

  vrb::FBO::Attributes GetFBOAttributes() const {
    vrb::FBO::Attributes attributes;
    if (renderMode == device::RenderMode::StandAlone) {
      attributes.depth = true;
      attributes.samples = 4;
    } else {
      attributes.depth = false;
      attributes.samples = 0;
    }
    return attributes;
  }

  void UpdateSpaces() {
    CHECK(session != XR_NULL_HANDLE);

    // Query supported reference spaces
    uint32_t spaceCount = 0;
    CHECK_XRCMD(xrEnumerateReferenceSpaces(session, 0, &spaceCount, nullptr));
    std::vector<XrReferenceSpaceType> spaces(spaceCount);
    CHECK_XRCMD(xrEnumerateReferenceSpaces(session, spaceCount, &spaceCount, spaces.data()));
    VRB_DEBUG("OpenXR Available reference spaces: %d", spaceCount);
    for (XrReferenceSpaceType space : spaces) {
      VRB_DEBUG("  OpenXR Space Name: %s", to_string(space));
    }

    auto supportsSpace = [&](XrReferenceSpaceType aType) -> bool {
      return std::find(spaces.begin(), spaces.end(), aType) != spaces.end();
    };

    // Initialize view spaces used by default
    if (viewSpace == XR_NULL_HANDLE) {
      CHECK_MSG(supportsSpace(XR_REFERENCE_SPACE_TYPE_VIEW), "XR_REFERENCE_SPACE_TYPE_VIEW not supported");
      XrReferenceSpaceCreateInfo create{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
      create.poseInReferenceSpace = XrPoseIdentity();
      create.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
      CHECK_XRCMD(xrCreateReferenceSpace(session, &create, &viewSpace));
    }

    if (localSpace == XR_NULL_HANDLE) {
      CHECK_MSG(supportsSpace(XR_REFERENCE_SPACE_TYPE_LOCAL), "XR_REFERENCE_SPACE_TYPE_LOCAL not supported");
      XrReferenceSpaceCreateInfo create{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
      create.poseInReferenceSpace = XrPoseIdentity();
      create.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
      CHECK_XRCMD(xrCreateReferenceSpace(session, &create, &localSpace));
    }

    if (layersSpace == XR_NULL_HANDLE) {
      CHECK_MSG(supportsSpace(XR_REFERENCE_SPACE_TYPE_LOCAL), "XR_REFERENCE_SPACE_TYPE_LOCAL not supported");
      XrReferenceSpaceCreateInfo create{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
      create.poseInReferenceSpace  = XrPoseIdentity();
      create.poseInReferenceSpace.position = {
        -kAverageHeight.x(), -kAverageHeight.y(), -kAverageHeight.z()
      };
      create.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_LOCAL;
      CHECK_XRCMD(xrCreateReferenceSpace(session, &create, &layersSpace));
    }

    // Optionally create a stageSpace to be used in WebXR room scale apps.
    if (stageSpace == XR_NULL_HANDLE && supportsSpace(XR_REFERENCE_SPACE_TYPE_STAGE)) {
      XrReferenceSpaceCreateInfo create{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
      create.poseInReferenceSpace = XrPoseIdentity();
      create.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_STAGE;
      CHECK_XRCMD(xrCreateReferenceSpace(session, &create, &stageSpace));
    }
  }

  void AddUILayer(const OpenXRLayerPtr& aLayer, VRLayerSurface::SurfaceType aSurfaceType) {
    if (session != XR_NULL_HANDLE) {
      vrb::RenderContextPtr ctx = context.lock();
      aLayer->Init(javaContext->env, session, ctx);
    }
    uiLayers.push_back(aLayer);
    if (aSurfaceType == VRLayerSurface::SurfaceType::FBO) {
      aLayer->SetBindDelegate([=](const OpenXRSwapChainPtr& aSwapchain, GLenum  aTarget, bool bound){
        if (aSwapchain) {
          HandleQuadLayerBind(aSwapchain, aTarget, bound);
        }
      });
      if (boundSwapChain) {
        boundSwapChain->BindFBO();
      }
    }
  }

  void HandleQuadLayerBind(const OpenXRSwapChainPtr& aSwapchain, GLenum  aTarget, bool bound) {
    if (!bound) {
      if (boundSwapChain && boundSwapChain == aSwapchain) {
        boundSwapChain->ReleaseImage();
        boundSwapChain = nullptr;
      }
      if (previousBoundSwapchain) {
        previousBoundSwapchain->BindFBO();
        boundSwapChain = previousBoundSwapchain;
        previousBoundSwapchain = nullptr;
      }
      return;
    }

    if (boundSwapChain == aSwapchain) {
      // Layer already bound
      return;
    }

    previousBoundSwapchain = boundSwapChain;
    boundSwapChain = aSwapchain;
    boundSwapChain->AcquireImage();
    boundSwapChain->BindFBO(aTarget);
  }

  const XrEventDataBaseHeader* PollEvent() {
    if (!instance) {
      return nullptr;
    }
    XrEventDataBaseHeader* baseHeader = reinterpret_cast<XrEventDataBaseHeader*>(&eventBuffer);
    *baseHeader = {XR_TYPE_EVENT_DATA_BUFFER};
    const XrResult xr = xrPollEvent(instance, &eventBuffer);
    if (xr == XR_SUCCESS) {
      return baseHeader;
    }

    CHECK_MSG(xr == XR_EVENT_UNAVAILABLE, "Expected XR_EVENT_UNAVAILABLE result")
    return nullptr;
  }

  void BeginXRSession() {
      XrSessionBeginInfo sessionBeginInfo{XR_TYPE_SESSION_BEGIN_INFO};
      sessionBeginInfo.primaryViewConfigurationType = viewConfigType;
#if defined(VIVEXR)
      XrFrameSynchronizationSessionBeginInfoHTC frameSynchronizationInfo {
          kViveFrameSynchronizationSessionBeginInfoType,
          nullptr,
          XR_FRAME_SYNCHRONIZATION_MODE_PROMPT_HTC
      };
      if (OpenXRExtensions::IsExtensionSupported(kViveFrameSynchronizationExtensionName)) {
        sessionBeginInfo.next = &frameSynchronizationInfo;
        VRB_LOG("VIVE XR Elite: prompt frame synchronization enabled");
      }
#endif
      CHECK_XRCMD(xrBeginSession(session, &sessionBeginInfo));
      vrReady = true;
  }

  void HandleSessionEvent(const XrEventDataSessionStateChanged& event) {
    VRB_LOG("OpenXR XrEventDataSessionStateChanged: state %s->%s session=%p time=%ld",
        to_string(sessionState), to_string(event.state), event.session, event.time);
    auto previousSessionState = std::exchange(sessionState, event.state);

    if (event.session != XR_NULL_HANDLE && session != XR_NULL_HANDLE) {
      CHECK(session == event.session);
    }

    switch (sessionState) {
      case XR_SESSION_STATE_READY: {
        VRB_LOG("XR_SESSION_STATE_READY");
        BeginXRSession();
        break;
      }
      case XR_SESSION_STATE_VISIBLE: {
        VRB_LOG("XR_SESSION_STATE_VISIBLE");
        if (previousSessionState == XR_SESSION_STATE_FOCUSED)
            VRBrowser::OnAppFocusChanged(false);
        break;
      }
      case XR_SESSION_STATE_FOCUSED: {
        VRB_LOG("XR_SESSION_STATE_FOCUSED");
        CHECK(previousSessionState == XR_SESSION_STATE_VISIBLE);
          VRBrowser::OnAppFocusChanged(true);
        break;
      }
      case XR_SESSION_STATE_SYNCHRONIZED: {
        VRB_LOG("XR_SESSION_STATE_SYNCHRONIZED");
        break;
      }
      case XR_SESSION_STATE_STOPPING: {
        VRB_LOG("XR_SESSION_STATE_STOPPING");
        vrReady = false;
        CHECK_XRCMD(xrEndSession(session))
        break;
      }
      case XR_SESSION_STATE_EXITING: {
        VRB_LOG("XR_SESSION_STATE_EXITING");
        vrReady = false;
        break;
      }
      case XR_SESSION_STATE_LOSS_PENDING: {
        VRB_LOG("XR_SESSION_STATE_LOSS_PENDING");
        vrReady = false;
        break;
      }
      default:
        break;
    }
  }

  void UpdateClockLevels() {
      if (!OpenXRExtensions::IsExtensionSupported(XR_EXT_PERFORMANCE_SETTINGS_EXTENSION_NAME))
          return;

#if defined(VIVEXR)
      VRB_LOG("VIVE XR Elite: CPU/GPU performance set to sustained high");
      CHECK_XRCMD(OpenXRExtensions::sXrPerfSettingsSetPerformanceLevelEXT(session, XR_PERF_SETTINGS_DOMAIN_CPU_EXT, XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT));
      CHECK_XRCMD(OpenXRExtensions::sXrPerfSettingsSetPerformanceLevelEXT(session, XR_PERF_SETTINGS_DOMAIN_GPU_EXT, XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT));
      return;
#endif

      if (renderMode == device::RenderMode::StandAlone && minCPULevel == device::CPULevel::Normal) {
          CHECK_XRCMD(OpenXRExtensions::sXrPerfSettingsSetPerformanceLevelEXT(session, XR_PERF_SETTINGS_DOMAIN_CPU_EXT, XR_PERF_SETTINGS_LEVEL_SUSTAINED_LOW_EXT));
          CHECK_XRCMD(OpenXRExtensions::sXrPerfSettingsSetPerformanceLevelEXT(session, XR_PERF_SETTINGS_DOMAIN_GPU_EXT, XR_PERF_SETTINGS_LEVEL_SUSTAINED_LOW_EXT));
      } else {
          CHECK_XRCMD(OpenXRExtensions::sXrPerfSettingsSetPerformanceLevelEXT(session, XR_PERF_SETTINGS_DOMAIN_CPU_EXT, XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT));
          CHECK_XRCMD(OpenXRExtensions::sXrPerfSettingsSetPerformanceLevelEXT(session, XR_PERF_SETTINGS_DOMAIN_GPU_EXT, XR_PERF_SETTINGS_LEVEL_SUSTAINED_HIGH_EXT));
      }
  }

  void UpdateDisplayRefreshRate() {
    if (!OpenXRExtensions::IsExtensionSupported(XR_FB_DISPLAY_REFRESH_RATE_EXTENSION_NAME) || refreshRates.empty())
      return;

    float suggestedRefreshRate = 0.0;
    switch (deviceType) {
      case device::MetaQuest3:
      case device::OculusQuest2:
      case device::MetaQuestPro:
      // Pico4x default is 72hz, but has an experimental setting to set it to 90hz. If the setting
      // is disabled we'll select 72hz which is the only one advertised by OpenXR in that case.
      case device::Pico4x:
      case device::Pico4U:
      case device::PfdmYVR1:
      case device::PfdmYVR2:
      case device::PfdmMR:
        suggestedRefreshRate = 90.0;
        break;
      case device::ViveXRElite:
        // Keep the OpenXR compositor at the XR Elite's 90 Hz rate in both
        // modes. Immersive WebXR content may render more slowly, but Wolvic
        // now resubmits the last completed image every display interval so
        // head-pose reprojection remains smooth independently of page FPS.
        suggestedRefreshRate = 90.0;
        VRB_LOG("VIVE XR Elite: selecting %.0f Hz for %s mode", suggestedRefreshRate,
                renderMode == device::RenderMode::Immersive ? "immersive" : "standalone");
        break;
      case device::OculusQuest:
        suggestedRefreshRate = 72.0;
        break;
      default:
        suggestedRefreshRate = 60.0;
        break;
    }

    // Selects the first available refresh rate equal or higher than the desired one. Note that
    // OpenXR returns the refresh rates sorted from lowest to highest.
    auto selectValidRefreshRate = [refreshRates = this->refreshRates](const float suggestedRefreshRate) {
      auto it = std::find_if(refreshRates.begin(), refreshRates.end(),
   [&suggestedRefreshRate](const float& refreshRate) { return refreshRate >= suggestedRefreshRate;});
      if (it == refreshRates.end())
        return refreshRates.back();
      return *it;
    };

    float selectedRefreshRate = selectValidRefreshRate(suggestedRefreshRate);
    VRB_DEBUG("OpenXR setting refresh rate to %.0fhz", selectedRefreshRate);
    CHECK_XRCMD(OpenXRExtensions::sXrRequestDisplayRefreshRateFB(session, selectedRefreshRate));
  }

  void Shutdown() {
    // Release swapChains before destroying the instance. Note that layers are backed by swapchains.
    eyeSwapChains.clear();
    uiLayers.clear();
    cubeLayer.reset();
    equirectLayer.reset();
    passthroughLayer.reset();

    // Release spaces
    if (viewSpace != XR_NULL_HANDLE) {
      CHECK_XRCMD(xrDestroySpace(viewSpace));
      viewSpace = XR_NULL_HANDLE;
    }

    if (localSpace != XR_NULL_HANDLE) {
      CHECK_XRCMD(xrDestroySpace(localSpace));
      localSpace = XR_NULL_HANDLE;
    }

    if (layersSpace != XR_NULL_HANDLE) {
      CHECK_XRCMD(xrDestroySpace(layersSpace));
      layersSpace = XR_NULL_HANDLE;
    }

    if (stageSpace != XR_NULL_HANDLE) {
      CHECK_XRCMD(xrDestroySpace(stageSpace));
      stageSpace = XR_NULL_HANDLE;
    }

    // Release input
    input = nullptr;

    if (passthroughStrategy) {
        // Explicitly destroy it before releasing session and instance as the strategy might
        // have derived object handles (like the FB extension one)
        passthroughStrategy = nullptr;
    }

    if (session) {
      CHECK_XRCMD(xrDestroySession(session));
      session = XR_NULL_HANDLE;
    }

    // Shutdown OpenXR instance
    if (instance) {
      CHECK_XRCMD(xrDestroyInstance(instance));
      instance = XR_NULL_HANDLE;
    }

    // Hand mesh properties
    if (handMeshProperties)
        handMeshProperties.reset();

    // TODO: Check if activity globarRef needs to be released
  }

  void MaybeNotifyControllersReady() {
    ASSERT(input);
    if (!controllersReadyCallback || !input->AreControllersReady())
        return;
    // Invoke the callback. We only clear it if the load has started. Otherwise we will retry later.
    // This happens for example if a profile with no 3D models like hand interaction is loaded on
    // start. If we clear the callback it won't ever be called in case the user grabs a controller.
    if (controllersReadyCallback()) {
        if (input->HasPhysicalControllersAvailable()) {
            VRB_LOG("DeviceDelegateOpenXR -- Physical controllers available");
            VRBrowser::OnControllersAvailable();
        }
        controllersReadyCallback = nullptr;
    }
  }

  void UpdateInteractionProfile() {
      if (!input || !controller)
          return;

      input->UpdateInteractionProfile(*controller);
      MaybeNotifyControllersReady();
  }

  bool IsPassthroughLayerReady() {
    if (!passthroughStrategy->isReady() || passthroughLayer == nullptr || !passthroughLayer->IsValid())
      return false;

    return true;
  }
};

DeviceDelegateOpenXRPtr
DeviceDelegateOpenXR::Create(vrb::RenderContextPtr& aContext, JavaContext* aJavaContext) {
  DeviceDelegateOpenXRPtr result = std::make_shared<vrb::ConcreteClass<DeviceDelegateOpenXR, DeviceDelegateOpenXR::State> >();
  result->m.context = aContext;
  result->m.javaContext = aJavaContext;
  result->m.Initialize();
  return result;
}

device::DeviceType
DeviceDelegateOpenXR::GetDeviceType() {
  return m.deviceType;
}

void
DeviceDelegateOpenXR::SetRenderMode(const device::RenderMode aMode) {
  if (aMode == m.renderMode) {
    return;
  }
  m.renderMode = aMode;
#if defined(VIVEXR)
  m.immersiveFramePoseHistory.clear();
  m.selectedImmersiveFrameViews.clear();
  m.selectedImmersiveFrameId = 0;
  m.selectedImmersiveFramePoseTime = 0;
  m.latestImmersiveInputFrameId = 0;
  m.poseMatchDiagnosticCount = 0;
  m.poseMatchLagSum = 0;
  m.poseMatchLagMax = 0;
  m.poseMatchMissCount = 0;
  m.poseMatchAgeSum = 0;
  m.poseMatchAgeMax = 0;
  m.runtimeTargetViewsValid = false;
  m.latePoseSampleCount = 0;
  m.latePoseInvalidCount = 0;
  m.latePoseRotationSum = 0.0;
  m.latePoseRotationMax = 0.0;
  m.latePosePositionSum = 0.0;
  m.latePosePositionMax = 0.0;
  m.lastSubmittedDisplayTime = 0;
  m.submittedTimeDiagnosticCount = 0;
  m.submittedTimeNonMonotonicCount = 0;
  m.submittedTimeIrregularCount = 0;
  m.submittedTimeStepSum = 0;
  m.submittedTimeStepMin = 0;
  m.submittedTimeStepMax = 0;
  m.hasLastStageTransformPose = false;
  m.stageTransformDiagnosticCount = 0;
  m.stageTransformDriftSum = 0.0;
  m.stageTransformDriftMax = 0.0;
#endif
  vrb::RenderContextPtr render = m.context.lock();
  for (OpenXRSwapChainPtr& eyeSwapchain: m.eyeSwapChains) {
    XrSwapchainCreateInfo info = m.GetSwapChainCreateInfo();
    eyeSwapchain->InitFBO(render, m.session, info, m.GetFBOAttributes());
  }

  m.UpdateClockLevels();
  m.UpdateDisplayRefreshRate();
  UpdatePassthrough();

  // Reset reorient when exiting or entering immersive
  m.reorientMatrix = vrb::Matrix::Identity();
}

device::RenderMode
DeviceDelegateOpenXR::GetRenderMode() {
  return m.renderMode;
}

void
DeviceDelegateOpenXR::RegisterImmersiveDisplay(ImmersiveDisplayPtr aDisplay) {
  m.immersiveDisplay = std::move(aDisplay);
}

void
DeviceDelegateOpenXR::SetImmersiveSize(const uint32_t aEyeWidth, const uint32_t aEyeHeight) {

}

vrb::CameraPtr
DeviceDelegateOpenXR::GetCamera(const device::Eye aWhich) {
  const int32_t index = device::EyeIndex(aWhich);
  if (index < 0) { return nullptr; }
  return m.cameras[index];
}

const vrb::Matrix&
DeviceDelegateOpenXR::GetHeadTransform() const {
  return m.cameras[0]->GetHeadTransform();
}

const vrb::Matrix&
DeviceDelegateOpenXR::GetReorientTransform() const {
  return m.reorientMatrix;
}

void
DeviceDelegateOpenXR::SetReorientTransform(const vrb::Matrix& aMatrix) {
  m.reorientMatrix = aMatrix;
}

void
DeviceDelegateOpenXR::Reorient(const vrb::Matrix& transform, ReorientMode mode) {
  switch (mode) {
    case ReorientMode::SIX_DOF:
      m.reorientMatrix = DeviceUtils::CalculateReorientationMatrixOnHeadLock(transform, GetHeadTransform().GetTranslation());
      break;
    case ReorientMode::NO_ROLL:
      m.reorientMatrix = DeviceUtils::CalculateReorientationMatrixWithoutRoll(transform, GetHeadTransform().GetTranslation());
      break;
    default:
      VRB_ERROR("Unsupported reorient mode %d", mode);
  }
}

void
DeviceDelegateOpenXR::SetClearColor(const vrb::Color& aColor) {
  m.clearColor = aColor;
}

void
DeviceDelegateOpenXR::SetClipPlanes(const float aNear, const float aFar) {
  m.SetClipPlanes(aNear, aFar);
}

void
DeviceDelegateOpenXR::SetControllerDelegate(ControllerDelegatePtr& aController) {
  m.controller = aController;
}

void
DeviceDelegateOpenXR::ReleaseControllerDelegate() {
  m.controller = nullptr;
}

int32_t
DeviceDelegateOpenXR::GetControllerModelCount() const {
  return m.input->GetControllerModelCount();
}

const std::string
DeviceDelegateOpenXR::GetControllerModelName(const int32_t aModelIndex) const {
  return m.input->GetControllerModelName(aModelIndex);
}

bool
DeviceDelegateOpenXR::IsPositionTrackingSupported() const {
  // returns true for 6DoF controllers
  return m.IsPositionTrackingSupported();
}

void DeviceDelegateOpenXR::OnControllersCreated(std::function<void()> callback) {
  if (m.input) {
      callback();
      return;
  }
  m.controllersCreatedCallback = std::move(callback);
}

void
DeviceDelegateOpenXR::OnControllersReady(const ControllersReadyCallback& callback) {
  if (m.input && m.input->AreControllersReady()) {
    callback();
    return;
  }
  m.controllersReadyCallback = callback;
}

void
DeviceDelegateOpenXR::SetCPULevel(const device::CPULevel aLevel) {
  m.minCPULevel = aLevel;
  m.UpdateClockLevels();
};

void
DeviceDelegateOpenXR::ProcessEvents() {
  while (const XrEventDataBaseHeader* ev = m.PollEvent()) {
    switch (ev->type) {
      case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
        const auto& event = *reinterpret_cast<const XrEventDataSessionStateChanged*>(ev);
        m.HandleSessionEvent(event);
        break;
      }
      case XR_TYPE_EVENT_DATA_EVENTS_LOST: {
        const auto& event = *reinterpret_cast<const XrEventDataEventsLost*>(ev);
        VRB_WARN("OpenXR %d events lost", event.lostEventCount);
        break;
      }
      case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING: {
        // Receiving the XrEventDataInstanceLossPending event structure indicates that the application
        // is about to lose the indicated XrInstance at the indicated lossTime in the future.
        const auto& event = *reinterpret_cast<const XrEventDataInstanceLossPending*>(ev);
        VRB_WARN("OpenXR XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING by %ld", event.lossTime);
        m.vrReady = false;
        return;
      }
      case XR_TYPE_EVENT_DATA_INTERACTION_PROFILE_CHANGED: {
        m.UpdateInteractionProfile();
        break;
      }
      case XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING: {
        const auto& event =
            *reinterpret_cast<const XrEventDataReferenceSpaceChangePending*>(ev);
        m.firstPose = std::nullopt;
        m.reorientRequested = true;
        m.reorientChangeTime = event.changeTime;
        VRB_LOG("OpenXR: reference space %d change pending at %lld; user recentered the view?",
                event.referenceSpaceType,
                static_cast<long long>(event.changeTime));
        break;
      }
      case XR_TYPE_EVENT_DATA_PASSTHROUGH_STATE_CHANGED_FB: {
        auto result = m.passthroughStrategy->handleEvent(*ev);
        if (result == OpenXRPassthroughStrategy::HandleEventResult::NonRecoverableError) {
            if (m.passthroughLayer->IsValid())
                m.passthroughLayer = nullptr;
        } else if (result == OpenXRPassthroughStrategy::HandleEventResult::NeedsReinit) {
            // TODO: return a bool with the initialization result?
            m.passthroughStrategy->initializePassthrough(m.session);
            m.passthroughLayer->Init(m.session);
        }
        break;
      }
      case XR_TYPE_EVENT_DATA_MAIN_SESSION_VISIBILITY_CHANGED_EXTX: {
        assert(OpenXRExtensions::IsExtensionSupported(XR_EXTX_OVERLAY_EXTENSION_NAME));
        const auto &event = *reinterpret_cast<const XrEventDataMainSessionVisibilityChangedEXTX *>(ev);
        VRB_LOG("OpenXR main session is now %s", event.visible == XR_TRUE ? "visible" : "not visible");
        break;
      }
      default: {
        VRB_DEBUG("OpenXR ignoring event type %d", ev->type);
        break;
      }
    }
  }
}

bool
DeviceDelegateOpenXR::SupportsFramePrediction(FramePrediction aPrediction) const {
  return true;
}

void
DeviceDelegateOpenXR::StartFrame(const FramePrediction aPrediction) {
  mShouldRender = false;
  if (!m.vrReady) {
    VRB_ERROR("OpenXR StartFrame called while not in VR mode");
    return;
  }

#if OCULUSVR || PICOXR || PFDMXR
  // Fix brigthness issue.
  glDisable(GL_FRAMEBUFFER_SRGB_EXT);
#endif

  CHECK(m.session != XR_NULL_HANDLE);
  CHECK(m.viewSpace != XR_NULL_HANDLE);

  // Throttle the application frame loop in order to synchronize
  // application frame submissions with the display.
  XrFrameWaitInfo frameWaitInfo{XR_TYPE_FRAME_WAIT_INFO};
  XrFrameState frameState{XR_TYPE_FRAME_STATE};
  CHECK_XRCMD(xrWaitFrame(m.session, &frameWaitInfo, &frameState));
  m.runtimePredictedDisplayTime = frameState.predictedDisplayTime;
  m.predictedDisplayPeriod = frameState.predictedDisplayPeriod;

  // Begin frame and select the predicted display time
  XrFrameBeginInfo frameBeginInfo{XR_TYPE_FRAME_BEGIN_INFO};
  CHECK_XRCMD(xrBeginFrame(m.session, &frameBeginInfo));

  m.framePrediction = aPrediction;
  if (aPrediction == FramePrediction::ONE_FRAME_AHEAD) {
    m.prevPredictedDisplayTime = m.predictedDisplayTime;
    m.prevPredictedPose = m.predictedPose;
    m.prevViews = m.views;
    m.predictedDisplayTime = frameState.predictedDisplayTime + frameState.predictedDisplayPeriod;
#if defined(VIVEXR)
    if (m.prevPredictedDisplayTime != 0) {
      const XrDuration delta = m.prevPredictedDisplayTime - frameState.predictedDisplayTime;
      if (m.frameTimingDiagnosticCount == 0) {
        m.frameTimingDeltaMin = delta;
        m.frameTimingDeltaMax = delta;
      } else {
        m.frameTimingDeltaMin = std::min(m.frameTimingDeltaMin, delta);
        m.frameTimingDeltaMax = std::max(m.frameTimingDeltaMax, delta);
      }
      m.frameTimingDeltaSum += delta;
      if (std::llabs(delta) > frameState.predictedDisplayPeriod / 2) {
        ++m.missedFrameTimingCount;
      }
      if (++m.frameTimingDiagnosticCount == 90) {
        const double periodMs = static_cast<double>(frameState.predictedDisplayPeriod) / 1000000.0;
        VRB_LOG("VIVE XR frame timing: avg=%+.3fms min=%+.3fms max=%+.3fms missed=%u/90 period=%.3fms",
                static_cast<double>(m.frameTimingDeltaSum) / 90000000.0,
                static_cast<double>(m.frameTimingDeltaMin) / 1000000.0,
                static_cast<double>(m.frameTimingDeltaMax) / 1000000.0,
                m.missedFrameTimingCount, periodMs);
        m.frameTimingDiagnosticCount = 0;
        m.frameTimingDeltaSum = 0;
        m.missedFrameTimingCount = 0;
      }
    }
#endif
  } else {
    m.predictedDisplayTime = frameState.predictedDisplayTime;
  }

  mShouldRender = frameState.shouldRender;
  if (!frameState.shouldRender)
    return;

#if defined(VIVEXR)
  // Re-locate the views for the runtime's actual target time. Comparing this
  // late sample with the views predicted one frame earlier tells us how much
  // correction VIVE's asynchronous timewarp needs to apply during head turns.
  m.runtimeTargetViewsValid = false;
  if (m.renderMode == device::RenderMode::Immersive &&
      aPrediction == FramePrediction::ONE_FRAME_AHEAD &&
      !m.runtimeTargetViews.empty()) {
    XrViewLocateInfo runtimeTargetLocateInfo{XR_TYPE_VIEW_LOCATE_INFO};
    runtimeTargetLocateInfo.viewConfigurationType = m.viewConfigType;
    runtimeTargetLocateInfo.displayTime = m.runtimePredictedDisplayTime;
    runtimeTargetLocateInfo.space = m.localSpace;
    XrViewState runtimeTargetViewState{XR_TYPE_VIEW_STATE};
    uint32_t runtimeTargetViewCount = 0;
    const XrResult locateResult = xrLocateViews(
        m.session, &runtimeTargetLocateInfo, &runtimeTargetViewState,
        static_cast<uint32_t>(m.runtimeTargetViews.size()),
        &runtimeTargetViewCount, m.runtimeTargetViews.data());
    if (XR_SUCCEEDED(locateResult) &&
        runtimeTargetViewCount == static_cast<uint32_t>(m.runtimeTargetViews.size()) &&
        (runtimeTargetViewState.viewStateFlags & XR_VIEW_STATE_ORIENTATION_VALID_BIT)) {
      m.runtimeTargetViewsValid = true;
    } else if (XR_FAILED(locateResult)) {
      MessageXrResult(locateResult);
    }
  }
#endif

  // Query head location
  XrSpaceLocation location {XR_TYPE_SPACE_LOCATION};
  CHECK_XRCMD(xrLocateSpace(m.viewSpace, m.localSpace, m.predictedDisplayTime, &location));
  m.predictedPose = location.pose;
  if (!m.firstPose && location.pose.position.y != 0.0) {
    m.firstPose = location.pose;
  }

  const vrb::Matrix trackingHead = XrPoseToMatrix(location.pose);
  vrb::Matrix head = trackingHead;
#if HVR
  if (IsPositionTrackingSupported()) {
    // Convert from floor to local (HVR doesn't support stageSpace yet)
    head.TranslateInPlace(vrb::Vector(-m.firstPose->position.x, -m.firstPose->position.y, -m.firstPose->position.z));
  }
#endif

  if (m.renderMode == device::RenderMode::StandAlone) {
    head.TranslateInPlace(kAverageHeight);
  }

  m.cameras[0]->SetHeadTransform(head);
  m.cameras[1]->SetHeadTransform(head);

  if (m.immersiveDisplay) {
    // Setup capability caps for this frame
    device::CapabilityFlags caps =
        device::Orientation | device::Present | device::InlineSession | device::ImmersiveVRSession;
    if (location.locationFlags & XR_SPACE_LOCATION_POSITION_VALID_BIT) {
      caps |= IsPositionTrackingSupported() ? device::Position : device::PositionEmulated;
    }

    // Update WebXR room scale transform if the device supports stage space
    if (m.stageSpace != XR_NULL_HANDLE) {
      caps |= device::StageParameters;

      // Compute the transform between local and stage space
      XrSpaceLocation stageLocation{XR_TYPE_SPACE_LOCATION};
      xrLocateSpace(m.localSpace, m.stageSpace, m.predictedDisplayTime, &stageLocation);
      vrb::Matrix transform = XrPoseToMatrix(stageLocation.pose);
      m.immersiveDisplay->SetSittingToStandingTransform(transform);
#if defined(VIVEXR)
      if (stageLocation.locationFlags & XR_SPACE_LOCATION_ORIENTATION_VALID_BIT) {
        const double rotationDelta = m.hasLastStageTransformPose
            ? QuaternionAngularDistanceDegrees(
                  m.lastStageTransformPose.orientation, stageLocation.pose.orientation)
            : 0.0;
        const double positionDelta = m.hasLastStageTransformPose
            ? PosePositionDistanceMillimeters(m.lastStageTransformPose, stageLocation.pose)
            : 0.0;

        // VIVE's system recenter changes LOCAL relative to STAGE, but its
        // OpenXR runtime does not always emit
        // XR_TYPE_EVENT_DATA_REFERENCE_SPACE_CHANGE_PENDING. Detect the
        // otherwise-fixed reference-space transform jumping so the standalone
        // browser is realigned with the user's new forward direction.
        if (m.renderMode == device::RenderMode::StandAlone &&
            m.hasLastStageTransformPose &&
            (rotationDelta > 0.5 || positionDelta > 10.0)) {
          m.firstPose = std::nullopt;
          m.reorientRequested = true;
          // The observed transform jump means the runtime has already applied
          // the change, so this fallback can be handled on the current frame.
          m.reorientChangeTime = 0;
          VRB_LOG("VIVE XR recenter fallback: local-to-stage changed by %.3fdeg, %.1fmm",
                  rotationDelta, positionDelta);
        }

        m.lastStageTransformPose = stageLocation.pose;
        m.hasLastStageTransformPose = true;

        if (m.renderMode == device::RenderMode::Immersive) {
          m.stageTransformDriftSum += rotationDelta;
          m.stageTransformDriftMax = std::max(m.stageTransformDriftMax, rotationDelta);
          if (++m.stageTransformDiagnosticCount == 90) {
            const XrQuaternionf& q = stageLocation.pose.orientation;
            VRB_LOG("VIVE XR floor space: avgDrift=%.5fdeg maxDrift=%.5fdeg poseY=%.4fm q=(%.5f,%.5f,%.5f,%.5f) flags=0x%llx",
                    m.stageTransformDriftSum / 90.0, m.stageTransformDriftMax,
                    stageLocation.pose.position.y, q.x, q.y, q.z, q.w,
                    static_cast<unsigned long long>(stageLocation.locationFlags));
            m.stageTransformDiagnosticCount = 0;
            m.stageTransformDriftSum = 0.0;
            m.stageTransformDriftMax = 0.0;
          }
        }
      }
#endif
#if HVR
      // Workaround for empty stage transform bug in HVR
      if (IsPositionTrackingSupported()) {
          m.immersiveDisplay->SetSittingToStandingTransform(
                  vrb::Matrix::Translation(vrb::Vector(0.0f, m.firstPose->position.y, 0.0f)));
      } else {
          m.immersiveDisplay->SetSittingToStandingTransform(
                  vrb::Matrix::Translation(kAverageHeight));
      }
#elif LYNX || SPACES
      // Stage space coordinates are wrong. As an example the returned Y value is 0.008.
      m.immersiveDisplay->SetSittingToStandingTransform(vrb::Matrix::Translation(kAverageHeight * 0.7));
#endif
    }

    m.immersiveDisplay->SetCapabilityFlags(caps);
  }

  // Query eyeTransform and perspective for each view
  XrViewState viewState{XR_TYPE_VIEW_STATE};
  uint32_t viewCapacityInput = (uint32_t) m.views.size();
  uint32_t viewCountOutput = 0;

  // Eye transform
  {
    XrViewLocateInfo viewLocateInfo{XR_TYPE_VIEW_LOCATE_INFO};
    viewLocateInfo.viewConfigurationType = m.viewConfigType;
    viewLocateInfo.displayTime = m.predictedDisplayTime;
#if defined(VIVEXR)
    // Keep the XR Elite standalone shell on its proven local-space path. For
    // immersive WebXR, request the eye poses relative to VIEW directly so
    // Gecko does not inherit the small prediction mismatch between a separate
    // xrLocateSpace(head) call and xrLocateViews(local).
    viewLocateInfo.space = m.renderMode == device::RenderMode::Immersive
                               ? m.viewSpace
                               : m.localSpace;
#else
    viewLocateInfo.space = m.viewSpace;
#endif
    CHECK_XRCMD(xrLocateViews(m.session, &viewLocateInfo, &viewState, viewCapacityInput, &viewCountOutput, m.views.data()));
    for (int i = 0; i < m.views.size(); ++i) {
      const XrView &view = m.views[i];
      const device::Eye eye = i == 0 ? device::Eye::Left : device::Eye::Right;
#if defined(VIVEXR)
      const vrb::Matrix eyeTransform =
          m.renderMode == device::RenderMode::Immersive
              ? XrPoseToMatrix(view.pose)
              : trackingHead.AfineInverse().PostMultiply(XrPoseToMatrix(view.pose));
#else
      vrb::Matrix eyeTransform = XrPoseToMatrix(view.pose);
#endif
      m.cameras[i]->SetEyeTransform(eyeTransform);
      if (m.immersiveDisplay) {
        m.immersiveDisplay->SetEyeTransform(eye, eyeTransform);
      }
    }
  }

  // Perspective
#if defined(VIVEXR)
  if (m.renderMode == device::RenderMode::Immersive) {
    XrViewLocateInfo viewLocateInfo{XR_TYPE_VIEW_LOCATE_INFO};
    viewLocateInfo.viewConfigurationType = m.viewConfigType;
    viewLocateInfo.displayTime = m.predictedDisplayTime;
    viewLocateInfo.space = m.localSpace;
    CHECK_XRCMD(xrLocateViews(m.session, &viewLocateInfo, &viewState, viewCapacityInput, &viewCountOutput, m.views.data()));
  }
#else
  XrViewLocateInfo viewLocateInfo{XR_TYPE_VIEW_LOCATE_INFO};
  viewLocateInfo.viewConfigurationType = m.viewConfigType;
  viewLocateInfo.displayTime = m.predictedDisplayTime;
  viewLocateInfo.space = m.localSpace;
  CHECK_XRCMD(xrLocateViews(m.session, &viewLocateInfo, &viewState, viewCapacityInput, &viewCountOutput, m.views.data()));
#endif

  for (int i = 0; i < m.views.size(); ++i) {
    const XrView& view = m.views[i];

    vrb::Matrix perspective = vrb::Matrix::PerspectiveMatrix(fabsf(view.fov.angleLeft), view.fov.angleRight,
                                                             view.fov.angleUp, fabsf(view.fov.angleDown), m.near, m.far);
    m.cameras[i]->SetPerspective(perspective);
    if (m.immersiveDisplay) {
      const device::Eye eye = i == 0 ? device::Eye::Left : device::Eye::Right;
      auto toDegrees = [](float angle) -> float {
        return angle * 180.0f / (float)M_PI;
      };
      m.immersiveDisplay->SetFieldOfView(eye, toDegrees(fabsf(view.fov.angleLeft)), toDegrees(view.fov.angleRight),
                                         toDegrees(view.fov.angleUp), toDegrees(fabsf(view.fov.angleDown)));
    }
  }

  // Update controllers
  if (m.input && m.controller) {
    vrb::Vector offsets = vrb::Vector::Zero();
#if defined(HVR)
    offsets.y() = -m.firstPose->position.y;
#endif
    m.input->Update(frameState, m.localSpace, head, offsets, m.renderMode, m.pointerMode, m.handTrackingEnabled, *m.controller);
  }

  if (m.reorientRequested &&
      m.renderMode == device::RenderMode::StandAlone &&
      (m.reorientChangeTime == 0 || m.predictedDisplayTime >= m.reorientChangeTime)) {
      if (mReorientClient)
          mReorientClient->OnReorient();
      m.reorientMatrix = DeviceUtils::CalculateReorientationMatrix(head, kAverageHeight);
      m.reorientRequested = false;
      m.reorientChangeTime = 0;
  }
}

void
DeviceDelegateOpenXR::RecordImmersiveFramePose(uint64_t aInputFrameId) {
#if defined(VIVEXR)
  if (aInputFrameId == 0 || m.views.empty()) {
    return;
  }

  ImmersiveFramePoseOpenXR framePose;
  framePose.inputFrameId = aInputFrameId;
  framePose.predictedDisplayTime = m.predictedDisplayTime;
  framePose.views = m.views;
  m.immersiveFramePoseHistory.push_back(std::move(framePose));
  m.latestImmersiveInputFrameId = aInputFrameId;
  // At 90 Hz this retains a little over one second of render-pose history.
  // Repeated OpenXR submissions must keep matching the source pose even when
  // Gecko stalls long enough for the previous 32-frame window to be evicted.
  while (m.immersiveFramePoseHistory.size() > 128) {
    m.immersiveFramePoseHistory.pop_front();
  }
#else
  (void)aInputFrameId;
#endif
}

void
DeviceDelegateOpenXR::SelectImmersiveFramePose(uint64_t aInputFrameId) {
#if defined(VIVEXR)
  // A Gecko timeout repeats the same released swapchain image. Keep its last
  // successfully matched source pose without searching or letting a long
  // stall evict it from the history; pairing an old image with the current
  // head pose would disable the runtime's corrective reprojection.
  if (aInputFrameId != 0 &&
      aInputFrameId == m.selectedImmersiveFrameId &&
      !m.selectedImmersiveFrameViews.empty()) {
    return;
  }

  m.selectedImmersiveFrameViews.clear();
  m.selectedImmersiveFrameId = 0;
  m.selectedImmersiveFramePoseTime = 0;
  if (aInputFrameId == 0) {
    return;
  }

  bool found = false;
  for (auto iter = m.immersiveFramePoseHistory.rbegin();
       iter != m.immersiveFramePoseHistory.rend(); ++iter) {
    if (iter->inputFrameId == aInputFrameId) {
      m.selectedImmersiveFrameViews = iter->views;
      m.selectedImmersiveFrameId = aInputFrameId;
      m.selectedImmersiveFramePoseTime = iter->predictedDisplayTime;
      found = true;
      break;
    }
  }

  const uint64_t lag = m.latestImmersiveInputFrameId >= aInputFrameId
                           ? m.latestImmersiveInputFrameId - aInputFrameId
                           : 0;
  m.poseMatchLagSum += lag;
  m.poseMatchLagMax = std::max(m.poseMatchLagMax, lag);
  if (found) {
    const XrDuration poseAge =
        std::max<XrDuration>(0, m.runtimePredictedDisplayTime - m.selectedImmersiveFramePoseTime);
    m.poseMatchAgeSum += poseAge;
    m.poseMatchAgeMax = std::max(m.poseMatchAgeMax, poseAge);
    if (m.runtimeTargetViewsValid &&
        m.runtimeTargetViews.size() == m.selectedImmersiveFrameViews.size()) {
      for (size_t i = 0; i < m.runtimeTargetViews.size(); ++i) {
        const double rotation = QuaternionAngularDistanceDegrees(
            m.selectedImmersiveFrameViews[i].pose.orientation,
            m.runtimeTargetViews[i].pose.orientation);
        const double position = PosePositionDistanceMillimeters(
            m.selectedImmersiveFrameViews[i].pose,
            m.runtimeTargetViews[i].pose);
        m.latePoseRotationSum += rotation;
        m.latePoseRotationMax = std::max(m.latePoseRotationMax, rotation);
        m.latePosePositionSum += position;
        m.latePosePositionMax = std::max(m.latePosePositionMax, position);
        ++m.latePoseSampleCount;
      }
    } else {
      ++m.latePoseInvalidCount;
    }
  } else {
    ++m.poseMatchMissCount;
    ++m.latePoseInvalidCount;
  }

  if (++m.poseMatchDiagnosticCount == 90) {
    VRB_LOG("VIVE XR pose matching: avgLag=%.2f maxLag=%llu unmatched=%u/90 avgAge=%.2fms maxAge=%.2fms latest=%llu rendered=%llu",
            static_cast<double>(m.poseMatchLagSum) / 90.0,
            static_cast<unsigned long long>(m.poseMatchLagMax), m.poseMatchMissCount,
            static_cast<double>(m.poseMatchAgeSum) / 90000000.0,
            static_cast<double>(m.poseMatchAgeMax) / 1000000.0,
            static_cast<unsigned long long>(m.latestImmersiveInputFrameId),
            static_cast<unsigned long long>(aInputFrameId));
    if (m.latePoseSampleCount > 0) {
      VRB_LOG("VIVE XR late pose delta: avgRot=%.4fdeg maxRot=%.4fdeg avgPos=%.3fmm maxPos=%.3fmm samples=%u invalidFrames=%u/90",
              m.latePoseRotationSum / m.latePoseSampleCount,
              m.latePoseRotationMax,
              m.latePosePositionSum / m.latePoseSampleCount,
              m.latePosePositionMax,
              m.latePoseSampleCount, m.latePoseInvalidCount);
    } else {
      VRB_LOG("VIVE XR late pose delta: no valid samples invalidFrames=%u/90",
              m.latePoseInvalidCount);
    }
    m.poseMatchDiagnosticCount = 0;
    m.poseMatchLagSum = 0;
    m.poseMatchLagMax = 0;
    m.poseMatchMissCount = 0;
    m.poseMatchAgeSum = 0;
    m.poseMatchAgeMax = 0;
    m.latePoseSampleCount = 0;
    m.latePoseInvalidCount = 0;
    m.latePoseRotationSum = 0.0;
    m.latePoseRotationMax = 0.0;
    m.latePosePositionSum = 0.0;
    m.latePosePositionMax = 0.0;
  }
#else
  (void)aInputFrameId;
#endif
}

void
DeviceDelegateOpenXR::BindEye(const device::Eye aWhich) {
  if (!m.vrReady) {
    VRB_ERROR("OpenXR BindEye called while not in VR mode");
    return;
  }

  int32_t index = device::EyeIndex(aWhich);
  if (index < 0 || index >= m.eyeSwapChains.size()) {
    VRB_ERROR("No eye found");
    return;
  }

  if (m.boundSwapChain) {
    m.boundSwapChain->ReleaseImage();
  }

  m.boundSwapChain = m.eyeSwapChains[index];
  m.boundSwapChain->AcquireImage();
  m.boundSwapChain->BindFBO();
  VRB_GL_CHECK(glViewport(0, 0, m.boundSwapChain->Width(), m.boundSwapChain->Height()));
  VRB_GL_CHECK(glClearColor(m.clearColor.Red(), m.clearColor.Green(), m.clearColor.Blue(), m.clearColor.Alpha()));
  VRB_GL_CHECK(glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT));

  for (const auto& layer: m.uiLayers)
    layer->SetCurrentEye(aWhich);
}

bool
DeviceDelegateOpenXR::IsPassthroughEnabled() const {
    if (m.renderMode == device::RenderMode::StandAlone)
        return mIsPassthroughEnabled;

    return (m.immersiveBlendMode != XR_ENVIRONMENT_BLEND_MODE_OPAQUE) ||
            (m.passthroughStrategy->usesCompositorLayer() &&
             mImmersiveXrSessionType == DeviceDelegate::ImmersiveXRSessionType::AR);
};

void
DeviceDelegateOpenXR::EndFrame(const FrameEndMode aEndMode) {
  if (!m.vrReady) {
    VRB_ERROR("OpenXR EndFrame called while not in VR mode");
    return;
  }
  if (m.boundSwapChain) {
    m.boundSwapChain->ReleaseImage();
    m.boundSwapChain = nullptr;
  }

  const bool frameAhead = m.framePrediction == FramePrediction::ONE_FRAME_AHEAD;
  XrTime displayTime = frameAhead ? m.prevPredictedDisplayTime : m.predictedDisplayTime;
  const std::vector<XrView>* targetViews = frameAhead ? &m.prevViews : &m.views;
#if defined(VIVEXR)
  if (m.renderMode == device::RenderMode::Immersive &&
      !m.selectedImmersiveFrameViews.empty()) {
    targetViews = &m.selectedImmersiveFrameViews;
    displayTime = m.runtimePredictedDisplayTime;
  }
  // When Wolvic misses a runtime interval, the one-frame-ahead timestamp is
  // already a full display period in the past. Submit the rendered pose for
  // the runtime's current target time so asynchronous reprojection can cover
  // the missed frame instead of presenting against a stale target timestamp.
  if (frameAhead && displayTime + m.predictedDisplayPeriod / 2 < m.runtimePredictedDisplayTime) {
    displayTime = m.runtimePredictedDisplayTime;
  }
  if (m.renderMode == device::RenderMode::Immersive) {
    if (m.lastSubmittedDisplayTime != 0) {
      const XrDuration step = displayTime - m.lastSubmittedDisplayTime;
      if (m.submittedTimeDiagnosticCount == 0) {
        m.submittedTimeStepMin = step;
        m.submittedTimeStepMax = step;
      } else {
        m.submittedTimeStepMin = std::min(m.submittedTimeStepMin, step);
        m.submittedTimeStepMax = std::max(m.submittedTimeStepMax, step);
      }
      m.submittedTimeStepSum += step;
      if (step <= 0) {
        ++m.submittedTimeNonMonotonicCount;
      }
      if (std::llabs(step - m.predictedDisplayPeriod) > m.predictedDisplayPeriod / 2) {
        ++m.submittedTimeIrregularCount;
      }
      if (++m.submittedTimeDiagnosticCount == 90) {
        VRB_LOG("VIVE XR submitted time: avgStep=%.3fms minStep=%.3fms maxStep=%.3fms nonMonotonic=%u/90 irregular=%u/90 period=%.3fms",
                static_cast<double>(m.submittedTimeStepSum) / 90000000.0,
                static_cast<double>(m.submittedTimeStepMin) / 1000000.0,
                static_cast<double>(m.submittedTimeStepMax) / 1000000.0,
                m.submittedTimeNonMonotonicCount, m.submittedTimeIrregularCount,
                static_cast<double>(m.predictedDisplayPeriod) / 1000000.0);
        m.submittedTimeDiagnosticCount = 0;
        m.submittedTimeNonMonotonicCount = 0;
        m.submittedTimeIrregularCount = 0;
        m.submittedTimeStepSum = 0;
      }
    }
    m.lastSubmittedDisplayTime = displayTime;
  }
#endif

  std::vector<const XrCompositionLayerBaseHeader*>& layers = m.frameEndLayers;
  layers.clear();

  auto pickEnvironmentBlendMode = [this](device::RenderMode renderMode) {
      if (renderMode == device::RenderMode::Immersive)
          return m.shouldUsePassthrough ? m.immersiveBlendMode : m.defaultBlendMode;

      return mIsPassthroughEnabled ? m.passthroughBlendMode : m.defaultBlendMode;
  };

  // This limit is valid at least for Pico and Meta.
  auto submitEndFrame = [&layers, displayTime, session = m.session, blendMode = pickEnvironmentBlendMode(m.renderMode), distance = m.furthestHitDistance]() {
      XrFrameEndInfo frameEndInfo{XR_TYPE_FRAME_END_INFO};
      frameEndInfo.displayTime = displayTime;
      frameEndInfo.environmentBlendMode = blendMode;
      frameEndInfo.layerCount = (uint32_t) layers.size();
      frameEndInfo.layers = layers.data();

      XrFrameEndInfoML frameEndInfoML{XR_TYPE_FRAME_END_INFO_ML};
      if (OpenXRExtensions::IsExtensionSupported(XR_ML_FRAME_END_INFO_EXTENSION_NAME)) {
            frameEndInfoML.next = nullptr;
            frameEndInfoML.focusDistance = distance;
            frameEndInfo.next = &frameEndInfoML;
      }

      MessageXrResult(xrEndFrame(session, &frameEndInfo));
  };

  auto canAddLayers = [&layers, maxLayers = m.systemProperties.graphicsProperties.maxLayerCount]() {
      // Some runtimes incorrectly report 0 as maxLayerCount like Spaces.
      return layers.size() < std::max(1u, maxLayers);
  };

  if (!mShouldRender) {
      submitEndFrame();
      return;
  }

  XrPosef reorientPose = MatrixToXrPose(GetReorientTransform());

  // Add skybox or passthrough layer
  if (m.shouldUsePassthrough) {
      if (m.passthroughLayer && m.IsPassthroughLayerReady())
          layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&m.passthroughLayer->xrCompositionLayer));
  } else if (m.cubeLayer && m.cubeLayer->IsLoaded() && m.cubeLayer->IsDrawRequested()) {
    m.cubeLayer->Update(m.localSpace, reorientPose, XR_NULL_HANDLE);
    for (uint32_t i = 0; i < m.cubeLayer->HeaderCount(); ++i) {
      layers.push_back(m.cubeLayer->Header(i));
    }
    m.cubeLayer->ClearRequestDraw();
  }

  // Add VR video layer
  if (m.equirectLayer && m.equirectLayer->IsDrawRequested()) {
    m.equirectLayer->Update(m.localSpace, reorientPose, XR_NULL_HANDLE);
    for (uint32_t i = 0; i < m.equirectLayer->HeaderCount(); ++i) {
      layers.push_back(m.equirectLayer->Header(i));
    }
    m.equirectLayer->ClearRequestDraw();
  }

  // Sort quad layers by draw priority
  std::sort(m.uiLayers.begin(), m.uiLayers.end(), [](const OpenXRLayerPtr & a, OpenXRLayerPtr & b) -> bool {
    return a->GetLayer()->ShouldDrawBefore(*b->GetLayer());
  });

  // Add back UI layers
  for (const OpenXRLayerPtr& layer: m.uiLayers) {
    if (!layer->GetDrawInFront() && layer->IsDrawRequested() && canAddLayers()) {
      layer->Update(m.layersSpace, reorientPose, XR_NULL_HANDLE);
      for (uint32_t i = 0; i < layer->HeaderCount() && canAddLayers(); ++i) {
        layers.push_back(layer->Header(i));
      }
      layer->ClearRequestDraw();
    }
  }

  if (!canAddLayers()) {
      submitEndFrame();
      return;
  }

  // Add main eye buffer layer
  XrCompositionLayerProjection projectionLayer{XR_TYPE_COMPOSITION_LAYER_PROJECTION};
  std::vector<XrCompositionLayerProjectionView> projectionLayerViews;
  projectionLayerViews.resize(targetViews->size());
  projectionLayer.layerFlags = XR_COMPOSITION_LAYER_BLEND_TEXTURE_SOURCE_ALPHA_BIT;
  for (int i = 0; i < targetViews->size(); ++i) {
    const OpenXRSwapChainPtr& viewSwapChain =  m.eyeSwapChains[i];
    projectionLayerViews[i] = {XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW};
    projectionLayerViews[i].pose = (*targetViews)[i].pose;
    projectionLayerViews[i].fov = (*targetViews)[i].fov;
    projectionLayerViews[i].subImage.swapchain = viewSwapChain->SwapChain();
    projectionLayerViews[i].subImage.imageRect.offset = {0, 0};
    projectionLayerViews[i].subImage.imageRect.extent = {viewSwapChain->Width(), viewSwapChain->Height()};
  }
  projectionLayer.space = m.localSpace;
  projectionLayer.viewCount = (uint32_t)projectionLayerViews.size();
  projectionLayer.views = projectionLayerViews.data();
  layers.push_back(reinterpret_cast<XrCompositionLayerBaseHeader*>(&projectionLayer));

  // Add front UI layers
  for (const OpenXRLayerPtr& layer: m.uiLayers) {
    if (layer->GetDrawInFront() && layer->IsDrawRequested() && canAddLayers()) {
      layer->Update(m.layersSpace, reorientPose, XR_NULL_HANDLE);
      for (uint32_t i = 0; i < layer->HeaderCount() && canAddLayers(); ++i) {
        layers.push_back(layer->Header(i));
      }
      layer->ClearRequestDraw();
    }
  }

  submitEndFrame();
}

VRLayerQuadPtr
DeviceDelegateOpenXR::CreateLayerQuad(int32_t aWidth, int32_t aHeight,
                                        VRLayerSurface::SurfaceType aSurfaceType) {
  if (!m.layersEnabled) {
    return nullptr;
  }

  VRLayerQuadPtr layer = VRLayerQuad::Create(aWidth, aHeight, aSurfaceType);
  OpenXRLayerQuadPtr xrLayer = OpenXRLayerQuad::Create(m.javaContext->env, layer);
  m.AddUILayer(xrLayer, aSurfaceType);
  return layer;
}

VRLayerQuadPtr
DeviceDelegateOpenXR::CreateLayerQuad(const VRLayerSurfacePtr& aMoveLayer) {
  if (!m.layersEnabled) {
    return nullptr;
  }

  VRLayerQuadPtr layer = VRLayerQuad::Create(aMoveLayer->GetWidth(), aMoveLayer->GetHeight(), aMoveLayer->GetSurfaceType());
  OpenXRLayerQuadPtr xrLayer;

  for (int i = 0; i < m.uiLayers.size(); ++i) {
    if (m.uiLayers[i]->GetLayer() == aMoveLayer) {
      xrLayer = OpenXRLayerQuad::Create(m.javaContext->env, layer, m.uiLayers[i]);
      m.uiLayers.erase(m.uiLayers.begin() + i);
      break;
    }
  }
  if (xrLayer) {
    m.AddUILayer(xrLayer, aMoveLayer->GetSurfaceType());
  }
  return layer;
}

VRLayerCylinderPtr
DeviceDelegateOpenXR::CreateLayerCylinder(int32_t aWidth, int32_t aHeight,
                                            VRLayerSurface::SurfaceType aSurfaceType) {
  if (!m.layersEnabled) {
    return nullptr;
  }

  VRLayerCylinderPtr layer = VRLayerCylinder::Create(aWidth, aHeight, aSurfaceType);
  OpenXRLayerCylinderPtr xrLayer = OpenXRLayerCylinder::Create(m.javaContext->env, layer);
  m.AddUILayer(xrLayer, aSurfaceType);
  return layer;
}

VRLayerCylinderPtr
DeviceDelegateOpenXR::CreateLayerCylinder(const VRLayerSurfacePtr& aMoveLayer) {
  if (!m.layersEnabled) {
    return nullptr;
  }

  VRLayerCylinderPtr layer = VRLayerCylinder::Create(aMoveLayer->GetWidth(), aMoveLayer->GetHeight(), aMoveLayer->GetSurfaceType());
  OpenXRLayerCylinderPtr xrLayer;

  for (int i = 0; i < m.uiLayers.size(); ++i) {
    if (m.uiLayers[i]->GetLayer() == aMoveLayer) {
      xrLayer = OpenXRLayerCylinder::Create(m.javaContext->env, layer, m.uiLayers[i]);
      m.uiLayers.erase(m.uiLayers.begin() + i);
      break;
    }
  }
  if (xrLayer) {
    m.AddUILayer(xrLayer, aMoveLayer->GetSurfaceType());
  }
  return layer;
}


VRLayerCubePtr
DeviceDelegateOpenXR::CreateLayerCube(int32_t aWidth, int32_t aHeight, GLint aInternalFormat) {
  if (!m.layersEnabled) {
    return nullptr;
  }
  if (m.cubeLayer) {
    m.cubeLayer->Destroy();
  }
  VRLayerCubePtr layer = VRLayerCube::Create(aWidth, aHeight, aInternalFormat);
  m.cubeLayer = OpenXRLayerCube::Create(layer, aInternalFormat);
  if (m.session != XR_NULL_HANDLE) {
    vrb::RenderContextPtr context = m.context.lock();
    m.cubeLayer->Init(m.javaContext->env, m.session, context);
  }
  return layer;
}

VRLayerEquirectPtr
DeviceDelegateOpenXR::CreateLayerEquirect(const VRLayerPtr &aSource) {
  if (!m.layersEnabled) {
    return nullptr;
  }

  VRLayerEquirectPtr result = VRLayerEquirect::Create();
  OpenXRLayerPtr source;
  for (const OpenXRLayerPtr& layer: m.uiLayers) {
    if (layer->GetLayer() == aSource) {
      source = layer;
      break;
    }
  }
  if (m.equirectLayer) {
    m.equirectLayer->Destroy();
  }
  m.equirectLayer = OpenXRLayerEquirect::Create(result, source);
  if (m.session != XR_NULL_HANDLE) {
    vrb::RenderContextPtr context = m.context.lock();
    m.equirectLayer->Init(m.javaContext->env, m.session, context);
  }
  return result;
}

void
DeviceDelegateOpenXR::CreateLayerPassthrough() {
  assert(m.passthroughStrategy);
  if (!m.layersEnabled || m.passthroughLayer || !m.passthroughStrategy->usesCompositorLayer()) {
    return;
  }

  m.passthroughLayer = m.passthroughStrategy->createLayerIfSupported();
  VRB_LOG("--- Passthrough layer created ---");
  assert(m.passthroughLayer);

  assert(m.session != XR_NULL_HANDLE);
  m.passthroughLayer->Init(m.session);

  UpdatePassthrough();
}

bool
DeviceDelegateOpenXR::usesPassthroughCompositorLayer() const {
  assert(m.passthroughStrategy);
  return m.passthroughStrategy->usesCompositorLayer();
}

void
DeviceDelegateOpenXR::DeleteLayer(const VRLayerPtr& aLayer) {
  if (m.cubeLayer && m.cubeLayer->layer == aLayer) {
    m.cubeLayer->Destroy();
    m.cubeLayer = nullptr;
    return;
  }
  if (m.equirectLayer && m.equirectLayer->layer == aLayer) {
    m.equirectLayer->Destroy();
    m.equirectLayer = nullptr;
    return;
  }
  for (int i = 0; i < m.uiLayers.size(); ++i) {
    if (m.uiLayers[i]->GetLayer() == aLayer) {
      m.uiLayers[i]->Destroy();
      m.uiLayers.erase(m.uiLayers.begin() + i);
      return;
    }
  }
}

int32_t DeviceDelegateOpenXR::GetHandTrackingJointIndex(const HandTrackingJoints aJoint) {
  switch (aJoint) {
    case HandTrackingJoints::Palm: return XR_HAND_JOINT_PALM_EXT;
    case HandTrackingJoints::Wrist: return XR_HAND_JOINT_WRIST_EXT;
    case HandTrackingJoints::ThumbMetacarpal: return XR_HAND_JOINT_THUMB_METACARPAL_EXT;
    case HandTrackingJoints::ThumbProximal: return XR_HAND_JOINT_THUMB_PROXIMAL_EXT;
    case HandTrackingJoints::ThumbDistal: return XR_HAND_JOINT_THUMB_DISTAL_EXT;
    case HandTrackingJoints::ThumbTip: return XR_HAND_JOINT_THUMB_TIP_EXT;
    case HandTrackingJoints::IndexMetacarpal: return XR_HAND_JOINT_INDEX_METACARPAL_EXT;
    case HandTrackingJoints::IndexProximal: return XR_HAND_JOINT_INDEX_PROXIMAL_EXT;
    case HandTrackingJoints::IndexIntermediate: return XR_HAND_JOINT_INDEX_INTERMEDIATE_EXT;
    case HandTrackingJoints::IndexDistal: return XR_HAND_JOINT_INDEX_DISTAL_EXT;
    case HandTrackingJoints::IndexTip: return XR_HAND_JOINT_INDEX_TIP_EXT;
    case HandTrackingJoints::MiddleMetacarpal: return XR_HAND_JOINT_MIDDLE_METACARPAL_EXT;
    case HandTrackingJoints::MiddleProximal: return XR_HAND_JOINT_MIDDLE_PROXIMAL_EXT;
    case HandTrackingJoints::MiddleIntermediate: return XR_HAND_JOINT_MIDDLE_INTERMEDIATE_EXT;
    case HandTrackingJoints::MiddleDistal: return XR_HAND_JOINT_MIDDLE_DISTAL_EXT;
    case HandTrackingJoints::MiddleTip: return XR_HAND_JOINT_MIDDLE_TIP_EXT;
    case HandTrackingJoints::RingMetacarpal: return XR_HAND_JOINT_RING_METACARPAL_EXT;
    case HandTrackingJoints::RingProximal: return XR_HAND_JOINT_RING_PROXIMAL_EXT;
    case HandTrackingJoints::RingIntermediate: return XR_HAND_JOINT_RING_INTERMEDIATE_EXT;
    case HandTrackingJoints::RingDistal: return XR_HAND_JOINT_RING_DISTAL_EXT;
    case HandTrackingJoints::RingTip: return XR_HAND_JOINT_RING_TIP_EXT;
    case HandTrackingJoints::LittleMetacarpal: return XR_HAND_JOINT_LITTLE_METACARPAL_EXT;
    case HandTrackingJoints::LittleProximal: return XR_HAND_JOINT_LITTLE_PROXIMAL_EXT;
    case HandTrackingJoints::LittleIntermediate: return XR_HAND_JOINT_LITTLE_INTERMEDIATE_EXT;
    case HandTrackingJoints::LittleDistal: return XR_HAND_JOINT_LITTLE_DISTAL_EXT;
    case HandTrackingJoints::LittleTip: return XR_HAND_JOINT_LITTLE_TIP_EXT;
  }
  return -1;
}

void
DeviceDelegateOpenXR::UpdateHandMesh(const uint32_t aControllerIndex, const std::vector<vrb::Matrix>& handJointTransforms,
               const vrb::GroupPtr& aRoot, const bool aEnabled, const bool leftHanded) {
  if (!m.handMeshRenderer)
    return;

  HandMeshBufferPtr buffer = m.input->GetNextHandMeshBuffer(aControllerIndex);
  m.handMeshRenderer->Update(aControllerIndex, handJointTransforms, aRoot, buffer, aEnabled, leftHanded);
}

void
DeviceDelegateOpenXR::DrawHandMesh(const uint32_t aControllerIndex, const vrb::Camera& aCamera) {
  if (!m.handMeshRenderer)
    return;
  m.handMeshRenderer->Draw(aControllerIndex, aCamera);
}

void DeviceDelegateOpenXR::SetHitDistance(const float distance) {
  m.furthestHitDistance = std::max(distance, m.furthestHitDistance);
}

bool DeviceDelegateOpenXR::PopulateTrackedKeyboardInfo(DeviceDelegate::TrackedKeyboardInfo& keyboardInfo) {
  return m.input->PopulateTrackedKeyboardInfo(keyboardInfo);
}

void
DeviceDelegateOpenXR::EnterVR(const crow::BrowserEGLContext& aEGLContext) {
  // Reset reorientation after Enter VR
  m.reorientMatrix = vrb::Matrix::Identity();
  m.firstPose = std::nullopt;

  if (m.session != XR_NULL_HANDLE && m.graphicsBinding.context == aEGLContext.Context()) {
#if HVR
    // Session already created, call begin again. This can happen for example in HVR when reentering
    // the security zone, because HVR forces us to stop and end the session when exiting.
    m.BeginXRSession();
#endif
    ProcessEvents();
    return;
  }

  CHECK(m.instance != XR_NULL_HANDLE && m.system != XR_NULL_SYSTEM_ID);
  m.CheckGraphicsRequirements();

  m.graphicsBinding.context = aEGLContext.Context();
  m.graphicsBinding.display = aEGLContext.Display();
  m.graphicsBinding.config = aEGLContext.Config();

  XrSessionCreateInfo createInfo{XR_TYPE_SESSION_CREATE_INFO};
  createInfo.next = reinterpret_cast<const XrBaseInStructure*>(&m.graphicsBinding);
  createInfo.systemId = m.system;

  XrSessionCreateInfoOverlayEXTX overlayInfo {
      .type = XR_TYPE_SESSION_CREATE_INFO_OVERLAY_EXTX,
      .createFlags = 0,
      .sessionLayersPlacement = 0
  };
  if (OpenXRExtensions::IsExtensionSupported(XR_EXTX_OVERLAY_EXTENSION_NAME)) {
    auto oldNext = createInfo.next;
    createInfo.next = &overlayInfo;
    overlayInfo.next = oldNext;
  }

  CHECK_XRCMD(xrCreateSession(m.instance, &createInfo, &m.session));
  CHECK(m.session != XR_NULL_HANDLE);
  VRB_LOG("OpenXR session created succesfully");

  m.UpdateSpaces();
  m.InitializeViews();
  m.InitializeImmersiveDisplay();
  m.InitializeRefreshRates();

  m.passthroughStrategy->initializePassthrough(m.session);

  m.input = OpenXRInput::Create(m.instance, m.session, m.systemProperties, m.localSpace, m.isEyeTrackingSupported, *m.controller.get());
  ProcessEvents();
  if (m.controllersCreatedCallback) {
    m.controllersCreatedCallback();
    m.controllersCreatedCallback = nullptr;
  }
  m.MaybeNotifyControllersReady();
  m.input->SetKeyboardTrackingEnabled(m.keyboardTrackingSupported && m.renderModelLoadingSupported);

  vrb::RenderContextPtr context = m.context.lock();

  // Lazily create hand mesh renderer
  if (m.mHandTrackingSupported && !m.handMeshRenderer) {
    auto& create = context->GetRenderThreadCreationContext();
    if (m.handMeshProperties) {
      m.handMeshRenderer = HandMeshRendererGeometry::Create(create);
      m.input->SetHandMeshBufferSizes(m.handMeshProperties->indexCount, m.handMeshProperties->vertexCount);
    } else {
#if defined(PICOXR)
      // Due to unreliable hand-tracking orientation data on Pico devices running system
      // versions earlier than 5.7.1, we use the Spheres strategy.
      if (CompareBuildIdString(kPicoVersionHandTrackingUpdate))
        m.handMeshRenderer = HandMeshRendererSpheres::Create(create);
#endif
    }

    if (!m.handMeshRenderer)
      m.handMeshRenderer = HandMeshRendererSkinned::Create(create);
  }

  // Initialize layers if needed
  for (OpenXRLayerPtr& layer: m.uiLayers) {
    layer->Init(m.javaContext->env, m.session, context);
  }
  if (m.cubeLayer) {
    m.cubeLayer->Init(m.javaContext->env, m.session, context);
  }
  if (m.equirectLayer) {
    m.equirectLayer->Init(m.javaContext->env, m.session, context);
  }

  m.UpdateClockLevels();
  m.UpdateDisplayRefreshRate();
}

void
DeviceDelegateOpenXR::LeaveVR() {
  CHECK_MSG(!m.boundSwapChain, "Eye swapChain not released before LeaveVR");
  if (m.session == XR_NULL_HANDLE) {
    return;
  }
#ifdef HVR
  xrRequestExitSession(m.session);
#endif
  ProcessEvents();
}

void
DeviceDelegateOpenXR::OnDestroy() {
  m.Shutdown();
}

bool
DeviceDelegateOpenXR::IsInVRMode() const {
  return m.vrReady;
}

bool
DeviceDelegateOpenXR::ExitApp() {
  xrRequestExitSession(m.session);
  return true;
}

bool
DeviceDelegateOpenXR::ShouldExitRenderLoop() const
{
  return m.sessionState == XR_SESSION_STATE_EXITING || m.sessionState == XR_SESSION_STATE_LOSS_PENDING;
}

DeviceDelegateOpenXR::DeviceDelegateOpenXR(State &aState) : m(aState) {}

DeviceDelegateOpenXR::~DeviceDelegateOpenXR() { m.Shutdown(); }

void DeviceDelegateOpenXR::SetPointerMode(const DeviceDelegate::PointerMode mode) {
  m.pointerMode = mode;
}

void DeviceDelegateOpenXR::SetImmersiveBlendMode(device::BlendMode mode) {
  auto prevImmersiveBlendMode = m.immersiveBlendMode;
  m.immersiveBlendMode = toOpenXRBlendMode(mode);
  if (prevImmersiveBlendMode != m.immersiveBlendMode)
    UpdatePassthrough();
}

void DeviceDelegateOpenXR::SetHandTrackingEnabled(bool value) {
  m.handTrackingEnabled = value;
}

float DeviceDelegateOpenXR::GetSelectThreshold(int32_t aControllerIndex) {
  return m.input->GetSelectThreshold(aControllerIndex);
}

void
DeviceDelegateOpenXR::TogglePassthroughEnabled() {
  DeviceDelegate::TogglePassthroughEnabled();
  UpdatePassthrough();
}

void
DeviceDelegateOpenXR::UpdatePassthrough() {
  m.shouldUsePassthrough = IsPassthroughEnabled();
  if (!m.IsPassthroughLayerReady())
    return;

  if (m.shouldUsePassthrough)
    OpenXRExtensions::sXrPassthroughLayerResumeFB(m.passthroughLayer->GetPassthroughLayerHandle());
  else
    OpenXRExtensions::sXrPassthroughLayerPauseFB(m.passthroughLayer->GetPassthroughLayerHandle());
}

unsigned
DeviceDelegateOpenXR::MaxCompositionLayers() const {
  CHECK(m.instance != XR_NULL_HANDLE && m.system != XR_NULL_SYSTEM_ID);
  // Even if the runtime supports multiple layers, do not report them if the Android surface
  // swapchain extension is not available, because we won't be able to create the swapchains
  return m.layersEnabled ? m.systemProperties.graphicsProperties.maxLayerCount : 1;
}

} // namespace crow
