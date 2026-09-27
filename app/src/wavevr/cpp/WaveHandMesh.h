/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#pragma once
#include <wvr/wvr_hand_render_model.h>
#include "vrb/Camera.h"
#include "vrb/Matrix.h"
#include "vrb/ShaderUtil.h"
#include "WaveAlphaBlend.h"
#include <future>
#include <chrono>
#include <functional>
#include <vector>
#include <array>
#include <cmath>
#include <cstring>

namespace crow {
// Device-provided geometry: no VIVE Browser APK assets are copied into Wolvic.
class WaveHandMesh {
  struct Mesh {
    GLuint vao = 0;
    std::array<GLuint, 7> buffers{};
    uint32_t count = 0;
    std::array<vrb::Matrix, 48> inverseBind{}, localBind{};
    std::array<uint32_t, 48> parent{};
    std::array<int32_t, 48> used{};
  };
  std::array<Mesh, 2> meshes;
  std::array<std::array<vrb::Matrix, 48>, 2> skin;
  std::array<bool, 2> visible{};
  GLuint program = 0, alpha = 0;
  std::future<WVR_HandRenderModel_t*> loading;
  std::chrono::steady_clock::time_point retryAfter{};

  bool Upload(Mesh& mesh, const WVR_HandModel_t& model) {
    const uint32_t count = model.vertices.size / 3;
    if (!count || count > 100000 || model.vertices.dimension != 3 || model.vertices.size != count * 3 ||
        !model.vertices.buffer || !model.normals.buffer || model.normals.size != count * 3 ||
        model.normals.dimension != 3 || !model.texCoords.buffer || model.texCoords.size != count * 2 ||
        model.texCoords.dimension != 2 || !model.texCoord2s.buffer || model.texCoord2s.size != count * 2 ||
        model.texCoord2s.dimension != 2 || !model.boneIDs.buffer || model.boneIDs.size != count * 4 ||
        model.boneIDs.dimension != 4 || !model.boneWeights.buffer || model.boneWeights.size != count * 4 ||
        model.boneWeights.dimension != 4 || !model.indices.buffer || !model.indices.size ||
        model.indices.size > 600000 || model.indices.type != 3 || model.indices.size % 3) return false;
    for (uint32_t i = 0; i < model.indices.size; ++i) if (model.indices.buffer[i] >= count) return false;
    std::vector<float> ids(model.boneIDs.size);
    for (uint32_t i = 0; i < model.boneIDs.size; ++i) {
      if (model.boneIDs.buffer[i] >= 48 || !std::isfinite(model.boneWeights.buffer[i])) return false;
      ids[i] = model.boneIDs.buffer[i];
    }
    for (int i = 0; i < 48; ++i) {
      if (model.jointUsageTable[i] == 1 && model.jointParentTable[i] >= 48) return false;
      mesh.inverseBind[i] = vrb::Matrix::FromColumnMajor(model.jointInvTransMats + i * 16);
      mesh.localBind[i] = vrb::Matrix::FromColumnMajor(model.jointLocalTransMats + i * 16);
      mesh.parent[i] = model.jointParentTable[i] < 48 ? model.jointParentTable[i] : 47;
      mesh.used[i] = model.jointUsageTable[i];
    }
    glGenVertexArrays(1, &mesh.vao);
    glBindVertexArray(mesh.vao);
    glGenBuffers(mesh.buffers.size(), mesh.buffers.data());
    const float* attrs[] = {model.vertices.buffer, model.normals.buffer, model.texCoords.buffer,
        model.texCoord2s.buffer, ids.data(), model.boneWeights.buffer};
    const int dims[] = {3,3,2,2,4,4};
    for (int i = 0; i < 6; ++i) {
      glBindBuffer(GL_ARRAY_BUFFER, mesh.buffers[i]);
      glBufferData(GL_ARRAY_BUFFER, count * dims[i] * sizeof(float), attrs[i], GL_STATIC_DRAW);
      glEnableVertexAttribArray(i);
      glVertexAttribPointer(i, dims[i], GL_FLOAT, GL_FALSE, 0, nullptr);
    }
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh.buffers[6]);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, model.indices.size * sizeof(uint32_t), model.indices.buffer, GL_STATIC_DRAW);
    mesh.count = model.indices.size;
    return true;
  }
  void DeleteModels() {
    for (auto& m : meshes) {
      glDeleteBuffers(m.buffers.size(), m.buffers.data());
      if (m.vao) glDeleteVertexArrays(1, &m.vao);
      m = {};
    }
    if (alpha) glDeleteTextures(1, &alpha);
    alpha = 0;
  }
  bool Ready() {
    if (meshes[0].count && meshes[1].count && program && alpha) return true;
    if (!loading.valid()) {
      if (std::chrono::steady_clock::now() < retryAfter) return false;
      retryAfter = std::chrono::steady_clock::now() + std::chrono::seconds(3);
      loading = std::async(std::launch::async, [] {
        WVR_HandRenderModel_t* model = nullptr;
        const auto result = WVR_GetCurrentNaturalHandModel(&model);
        if (result != WVR_Success) {
          VRB_WARN("Wave native hand model request failed: %d", result);
          if (model) WVR_ReleaseNatureHandModel(&model);
        }
        return model;
      });
      return false;
    }
    if (loading.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return false;
    auto* model = loading.get();
    if (!model) return false;
    GLint oldVao, oldBuffer, oldTexture, oldActive, oldUnpack;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &oldVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &oldBuffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &oldActive);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &oldTexture);
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &oldUnpack);
    const auto& t = model->handAlphaTex;
    const uint32_t channels = t.format == 1 ? 4 : t.format == 8 ? 1 : 0;
    bool ok = channels && t.bitmap && t.width && t.height && t.width <= 4096 && t.height <= 4096 &&
        t.stride >= t.width * channels && Upload(meshes[0], model->right) && Upload(meshes[1], model->left);
    if (ok) {
      std::vector<uint8_t> pixels(t.width * t.height * channels);
      for (uint32_t row = 0; row < t.height; ++row)
        std::memcpy(pixels.data() + row * t.width * channels, t.bitmap + row * t.stride, t.width * channels);
      glGenTextures(1, &alpha);
      glBindTexture(GL_TEXTURE_2D, alpha);
      glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
      glTexImage2D(GL_TEXTURE_2D, 0, channels == 4 ? GL_RGBA8 : GL_R8, t.width, t.height,
          0, channels == 4 ? GL_RGBA : GL_RED, GL_UNSIGNED_BYTE, pixels.data());
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    VRB_LOG("Wave native hand model upload=%d vertices=%u/%u alpha=%ux%u format=%d",
        ok, model->right.vertices.size / 3, model->left.vertices.size / 3, t.width, t.height, t.format);
    WVR_ReleaseNatureHandModel(&model);
    if (!ok) DeleteModels();
    glBindVertexArray(oldVao);
    glBindBuffer(GL_ARRAY_BUFFER, oldBuffer);
    glBindTexture(GL_TEXTURE_2D, oldTexture);
    glPixelStorei(GL_UNPACK_ALIGNMENT, oldUnpack);
    glActiveTexture(oldActive);
    return ok;
  }
public:
  WaveHandMesh() {
    const char* vertex = R"GLSL(#version 300 es
precision highp float;
layout(location=0) in vec3 position;
layout(location=1) in vec3 normal;
layout(location=2) in vec2 uv;
layout(location=3) in vec2 uv2;
layout(location=4) in vec4 bones;
layout(location=5) in vec4 weights;
uniform mat4 joints[48];
uniform mat4 projection;
uniform mat4 view;
out vec2 tex;
out float fade;
out float light;
void main() {
  mat4 skin = joints[int(bones.x)] * weights.x + joints[int(bones.y)] * weights.y
    + joints[int(bones.z)] * weights.z + joints[int(bones.w)] * weights.w;
  gl_Position = projection * view * skin * vec4(position, 1.0);
  vec3 n = normalize(mat3(view * skin) * normal);
  light = 0.75 + 0.25 * abs(n.z);
  tex = uv;
  fade = smoothstep(0.0, 0.85, 1.0 - uv2.y);
})GLSL";
    const char* fragment = R"GLSL(#version 300 es
precision mediump float;
uniform sampler2D alphaTex;
in vec2 tex;
in float fade;
in float light;
out vec4 color;
void main() {
  float a = texture(alphaTex, tex).r * fade * 0.65;
  if (a < 0.01) discard;
  color = vec4(mix(vec3(0.12,0.70,0.90), vec3(0.75), fade) * light, a);
})GLSL";
    GLuint v = vrb::LoadShader(GL_VERTEX_SHADER, vertex);
    GLuint f = vrb::LoadShader(GL_FRAGMENT_SHADER, fragment);
    if (v && f) program = vrb::CreateProgram(v, f);
    if (v) glDeleteShader(v);
    if (f) glDeleteShader(f);
  }
  ~WaveHandMesh() {
    if (loading.valid()) {
      auto* model = loading.get();
      if (model) WVR_ReleaseNatureHandModel(&model);
    }
    DeleteModels();
    if (program) glDeleteProgram(program);
  }
  void Update(int hand, const std::vector<vrb::Matrix>& joints, const WVR_Vector3f_t& scale, bool enabled) {
    visible[hand] = false;
    if (!enabled || joints.size() != 26 || !program || !Ready()) return;
    const auto& mesh = meshes[hand];
    const auto wristInverse = joints[1].AfineInverse();
    std::array<vrb::Matrix, 48> local{};
    std::array<int, 48> visited{};
    std::function<bool(int)> resolve = [&](int bone) {
      if (visited[bone] == 2) return true;
      if (visited[bone] == 1) return false;
      visited[bone] = 1;
      local[bone] = bone < 26 ? wristInverse.PostMultiply(joints[bone]) : mesh.localBind[bone];
      local[bone].TranslateInPlace(-local[bone].GetTranslation());
      const auto parent = mesh.parent[bone];
      const auto offset = mesh.localBind[bone].GetTranslation();
      if (parent == 47) local[bone].TranslateInPlace(offset);
      else {
        if (!resolve(parent)) return false;
        local[bone].TranslateInPlace(local[parent].MultiplyPosition(offset));
      }
      visited[bone] = 2;
      return true;
    };
    vrb::Vector s(scale.v[0], scale.v[1], scale.v[2]);
    if (!std::isfinite(s.x()) || !std::isfinite(s.y()) || !std::isfinite(s.z()) ||
        s.x() <= 0 || s.y() <= 0 || s.z() <= 0) s = vrb::Vector(1,1,1);
    const auto world = joints[1].PostMultiply(vrb::Matrix::Identity().ScaleInPlace(s));
    for (int bone = 0; bone < 48; ++bone) {
      skin[hand][bone] = world;
      if (mesh.used[bone] == 1) {
        if (!resolve(bone)) return;
        skin[hand][bone] = world.PostMultiply(local[bone]).PostMultiply(mesh.inverseBind[bone]);
      }
    }
    visible[hand] = true;
  }
  void Draw(int hand, const vrb::Camera& camera) {
    if (!visible[hand]) return;
    GLint oldProgram, oldVao, oldActive, oldTexture;
    GLboolean depthMask;
    glGetIntegerv(GL_CURRENT_PROGRAM, &oldProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &oldVao);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &oldActive);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &oldTexture);
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
    const bool cull = glIsEnabled(GL_CULL_FACE), blend = glIsEnabled(GL_BLEND);
    const bool depth = glIsEnabled(GL_DEPTH_TEST);
    // These are UI interaction hands, drawn after all browser surfaces. Do
    // not let a page's depth hide fingers when they intersect its plane.
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    SetWaveSceneAlphaBlend();
    glDepthMask(GL_FALSE);
    glUseProgram(program);
    glUniformMatrix4fv(glGetUniformLocation(program, "joints"), 48, GL_FALSE, skin[hand][0].Data());
    glUniformMatrix4fv(glGetUniformLocation(program, "projection"), 1, GL_FALSE, camera.GetPerspective().Data());
    glUniformMatrix4fv(glGetUniformLocation(program, "view"), 1, GL_FALSE, camera.GetView().Data());
    glUniform1i(glGetUniformLocation(program, "alphaTex"), 0);
    glBindTexture(GL_TEXTURE_2D, alpha);
    glBindVertexArray(meshes[hand].vao);
    glDrawElements(GL_TRIANGLES, meshes[hand].count, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(oldVao);
    glBindTexture(GL_TEXTURE_2D, oldTexture);
    glActiveTexture(oldActive);
    glUseProgram(oldProgram);
    glDepthMask(depthMask);
    if (depth) glEnable(GL_DEPTH_TEST);
    if (cull) glEnable(GL_CULL_FACE);
    if (!blend) glDisable(GL_BLEND);
  }
};
}
