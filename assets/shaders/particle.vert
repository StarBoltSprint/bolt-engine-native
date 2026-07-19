#version 450
// Billboard particles — dust / pawprint / crystal / aura / near-cam dust / cloud

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
layout(location = 3) out float vCamDist;

void main() {
  Particle p = uParticles.data[gl_InstanceIndex];
  float life = clamp(p.colorLife.a, 0.0, 1.0);
  float kind = p.params.x;
  float size = p.posSize.w;

  // Life scale per kind
  if (kind > 3.5 && kind < 4.5) {
    // 4 near-cam dust — tiny, life-soft
    size *= (0.65 + 0.35 * life);
  } else if (kind > 4.5) {
    // 5 soft cloud — larger, slow breath
    float breath = 0.92 + 0.08 * sin(uFrame.cameraPos_time.w * 0.7 + p.posSize.x * 0.1);
    size *= (0.75 + 0.35 * life) * breath;
  } else if (kind > 0.5 && kind < 1.5) {
    size *= 1.15;
  } else if (kind > 1.5) {
    size *= (0.7 + 0.5 * life);
  } else {
    size *= (0.55 + 0.45 * life);
  }

  vec3 center = p.posSize.xyz;
  vec3 toCam = uFrame.cameraPos_time.xyz - center;
  float camDist = length(toCam);
  toCam = camDist > 1e-4 ? toCam / camDist : vec3(0.0, 0.0, 1.0);
  vec3 up = vec3(0.0, 1.0, 0.0);
  vec3 right = normalize(cross(up, toCam));
  if (length(right) < 1e-3) right = vec3(1.0, 0.0, 0.0);
  up = normalize(cross(toCam, right));

  // Soft depth cue: clouds slightly larger when mid-distance (scale read)
  if (kind > 4.5) {
    float mid = smoothstep(4.0, 14.0, camDist) * (1.0 - smoothstep(40.0, 70.0, camDist));
    size *= 0.9 + mid * 0.35;
  }

  // Pawprints: more ground-aligned (flatten billboard toward XZ)
  if (kind > 0.5 && kind < 1.5) {
    right = normalize(vec3(right.x, 0.0, right.z));
    if (length(right) < 1e-3) right = vec3(1.0, 0.0, 0.0);
    up = vec3(0.0, 1.0, 0.0);
    vec3 wp = center + right * (inPosition.x * size) + up * (inPosition.z * size * 0.22);
    vUV = inUV;
    vColorLife = vec4(p.colorLife.rgb, life);
    vKind = kind;
    vCamDist = camDist;
    gl_Position = uFrame.viewProj * vec4(wp, 1.0);
    return;
  }

  // Clouds: slightly flatter (pancake) for soft aerial sheets
  float yScale = (kind > 4.5) ? 0.55 : 1.0;
  vec3 wp = center + right * (inPosition.x * size) + up * (inPosition.z * size * yScale);
  vUV = inUV;
  vColorLife = vec4(p.colorLife.rgb, life);
  vKind = kind;
  vCamDist = camDist;
  gl_Position = uFrame.viewProj * vec4(wp, 1.0);
}
