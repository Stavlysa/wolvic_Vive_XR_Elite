/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#pragma once

#include <cmath>

namespace crow {

// Gecko's eye rectangles use the same top-to-bottom convention as the
// existing external-texture quad. Do not introduce another vertical flip.
inline bool BuildStereoEyeUV(float x, float y, float width, float height, float* uv) {
  if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(width) ||
      !std::isfinite(height) || x < 0.0f || y < 0.0f || x >= 1.0f || y >= 1.0f || width <= 0.0f ||
      height <= 0.0f || x + width > 1.00001f || y + height > 1.00001f) {
    return false;
  }
  const float right = std::fmin(1.0f, x + width);
  const float bottom = std::fmin(1.0f, y + height);
  const float coordinates[] = {x, y, x, bottom, right, y, right, bottom};
  for (int i = 0; i < 8; ++i) {
    uv[i] = coordinates[i];
  }
  return true;
}

} // namespace crow
