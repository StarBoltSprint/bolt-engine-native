#version 450
// White Bolt character — model via push constants

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(set = 0, binding = 0) uniform Frame {
  mat4 viewProj;
  vec4 cameraPos_time;
  vec4 sprintScore_flags;
  vec4 tiling_pad;
  mat4 invViewProj;
} uFrame;

layout(push_constant) uniform Push {
  mat4 model;
  vec4 color; // rgb + emissive boost
} uPush;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec3 vColor;

void main() {
  vec4 wp = uPush.model * vec4(inPosition, 1.0);
  vWorldPos = wp.xyz;
  vNormal = mat3(uPush.model) * inNormal;
  vColor = uPush.color.rgb;
  gl_Position = uFrame.viewProj * wp;
}
