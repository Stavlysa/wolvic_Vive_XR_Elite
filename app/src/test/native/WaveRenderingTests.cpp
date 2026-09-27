/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "StereoUV.h"
#include "WaveFrameState.h"
#include "WavePassthrough.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <limits>

static std::atomic<bool> supported{true}, failEnable{false}, blockEnable{false};
static std::atomic<int> enableCalls{0}, disableCalls{0};

extern "C" uint64_t WVR_GetSupportedFeatures() {
  return supported ? WVR_SupportedFeature_PassthroughOverlay : 0;
}
extern "C" WVR_Result WVR_ShowPassthroughUnderlay(bool show) {
  if (show) {
    ++enableCalls;
    while (blockEnable) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return failEnable ? WVR_Error_FeatureNotSupport : WVR_Success;
  }
  ++disableCalls;
  return WVR_Success;
}

template<typename Predicate> static void WaitFor(Predicate done) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (!done()) {
    assert(std::chrono::steady_clock::now() < deadline);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
}

int main() {
  float uv[8] = {};
  assert(crow::BuildStereoEyeUV(0, 0, .5f, 1, uv));
  const float left[] = {0,0,0,1,.5f,0,.5f,1};
  for (int i = 0; i < 8; ++i) assert(uv[i] == left[i]);
  assert(crow::BuildStereoEyeUV(.5f, 0, .5f, 1, uv));
  assert(uv[0] == .5f && uv[6] == 1 && uv[7] == 1);
  assert(crow::BuildStereoEyeUV(.125f, .25f, .25f, .5f, uv));
  assert(uv[0] == .125f && uv[1] == .25f && uv[6] == .375f && uv[7] == .75f);
  assert(!crow::BuildStereoEyeUV(0, 0, 0, 1, uv));
  assert(!crow::BuildStereoEyeUV(-.1f, 0, .5f, 1, uv));
  assert(!crow::BuildStereoEyeUV(.75f, 0, .5f, 1, uv));
  assert(!crow::BuildStereoEyeUV(0, 0, std::numeric_limits<float>::quiet_NaN(), 1, uv));
  assert(crow::ValidWaveStereoIndices(0, 2, 3, 3));
  assert(!crow::ValidWaveStereoIndices(-1, 0, 3, 3));
  assert(!crow::ValidWaveStereoIndices(0, -1, 3, 3));
  assert(!crow::ValidWaveStereoIndices(3, 0, 3, 3));
  assert(!crow::ValidWaveStereoIndices(0, 0, 0, 0));

  {
    crow::WavePassthrough pt;
    pt.Initialize();
    pt.Request(true);
    WaitFor([&] { return pt.IsActive(); });
    int callsBeforeResume = enableCalls;
    pt.Request(true, true); // runtime may reset the underlay while sleeping
    WaitFor([&] { return enableCalls > callsBeforeResume; });
    pt.Request(false);
    WaitFor([&] { return !pt.IsActive(); });
    pt.Request(true);
    WaitFor([&] { return pt.IsActive(); });
    int before = disableCalls;
    pt.Shutdown();
    assert(!pt.IsActive() && disableCalls == before + 1);
    pt.Shutdown(); // idempotent
  }
  {
    crow::WavePassthrough pt;
    pt.Request(true); // persisted preference restored before render init
    assert(!pt.TakeFailure() && !pt.IsActive());
    pt.Initialize();
    WaitFor([&] { return pt.IsActive(); });
  }
  {
    failEnable = true;
    crow::WavePassthrough pt;
    pt.Initialize();
    pt.Request(true);
    WaitFor([&] { return pt.TakeFailure(); });
    assert(!pt.IsActive());
    failEnable = false;
    pt.Request(false);
    pt.Request(true);
    WaitFor([&] { return pt.IsActive(); });
  }
  {
    blockEnable = true;
    crow::WavePassthrough pt;
    pt.Initialize();
    int before = enableCalls;
    pt.Request(true);
    WaitFor([&] { return enableCalls > before; });
    pt.Request(false); // coalesce a toggle arriving during IPC
    int disabled = disableCalls;
    blockEnable = false;
    WaitFor([&] { return disableCalls > disabled; });
    assert(!pt.IsActive());
  }
  {
    supported = false;
    crow::WavePassthrough pt;
    pt.Initialize();
    pt.Request(true);
    assert(pt.TakeFailure() && !pt.IsActive());
  }
  std::puts("PASS: stereo UVs, Wave queue indices, passthrough on/off, shutdown, failure, rapid toggle, unsupported device");
}
