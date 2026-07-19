#version 450
// Depth-only sun shadow (CSM). Terrain/path verts are world-space.
// uPush.color.w = cascade index 0..2
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(set = 0, binding = 0) uniform Frame {
  mat4 viewProj;
  vec4 cameraPos_time;
  vec4 sprintScore_flags;
  vec4 tiling_pad;
  mat4 invViewProj;
  mat4 prevViewProj;
  vec4 taaJitter;
  mat4 lightViewProj[3];
  vec4 shadowParams;
  vec4 cascadeSplits;
  vec4 cascadeOrigin;
} uFrame;

layout(push_constant) uniform Push {
  mat4 model;
  vec4 color;
  vec4 anim;
} uPush;

void main() {
  vec4 wp = uPush.model * vec4(inPosition, 1.0);
  int c = int(clamp(uPush.color.w, 0.0, 2.0) + 0.5);
  gl_Position = uFrame.lightViewProj[c] * wp;
}
