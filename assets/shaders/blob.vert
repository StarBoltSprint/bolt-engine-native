#version 450
// Soft contact-shadow disc under each stalk instance

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
} uFrame;

layout(set = 0, binding = 1) readonly buffer Instances {
  Instance data[];
} uInstances;

layout(location = 0) out vec2 vUV;
layout(location = 1) out float vScale;

void main() {
  Instance inst = uInstances.data[gl_InstanceIndex];
  float kind = inst.yawKind.y;
  // Ruins get wide soft contact discs; veg stays tight
  float mul = (kind > 9.5) ? 0.55 : 1.55;
  float sc = max(inst.posScale.w, 0.35) * mul;
  if (kind > 9.5) sc = clamp(sc, 2.5, 14.0);
  float dist = length(uFrame.cameraPos_time.xz - inst.posScale.xz);
  sc *= mix(1.0, 0.75, smoothstep(40.0, 120.0, dist));

  vec3 wp = inst.posScale.xyz;
  // Ruins may float — shadow still on ground-ish (use low Y for floating arches)
  float baseY = (kind > 10.5 && kind < 11.5) ? (inst.posScale.y - 1.5) : inst.posScale.y;
  wp.y = baseY + 0.05;
  wp.x += inPosition.x * sc;
  wp.z += inPosition.z * sc;

  vUV = inUV;
  vScale = sc;
  gl_Position = uFrame.viewProj * vec4(wp, 1.0);
}
