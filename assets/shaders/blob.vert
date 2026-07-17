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
  float sc = max(inst.posScale.w, 0.35) * 1.55;
  // Flatten slightly with distance (cheaper look when far)
  float dist = length(uFrame.cameraPos_time.xz - inst.posScale.xz);
  sc *= mix(1.0, 0.75, smoothstep(40.0, 120.0, dist));

  vec3 wp = inst.posScale.xyz;
  wp.y = inst.posScale.y + 0.04; // sit just above terrain sample height of stalk base
  wp.x += inPosition.x * sc;
  wp.z += inPosition.z * sc;

  vUV = inUV;
  vScale = sc;
  gl_Position = uFrame.viewProj * vec4(wp, 1.0);
}
