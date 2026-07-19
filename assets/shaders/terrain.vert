#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in float inFeature; // matId: terrain feature height offset

layout(set = 0, binding = 0) uniform Frame {
  mat4 viewProj;
  vec4 cameraPos_time;
  vec4 sprintScore_flags;
  vec4 tiling_pad;
  mat4 invViewProj;
  mat4 prevViewProj;
  vec4 taaJitter;
  mat4 lightViewProj;
  vec4 shadowParams;
} uFrame;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUV;
layout(location = 3) out float vHeight;
layout(location = 4) out float vFeature;

void main() {
  vWorldPos = inPosition;
  vNormal = inNormal;
  vUV = inUV;
  vHeight = inPosition.y;
  vFeature = inFeature;
  gl_Position = uFrame.viewProj * vec4(inPosition, 1.0);
}
