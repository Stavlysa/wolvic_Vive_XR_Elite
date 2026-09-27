/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "WaveAlphaBlend.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2ext.h>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

static GLuint Compile(GLenum type, const char* source) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint ok = 0;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    char log[4096] = {};
    glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
    std::fprintf(stderr, "%s\n", log);
    std::abort();
  }
  return shader;
}

int main() {
  EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
  assert(eglInitialize(display, nullptr, nullptr));
  const EGLint configAttrs[] = {EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR, EGL_RED_SIZE, 8,
      EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
  EGLConfig config;
  EGLint count = 0;
  assert(eglChooseConfig(display, configAttrs, &config, 1, &count) && count == 1);
  const EGLint contextAttrs[] = {EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE};
  EGLContext context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttrs);
  assert(context != EGL_NO_CONTEXT);
  const EGLint surfaceAttrs[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
  EGLSurface surface = eglCreatePbufferSurface(display, config, surfaceAttrs);
  assert(surface != EGL_NO_SURFACE);
  assert(eglMakeCurrent(display, surface, surface, context));
  std::printf("GPU: %s\n", glGetString(GL_RENDERER));

  // Compile the exact production external-OES widget fragment shader.
  std::string fragment =
#include "shaders/widget_surface.fs"
  fragment.replace(fragment.find("VRB_FRAGMENT_PRECISION"),
                   std::string("VRB_FRAGMENT_PRECISION").size(), "highp");
  GLuint fs = Compile(GL_FRAGMENT_SHADER, fragment.c_str());
  GLuint vs = Compile(GL_VERTEX_SHADER, R"GLSL(#version 100
    attribute vec2 position;
    uniform vec4 tint;
    varying vec4 v_color;
    varying vec2 v_uv;
    void main() {
      gl_Position = vec4(position, 0.0, 1.0);
      v_color = tint;
      v_uv = vec2(0.5);
    })GLSL");
  GLuint program = glCreateProgram();
  glAttachShader(program, vs);
  glAttachShader(program, fs);
  glBindAttribLocation(program, 0, "position");
  glLinkProgram(program);
  GLint linked = 0;
  glGetProgramiv(program, GL_LINK_STATUS, &linked);
  assert(linked);
  glUseProgram(program);
  glUniform1i(glGetUniformLocation(program, "u_texture0"), 0);
  const GLint tint = glGetUniformLocation(program, "tint");
  const GLfloat vertices[] = {-1,-1, 3,-1, -1,3};
  GLuint vbo;
  glGenBuffers(1, &vbo);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
  glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
  glEnableVertexAttribArray(0);
  glViewport(0, 0, 1, 1);
  glDisable(GL_DITHER);
  glEnable(GL_BLEND);
  crow::SetWaveSceneAlphaBlend();

  const auto createImage = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(eglGetProcAddress("eglCreateImageKHR"));
  const auto destroyImage = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(eglGetProcAddress("eglDestroyImageKHR"));
  const auto imageTarget = reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(eglGetProcAddress("glEGLImageTargetTexture2DOES"));
  assert(createImage && destroyImage && imageTarget);
  using Pixel = std::array<unsigned char, 4>;
  const auto draw = [&](Pixel pixel, float opacity = 1.0f) {
    GLuint texture, external;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    const EGLint imageAttrs[] = {EGL_GL_TEXTURE_LEVEL_KHR, 0, EGL_IMAGE_PRESERVED_KHR, EGL_TRUE, EGL_NONE};
    EGLImageKHR image = createImage(display, context, EGL_GL_TEXTURE_2D_KHR,
        reinterpret_cast<EGLClientBuffer>(static_cast<uintptr_t>(texture)), imageAttrs);
    assert(image != EGL_NO_IMAGE_KHR);
    glGenTextures(1, &external);
    glBindTexture(GL_TEXTURE_EXTERNAL_OES, external);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_EXTERNAL_OES, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    imageTarget(GL_TEXTURE_EXTERNAL_OES, image);
    glUniform4f(tint, 1, 1, 1, opacity);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glFinish();
    assert(glGetError() == GL_NO_ERROR);
    glDeleteTextures(1, &external);
    assert(destroyImage(display, image));
    glDeleteTextures(1, &texture);
  };
  const auto expect = [](const char* name, Pixel expected) {
    Pixel actual{};
    glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, actual.data());
    assert(glGetError() == GL_NO_ERROR);
    for (int c = 0; c < 4; ++c) {
      if (std::abs(int(actual[c]) - int(expected[c])) > 2) {
        std::fprintf(stderr, "%s: channel %d = %u, expected %u\n", name, c, actual[c], expected[c]);
        std::abort();
      }
    }
    std::printf("PASS %s (%u,%u,%u,%u)\n", name, actual[0],actual[1],actual[2],actual[3]);
  };
  const auto clear = []() {
    glClearColor(0,0,0,0);
    glClear(GL_COLOR_BUFFER_BIT);
  };

  clear();
  expect("empty passthrough background", {0,0,0,0});
  draw({0,0,0,0});
  expect("transparent rounded corner", {0,0,0,0});
  draw({128,0,0,128});
  expect("half-covered UI edge", {128,0,0,128});
  draw({0,128,0,128});
  expect("overlapping translucent widgets", {64,128,0,192});
  clear();
  draw({32,64,96,255});
  expect("opaque web page", {32,64,96,255});
  draw({128,128,128,128});
  expect("translucent toolbar over opaque page", {144,160,176,255});
  clear();
  draw({128,0,0,128}, .5f);
  expect("widget fade applied once", {64,0,0,64});
  clear();
  draw({0,0,0,255});
  expect("opaque black web content", {0,0,0,255});

  glDeleteBuffers(1, &vbo);
  glDeleteProgram(program);
  glDeleteShader(vs);
  glDeleteShader(fs);
  eglMakeCurrent(display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
  eglDestroySurface(display, surface);
  eglDestroyContext(display, context);
  eglTerminate(display);
  puts("Wave GPU alpha blending tests passed");
}
