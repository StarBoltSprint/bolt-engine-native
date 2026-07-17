#version 450
// Billboard dust / trail particles (unit quad + instance SSBO)

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

struct Particle {
  vec4 posSize;   // xyz, w=size
  vec4 colorLife; // rgb, a=life 0..1
};

layout(set = 0, binding = 0) uniform Frame {
  mat4 viewProj;
  vec4 cameraPos_time;
  vec4 sprintScore_flags;
  vec4 tiling_pad;
  mat4 invViewProj;
} uFrame;

layout(set = 0, binding = 14) readonly buffer Particles {
  Particle data[];
} uParticles;

layout(location = 0) out vec2 vUV;
layout(location = 1) out vec4 vColorLife;

void main() {
  Particle p = uParticles.data[gl_InstanceIndex];
  float life = clamp(p.colorLife.a, 0.0, 1.0);
  float size = p.posSize.w * (0.55 + 0.45 * life);

  vec3 center = p.posSize.xyz;
  vec3 toCam = normalize(uFrame.cameraPos_time.xyz - center);
  vec3 up = vec3(0.0, 1.0, 0.0);
  vec3 right = normalize(cross(up, toCam));
  // if looking straight down/up, pick another axis
  if (length(right) < 1e-3) right = vec3(1.0, 0.0, 0.0);
  up = normalize(cross(toCam, right));

  // inPosition.xz of unit quad is -1..1
  vec3 wp = center + right * (inPosition.x * size) + up * (inPosition.z * size);

  vUV = inUV;
  vColorLife = vec4(p.colorLife.rgb, life);
  gl_Position = uFrame.viewProj * vec4(wp, 1.0);
}
