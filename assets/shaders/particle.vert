#version 450
// Billboard particles — dust / pawprint / crystal / aura (Path 5 pack FX)

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;

struct Particle {
  vec4 posSize;   // xyz, w=size
  vec4 colorLife; // rgb, a=life 0..1
  vec4 params;    // x=kind
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
layout(location = 2) flat out float vKind;

void main() {
  Particle p = uParticles.data[gl_InstanceIndex];
  float life = clamp(p.colorLife.a, 0.0, 1.0);
  float kind = p.params.x;
  float size = p.posSize.w * (0.55 + 0.45 * life);
  // Pawprints stay flatter/larger; sparks shrink with life
  if (kind > 0.5 && kind < 1.5) size *= 1.15;
  if (kind > 1.5) size *= (0.7 + 0.5 * life);

  vec3 center = p.posSize.xyz;
  vec3 toCam = normalize(uFrame.cameraPos_time.xyz - center);
  vec3 up = vec3(0.0, 1.0, 0.0);
  vec3 right = normalize(cross(up, toCam));
  if (length(right) < 1e-3) right = vec3(1.0, 0.0, 0.0);
  up = normalize(cross(toCam, right));

  // Pawprints: more ground-aligned (flatten billboard toward XZ)
  if (kind > 0.5 && kind < 1.5) {
    right = normalize(vec3(right.x, 0.0, right.z));
    if (length(right) < 1e-3) right = vec3(1.0, 0.0, 0.0);
    up = vec3(0.0, 1.0, 0.0);
    // thin vertical extent
    vec3 wp = center + right * (inPosition.x * size) + up * (inPosition.z * size * 0.22);
    vUV = inUV;
    vColorLife = vec4(p.colorLife.rgb, life);
    vKind = kind;
    gl_Position = uFrame.viewProj * vec4(wp, 1.0);
    return;
  }

  vec3 wp = center + right * (inPosition.x * size) + up * (inPosition.z * size);
  vUV = inUV;
  vColorLife = vec4(p.colorLife.rgb, life);
  vKind = kind;
  gl_Position = uFrame.viewProj * vec4(wp, 1.0);
}
