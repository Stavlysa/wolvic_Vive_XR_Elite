/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */
#include "WaveHandMesh.h"
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <cassert>
#include <atomic>
#include <thread>

namespace vrb {
GLuint LoadShader(GLenum type, const char* source) {
  GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr); glCompileShader(shader);
  GLint ok; glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) { char log[4096]; glGetShaderInfoLog(shader, sizeof(log), nullptr, log); std::fprintf(stderr, "%s", log); }
  assert(ok); return shader;
}
GLuint CreateProgram(GLuint v, GLuint f) {
  GLuint p = glCreateProgram(); glAttachShader(p, v); glAttachShader(p, f); glLinkProgram(p);
  GLint ok; glGetProgramiv(p, GL_LINK_STATUS, &ok); assert(ok); return p;
}
}
static std::atomic<int> releases{0};
extern "C" WVR_Result WVR_GetCurrentNaturalHandModel(WVR_HandRenderModel_t** out) {
  static float vertices[] = {-1,-1,0, 3,-1,0, -1,3,0};
  static float normals[] = {0,0,1, 0,0,1, 0,0,1};
  static float uv[] = {0,0, 0,0, 0,0};
  static uint32_t bones[] = {1,47,47,47, 1,47,47,47, 1,47,47,47};
  static float weights[] = {1,0,0,0, 1,0,0,0, 1,0,0,0};
  static uint32_t indices[] = {0,1,2};
  static uint8_t pixels[] = {255,255,255,255};
  *out = new WVR_HandRenderModel_t{};
  for (auto* m : {&(*out)->right, &(*out)->left}) {
    m->vertices = {vertices,9,3}; m->normals = {normals,9,3};
    m->texCoords = m->texCoord2s = {uv,6,2};
    m->boneIDs = {bones,12,4}; m->boneWeights = {weights,12,4}; m->indices = {indices,3,3};
    for (int i=0; i<48; ++i) {
      m->jointParentTable[i] = 47;
      const auto identity = vrb::Matrix::Identity();
      std::memcpy(m->jointInvTransMats + i*16, identity.Data(), 64);
      std::memcpy(m->jointLocalTransMats + i*16, identity.Data(), 64);
    }
    m->jointUsageTable[1] = 1;
  }
  (*out)->handAlphaTex = {pixels,1,1,4,1};
  return WVR_Success;
}
extern "C" void WVR_ReleaseNatureHandModel(WVR_HandRenderModel_t** model) {
  delete *model; *model=nullptr; ++releases;
}
struct Camera : vrb::Camera {
  vrb::Matrix identity = vrb::Matrix::Identity();
  const vrb::Matrix& GetTransform() const override { return identity; }
  const vrb::Matrix& GetView() const override { return identity; }
  const vrb::Matrix& GetPerspective() const override { return identity; }
};
int main() {
  EGLDisplay display=eglGetDisplay(EGL_DEFAULT_DISPLAY);
  assert(eglInitialize(display,nullptr,nullptr));
  EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES3_BIT_KHR,
    EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_DEPTH_SIZE,16,EGL_NONE};
  EGLConfig config; EGLint count;
  assert(eglChooseConfig(display,attrs,&config,1,&count) && count);
  EGLint ctxAttrs[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
  EGLContext ctx=eglCreateContext(display,config,EGL_NO_CONTEXT,ctxAttrs);
  EGLint surfaceAttrs[]={EGL_WIDTH,1,EGL_HEIGHT,1,EGL_NONE};
  EGLSurface surface=eglCreatePbufferSurface(display,config,surfaceAttrs);
  assert(eglMakeCurrent(display,surface,surface,ctx));
  glViewport(0,0,1,1); glClearColor(0,0,0,0);
  // Simulate a web surface nearer than every hand vertex. The foreground
  // hand pass must ignore this depth but restore depth testing afterward.
  glEnable(GL_DEPTH_TEST); glDepthFunc(GL_LESS); glClearDepthf(0);
  Camera camera;
  std::vector<vrb::Matrix> joints(26,vrb::Matrix::Identity());
  WVR_Vector3f_t scale{{1,1,1}};
  {
    crow::WaveHandMesh renderer;
    std::array<uint8_t,4> pixel{};
    for (int attempt=0; attempt<200; ++attempt) {
      renderer.Update(0,joints,scale,true);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); renderer.Draw(0,camera);
      glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel.data());
      if (pixel[3]) break;
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    assert(releases==1 && pixel[3]>=160 && pixel[3]<=170);
    assert(pixel[0]>=120 && pixel[0]<=130);
    assert(glIsEnabled(GL_DEPTH_TEST));
    GLboolean depthMask; glGetBooleanv(GL_DEPTH_WRITEMASK,&depthMask); assert(depthMask);
    renderer.Update(1,joints,scale,true);
    glClear(GL_COLOR_BUFFER_BIT); renderer.Draw(1,camera);
    glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel.data());
    assert(pixel[3]>=160);
    renderer.Update(1,joints,scale,false);
    glClear(GL_COLOR_BUFFER_BIT); renderer.Draw(1,camera);
    glReadPixels(0,0,1,1,GL_RGBA,GL_UNSIGNED_BYTE,pixel.data());
    assert(pixel[3]==0);
    assert(glGetError()==GL_NO_ERROR);
  }
  std::puts("PASS: native hand shader, both meshes, upload/release, alpha, foreground occlusion and GL state restoration");
  eglMakeCurrent(display,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
  eglDestroySurface(display,surface); eglDestroyContext(display,ctx); eglTerminate(display);
}
