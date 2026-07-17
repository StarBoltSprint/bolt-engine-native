#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(set = 0, binding = 0) uniform Frame {
  mat4 viewProj;
  vec4 cameraPos_time;
  vec4 sprintScore_flags;
  vec4 tiling_pad;
} uFrame;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUV;
layout(location = 3) out float vHeight;

void main() {
  vWorldPos = inPosition;
  vNormal = inNormal;
  vUV = inUV;
  vHeight = inPosition.y;
  gl_Position = uFrame.viewProj * vec4(inPosition, 1.0);
}
