#version 450
// Crystal terrain — GPU height (optional) mirrors HeightField.cpp constants

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

layout(set = 0, binding = 0) uniform Frame {
  mat4 viewProj;
  vec4 cameraPos_time;
  vec4 sprintScore_pad; // x = score
} uFrame;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUV;
layout(location = 3) out float vHeight;

// TODO: paste valueNoise/fbm/domainWarp matching HeightField.cpp

void main() {
  vec3 pos = inPosition;
  // if (uFrame.sprintScore_pad.y > 0.5) pos.y = boltTerrainHeight(pos.xz, uFrame.sprintScore_pad.x);
  vHeight = pos.y;
  vWorldPos = pos;
  vNormal = inNormal;
  vUV = inUV;
  gl_Position = uFrame.viewProj * vec4(pos, 1.0);
}
