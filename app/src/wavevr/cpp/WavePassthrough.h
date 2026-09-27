/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <wvr/wvr.h>
#include <wvr/wvr_device.h>
#include <wvr/wvr_system.h>
#include "vrb/Logger.h"

namespace crow {

// The Wave passthrough API performs IPC. Keep it off the rendering thread;
// join the worker and restore system passthrough before WVR_Quit.
class WavePassthrough {
public:
  void Initialize() {
    mSupported = (WVR_GetSupportedFeatures() & WVR_SupportedFeature_PassthroughOverlay) != 0;
    mInitialized = true;
    VRB_LOG("Wave background passthrough supported=%d", mSupported);
    if (mSupported) {
      mWorker = std::thread([this] { Run(); });
    } else if (mDesired) {
      mFailed.store(true);
    }
  }

  void Request(bool enabled, bool force = false) {
    // Java can restore the startup preference before WVR_RenderInit. Retain
    // that request until Initialize() knows the actual runtime capability.
    if (!mInitialized) {
      mDesired = enabled;
      ++mRevision;
      return;
    }
    if (!mSupported) {
      if (enabled) mFailed.store(true);
      return;
    }
    {
      std::lock_guard<std::mutex> lock(mMutex);
      if (mDesired == enabled && !force) return;
      mDesired = enabled;
      ++mRevision;
    }
    mCondition.notify_one();
  }

  bool IsActive() const { return mActive.load(); }
  bool TakeFailure() { return mFailed.exchange(false); }

  void Shutdown() {
    if (!mWorker.joinable()) return;
    {
      std::lock_guard<std::mutex> lock(mMutex);
      mStopping = true;
    }
    mCondition.notify_one();
    mWorker.join();
  }

  ~WavePassthrough() { Shutdown(); }

private:
  void Run() {
    uint64_t appliedRevision = 0;
    std::unique_lock<std::mutex> lock(mMutex);
    while (true) {
      mCondition.wait(lock, [&] { return mStopping || mRevision != appliedRevision; });
      if (mStopping) break;
      const bool enabled = mDesired;
      appliedRevision = mRevision;
      lock.unlock();
      const WVR_Result result = WVR_ShowPassthroughUnderlay(enabled);
      VRB_LOG("Wave background passthrough requested=%d result=%d", enabled, static_cast<int>(result));
      if (result == WVR_Success) {
        mActive.store(enabled);
      } else if (enabled) {
        mFailed.store(true);
      }
      lock.lock();
    }
    lock.unlock();
    if (mActive.load()) {
      WVR_ShowPassthroughUnderlay(false);
      mActive.store(false);
    }
  }

  bool mSupported = false;
  bool mInitialized = false;
  bool mDesired = false;
  bool mStopping = false;
  uint64_t mRevision = 0;
  std::atomic<bool> mActive{false};
  std::atomic<bool> mFailed{false};
  std::mutex mMutex;
  std::condition_variable mCondition;
  std::thread mWorker;
};

} // namespace crow
