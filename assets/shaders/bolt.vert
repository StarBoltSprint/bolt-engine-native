#version 450
// Bolt GSD — multi-part matrices + secondary head/neck/spine motion (always)
// fullMesh deform (anim.w=1) still handles limbs when partition unavailable

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in vec2 inUV;
layout(location = 3) in float inMatId;

layout(set = 0, binding = 0) uniform Frame {
  mat4 viewProj;
  vec4 cameraPos_time;
  vec4 sprintScore_flags;
  vec4 tiling_pad;
  mat4 invViewProj;
} uFrame;

layout(push_constant) uniform Push {
  mat4 model;
  vec4 color; // w = energy
  vec4 anim;  // x=phase 0..1, y=speed, z=hop, w=fullMeshLimbDeform
} uPush;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUV;
layout(location = 3) out float vEnergy;
layout(location = 4) out float vMatId;

const float PI = 3.14159265;

vec3 rotX(vec3 p, float a) {
  float c = cos(a), s = sin(a);
  return vec3(p.x, p.y * c - p.z * s, p.y * s + p.z * c);
}
vec3 rotY(vec3 p, float a) {
  float c = cos(a), s = sin(a);
  return vec3(p.x * c + p.z * s, p.y, -p.x * s + p.z * c);
}
vec3 rotZ(vec3 p, float a) {
  float c = cos(a), s = sin(a);
  return vec3(p.x * c - p.y * s, p.x * s + p.y * c, p.z);
}

/** Natural head + neck + chest secondary motion (body mesh includes head). */
void secondarySpineHead(inout vec3 p, inout vec3 n, float phase, float hop, float sp) {
  float gait = phase * PI * 2.0;
  float s1 = sin(gait);
  float c1 = cos(gait);
  float s2 = sin(gait * 2.0);

  // —— Neck / head (high Y, forward Z) ——
  // Mesh: head ~ y>1.15, z>0.55 after normalize
  float headMask = smoothstep(1.05, 1.35, p.y) * smoothstep(0.35, 0.85, p.z);
  headMask *= smoothstep(1.95, 1.55, p.y); // not sky-high outliers
  if (headMask > 0.01) {
    // Nod: opposite gather of body, stronger at sprint
    float nod = (-c1 * 0.16 + s1 * 0.06) * hop * (0.55 + sp * 0.5);
    // Look yaw weave
    float look = s1 * 0.11 * hop * (0.4 + sp * 0.6);
    // Slight roll with stride
    float hroll = c1 * 0.05 * hop;

    vec3 neck = vec3(0.0, 1.18, 0.72);
    vec3 d = p - neck;
    d = rotY(d, look * headMask);
    d = rotX(d, nod * headMask);
    d = rotZ(d, hroll * headMask);
    // Reach forward slightly on extend
    d.z += (-c1 * 0.04) * hop * headMask;
    d.y += (s2 * 0.02) * hop * headMask;
    p = mix(p, neck + d, headMask);
    n = normalize(mix(n, rotX(rotY(n, look * 0.5), nod * 0.5), headMask));
  }

  // —— Ears (flop) ——
  if (p.y > 1.32 && abs(p.x) > 0.07 && p.z > 0.35 && p.z < 1.45) {
    float flop = sin(gait * 2.5 + sign(p.x) * 1.7) * 0.12 * hop;
    vec3 base = vec3(p.x, 1.48, 1.02);
    float w = smoothstep(1.32, 1.55, p.y);
    vec3 d = p - base;
    d = rotX(d, flop * w);
    d = rotZ(d, -sign(p.x) * flop * 0.4 * w);
    p = base + d;
  }

  // —— Chest / withers flex (subtle squash-stretch) ——
  float chest = smoothstep(0.55, 0.95, p.y) * smoothstep(1.25, 0.95, p.y) *
                smoothstep(-0.2, 0.55, p.z) * smoothstep(0.9, 0.4, p.z);
  if (chest > 0.01) {
    // Expand on suspension, compress on contact
    float pump = s2 * 0.035 * hop;
    vec3 core = vec3(0.0, 0.95, 0.15);
    vec3 d = p - core;
    d.y *= 1.0 + pump * chest;
    d.x *= 1.0 - pump * 0.5 * chest;
    p = mix(p, core + d, chest * 0.85);
  }
}

/** Full-mesh limb animation when partition failed (anim.w > 0.5). */
void deformFullMeshLimbs(inout vec3 p, inout vec3 n, float phase, float hop, float sp) {
  float gait = phase * PI * 2.0;
  float swingAmp = 0.55 * hop;
  float liftAmp = 0.14 * hop;

  bool lowEnough = p.y < 0.98 && p.y > -0.02;
  bool outEnough = abs(p.x) > 0.06;
  bool frontLeg = lowEnough && outEnough && p.z > 0.06 && p.z < 0.95;
  bool backLeg = lowEnough && outEnough && p.z < -0.06 && p.z > -1.15;

  if (frontLeg || backLeg) {
    float side = sign(p.x + 1e-5);
    float phaseOff = 0.0;
    if (frontLeg && side < 0.0) phaseOff = PI;
    if (backLeg && side > 0.0) phaseOff = PI;
    float ph = gait + phaseOff;
    float swing = sin(ph) * swingAmp;
    float lift = max(0.0, sin(ph)) * liftAmp;
    float knee = max(0.0, -sin(ph)) * 0.25 * hop;
    vec3 hip = vec3(side * 0.18, 0.90, frontLeg ? 0.30 : -0.40);
    float w = smoothstep(hip.y + 0.05, hip.y - 0.55, p.y);
    w *= smoothstep(0.04, 0.14, abs(p.x));
    vec3 d = p - hip;
    d = rotX(d, swing * 0.75 * w);
    float lower = smoothstep(hip.y - 0.25, hip.y - 0.75, p.y);
    d = rotX(d, knee * lower * w);
    p = mix(p, hip + d + vec3(0.0, lift * w, 0.0), w);
    n = normalize(mix(n, rotX(n, swing * 0.5 * w), w));
  }

  if (p.z < -0.45 && p.y > 0.55 && p.y < 1.45 && abs(p.x) < 0.35) {
    float wag = sin(gait * (2.2 + sp)) * 0.42 * (0.25 + hop);
    vec3 base = vec3(0.0, 1.0, -0.55);
    float w = smoothstep(-0.45, -0.85, p.z);
    vec3 d = p - base;
    d = rotY(d, wag * w);
    d = rotX(d, (0.25 + sp * 0.15) * w * 0.5);
    p = mix(p, base + d, w);
  }
}

void main() {
  vec3 pos = inPosition;
  vec3 nrm = inNormal;

  float phase = uPush.anim.x;
  float sp = clamp(uPush.anim.y, 0.0, 1.5);
  float hop = clamp(uPush.anim.z, 0.0, 1.6);

  // Always: natural head/neck/chest (body includes head after import partition)
  if (hop > 0.05) {
    secondarySpineHead(pos, nrm, phase, hop, sp);
  }

  // Only when still a rigid full mesh without separate legs
  if (uPush.anim.w > 0.5) {
    deformFullMeshLimbs(pos, nrm, phase, hop, sp);
  }

  vec4 wp = uPush.model * vec4(pos, 1.0);
  vWorldPos = wp.xyz;
  vNormal = normalize(mat3(uPush.model) * nrm);
  vUV = inUV;
  vEnergy = uPush.color.w;
  vMatId = inMatId;
  gl_Position = uFrame.viewProj * wp;
}
