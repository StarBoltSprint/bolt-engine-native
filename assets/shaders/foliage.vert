#version 450
layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

struct Instance {
  vec4 posScale;
  vec4 yawKind;
};

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

layout(set = 0, binding = 1) readonly buffer Instances {
  Instance data[];
} uInstances;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUV;
layout(location = 3) out float vKind;
layout(location = 4) out float vColorSeed;

void main() {
  Instance inst = uInstances.data[gl_InstanceIndex];
  float yaw = inst.yawKind.x;
  float c = cos(yaw);
  float s = sin(yaw);
  float sc = inst.posScale.w;
  float morph = clamp(inst.yawKind.w, 0.0, 1.0);
  float colorSeed = inst.yawKind.z;

  // Soft wind / resonance sway — trees sway more, floaters bob, stalks shimmer
  float t = uFrame.cameraPos_time.w;
  float phase = inst.posScale.x * 0.17 + inst.posScale.z * 0.13;
  float kind = inst.yawKind.y;
  float swayAmt = 0.035;
  if (kind > 9.5) swayAmt = 0.008;
  else if (kind > 4.5) swayAmt = 0.018;
  else if (kind > 3.5) swayAmt = 0.055;
  else if (kind > 2.5) swayAmt = 0.042;
  float bend = sin(t * 1.4 + phase) * swayAmt * sc * max(inPosition.y, 0.0);
  // Mesh morph: lean / squash so clones don't look identical
  vec3 lp = inPosition * sc;
  lp.x *= mix(1.0, 0.82, morph);
  lp.y *= mix(1.0, 1.18, morph * 0.85);
  lp.z *= mix(1.0, 0.9, morph * 0.5);
  lp.x += morph * 0.12 * sc * max(inPosition.y, 0.0); // permanent lean
  lp.x += bend;
  lp.z += cos(t * 1.1 + phase * 1.3) * (swayAmt * 0.55) * sc * max(inPosition.y, 0.0);
  if (kind > 4.5 && kind < 9.5) {
    lp.y += sin(t * 2.2 + phase * 2.0) * 0.08 * sc;
  }

  vec3 wp;
  wp.x = c * lp.x + s * lp.z + inst.posScale.x;
  wp.y = lp.y + inst.posScale.y;
  wp.z = -s * lp.x + c * lp.z + inst.posScale.z;

  vec3 ln = inNormal;
  vec3 wn;
  wn.x = c * ln.x + s * ln.z;
  wn.y = ln.y;
  wn.z = -s * ln.x + c * ln.z;

  float dist = length(uFrame.cameraPos_time.xyz - wp);
  float lod = clamp(1.0 - dist * 0.007, 0.25, 1.0);
  wp = mix(inst.posScale.xyz + vec3(0.0, lp.y, 0.0), wp, lod);

  vWorldPos = wp;
  vNormal = normalize(wn);
  vUV = inUV;
  vKind = inst.yawKind.y;
  vColorSeed = colorSeed;
  gl_Position = uFrame.viewProj * vec4(wp, 1.0);
}
