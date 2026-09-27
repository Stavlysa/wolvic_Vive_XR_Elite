/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#pragma once
#include <cstddef>

namespace crow {
inline bool ValidWaveStereoIndices(int left, int right, size_t leftSize, size_t rightSize) {
  return left >= 0 && right >= 0 && static_cast<size_t>(left) < leftSize &&
      static_cast<size_t>(right) < rightSize;
}
} // namespace crow
