/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#pragma once
#include <GLES3/gl3.h>

namespace crow {
inline void SetWaveSceneAlphaBlend() {
  // Scene shaders output straight RGB. The framebuffer stores premultiplied
  // RGB and coverage for the passthrough underlay. Do not square source alpha:
  // a translucent UI element over an opaque page must leave it opaque.
  glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                      GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
}
} // namespace crow
