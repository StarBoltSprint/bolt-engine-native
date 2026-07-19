#version 450
// Crystal Nebula sky dome — shared eval (forward path)
#include "sky_eval.glsl"

layout(location = 0) in vec2 vUV;

layout(set = 0, binding = 0) uniform Frame {
  mat4 viewProj;
  vec4 cameraPos_time;
  // x=score y=matFlags z=camSpeed w=skyEnergy (SkyGenerator pack)
  vec4 sprintScore_flags;
  vec4 tiling_pad;
  mat4 invViewProj;
} uFrame;

layout(location = 0) out vec4 outColor;

void main() {
  vec3 rd = skyRayFromInvVP(uFrame.invViewProj, vUV);
  float t = uFrame.cameraPos_time.w;
  float score = uFrame.sprintScore_flags.x;
  float skyE = uFrame.sprintScore_flags.w;
  outColor = vec4(evaluateCrystalSky(rd, t, score, skyE, vUV), 1.0);
}
