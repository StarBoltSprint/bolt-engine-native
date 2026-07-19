#version 450
// Depth-only sun shadow for instanced foliage (CSM)
// uPush.color.w = cascade index 0..2
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
  mat4 lightViewProj[3];
  vec4 shadowParams;
  vec4 cascadeSplits;
  vec4 cascadeOrigin;
} uFrame;

layout(set = 0, binding = 1) readonly buffer Instances {
  Instance data[];
} uInstances;

layout(push_constant) uniform Push {
  mat4 model;
  vec4 color;
  vec4 anim;
} uPush;

void main() {
  Instance inst = uInstances.data[gl_InstanceIndex];
  float yaw = inst.yawKind.x;
  float c = cos(yaw);
  float s = sin(yaw);
  float sc = inst.posScale.w;
  float morph = inst.yawKind.w;
  // Match foliage morph lightly so shadows align
  vec3 lp = inPosition * sc;
  lp.x *= mix(1.0, 0.85, morph);
  lp.y *= mix(1.0, 1.12, morph);
  lp.x += morph * 0.08 * sc * max(inPosition.y, 0.0);
  vec3 wp;
  wp.x = c * lp.x + s * lp.z + inst.posScale.x;
  wp.y = lp.y + inst.posScale.y;
  wp.z = -s * lp.x + c * lp.z + inst.posScale.z;
  int cascade = int(clamp(uPush.color.w, 0.0, 2.0) + 0.5);
  gl_Position = uFrame.lightViewProj[cascade] * vec4(wp, 1.0);
}
