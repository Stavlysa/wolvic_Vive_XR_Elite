R"SHADER(
#version 100
#extension GL_OES_EGL_image_external : require
precision VRB_FRAGMENT_PRECISION float;

uniform samplerExternalOES u_texture0;
varying vec4 v_color;
varying vec2 v_uv;

void main() {
  // Android widget surfaces carry premultiplied RGB. Convert to the straight
  // RGB expected by the scene blend function, before applying the widget tint.
  // Keep alpha coverage, including rounded corners and antialiased edges.
  vec4 color = texture2D(u_texture0, v_uv);
  color.rgb = color.a > 0.0 ? color.rgb / color.a : vec3(0.0);
  gl_FragColor = color * v_color;
}
)SHADER";
