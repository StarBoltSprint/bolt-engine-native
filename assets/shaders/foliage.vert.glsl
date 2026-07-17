#version 450
// GPU instanced Crystal stalks — instance buffer: pos.xyz, scale, yaw, kind

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;

struct Instance {
  vec4 posScale; // xyz position, w scale
  vec4 yawKind;  // x yaw, y kind
};

layout(set = 0, binding = 1) readonly buffer Instances {
  Instance data[];
} uInstances;

layout(set = 0, binding = 0) uniform Frame {
  mat4 viewProj;
  vec4 cameraPos_lodBias;
} uFrame;

layout(location = 0) out vec3 vNormal;
layout(location = 1) out float vKind;

void main() {
  Instance inst = uInstances.data[gl_InstanceIndex];
  float yaw = inst.yawKind.x;
  float c = cos(yaw), s = sin(yaw);
  vec3 lp = inPosition * inst.posScale.w;
  vec3 wp = vec3(c * lp.x + s * lp.z, lp.y, -s * lp.x + c * lp.z) + inst.posScale.xyz;
  // LOD: shrink distant when sprint lodBias high
  float dist = length(uFrame.cameraPos_lodBias.xyz - wp);
  float lod = clamp(1.0 - dist * (0.01 + uFrame.cameraPos_lodBias.w), 0.15, 1.0);
  wp = mix(inst.posScale.xyz, wp, lod);
  vNormal = inNormal;
  vKind = inst.yawKind.y;
  gl_Position = uFrame.viewProj * vec4(wp, 1.0);
}
