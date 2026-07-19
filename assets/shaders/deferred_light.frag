#version 450
// Deferred lighting: sample GBuffer + depth, apply IBL + sun + crystal lights
// Empty depth uses full SkyGenerator sky (sky_eval.glsl) — not flat envSkyColor
#include "common_pbr.glsl"
#include "sky_eval.glsl"

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

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

layout(set = 0, binding = 1) uniform sampler2D uGAlbedo; // rgb albedo, a metal
layout(set = 0, binding = 2) uniform sampler2D uGNormal; // rgb normal, a rough
layout(set = 0, binding = 3) uniform sampler2D uGDepth;
layout(set = 0, binding = 4) uniform sampler2D uGEmit;   // rgb emissive
// Note: deferred light set is separate; shadow via lightViewProj if we add sampler later

struct CrystalLight {
  vec4 posRange;
  vec4 colorIntensity;
};
layout(std430, set = 0, binding = 5) readonly buffer CrystalLights {
  uint lightCount;
  uint _pad0;
  uint _pad1;
  uint _pad2;
  CrystalLight lights[];
} uLights;

vec3 reconstructWorld(vec2 uv, float depth) {
  vec4 ndc = vec4(uv * 2.0 - 1.0, depth, 1.0);
  // Vulkan NDC y is down; invViewProj expects clip
  vec4 w = uFrame.invViewProj * ndc;
  return w.xyz / max(w.w, 1e-6);
}

vec3 evalLights(vec3 wp, vec3 N, vec3 V, vec3 albedo, float rough, float metal) {
  vec3 sum = vec3(0.0);
  uint n = min(uLights.lightCount, 64u);
  vec3 F0 = mix(vec3(0.04), albedo, metal);
  for (uint i = 0u; i < n; ++i) {
    vec3 Lpos = uLights.lights[i].posRange.xyz;
    float range = max(uLights.lights[i].posRange.w, 0.5);
    vec3 toL = Lpos - wp;
    float dist = length(toL);
    float atten = 1.0 - smoothstep(range * 0.5, range, dist);
    if (atten < 0.004) continue;
    vec3 L = toL / max(dist, 1e-3);
    float NdL = max(dot(N, L), 0.0);
    float wrap = clamp((dot(N, L) + 0.35) / 1.35, 0.0, 1.0);
    vec3 col = uLights.lights[i].colorIntensity.rgb;
    float inten = uLights.lights[i].colorIntensity.w;
    float fall = atten * atten * (1.0 / (1.0 + dist * dist * 0.012));
    vec3 H = normalize(L + V);
    float NdH = max(dot(N, H), 0.0);
    float a = max(rough * rough, 0.02);
    float a2 = a * a;
    float dggx = a2 / max(3.14159 * pow(NdH * NdH * (a2 - 1.0) + 1.0, 2.0), 1e-4);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metal);
    sum += (kD * albedo * (0.55 * NdL + 0.35 * wrap) + F * dggx * 0.5) * col * inten * fall;
  }
  return sum;
}

void main() {
  float depth = texture(uGDepth, vUV).r;
  // Sky / empty — full Crystal Nebula sky (same as sky.frag / SkyGenerator)
  if (depth >= 0.9995) {
    vec3 rd = skyRayFromInvVP(uFrame.invViewProj, vUV);
    // Fallback if invVP degenerate
    if (any(isnan(rd)) || length(rd) < 0.01) {
      vec4 clip = vec4(vUV * 2.0 - 1.0, 1.0, 1.0);
      vec4 w = uFrame.invViewProj * clip;
      rd = normalize(w.xyz / max(w.w, 1e-6) - uFrame.cameraPos_time.xyz);
    }
    float t = uFrame.cameraPos_time.w;
    float score = uFrame.sprintScore_flags.x;
    float skyE = uFrame.sprintScore_flags.w;
    // No extra screen vignette here (post has its own)
    outColor = vec4(evaluateCrystalSky(rd, t, score, skyE, vec2(0.5)), 1.0);
    return;
  }

  vec4 albM = texture(uGAlbedo, vUV);
  vec4 nR = texture(uGNormal, vUV);
  vec3 emit = texture(uGEmit, vUV).rgb;
  vec3 albedo = albM.rgb;
  float metal = albM.a;
  vec3 N = normalize(nR.rgb * 2.0 - 1.0);
  float rough = nR.a;

  vec3 wp = reconstructWorld(vUV, depth);
  vec3 V = normalize(uFrame.cameraPos_time.xyz - wp);

  vec3 L1 = normalize(vec3(0.35, 0.88, 0.35));
  float ndl = max(dot(N, L1), 0.0);
  float wrap = clamp((dot(N, L1) + 0.12) / 1.12, 0.0, 1.0);
  wrap *= wrap;

  // Deeper ground shade (proxy contact darkening without full shadow map in this pass)
  float groundDark = mix(0.32, 0.98, clamp(N.y * 0.55 + 0.2, 0.0, 1.0));
  // Slope AO punch
  float slopeAo = mix(0.55, 1.0, clamp(N.y, 0.0, 1.0));

  // Linear HDR — tonemap only in post; stronger key contrast
  vec3 col = evalIBL(N, V, albedo, rough, metal) * 0.42 * groundDark * slopeAo;
  col += albedo * vec3(0.98, 0.92, 0.9) * (0.42 * ndl + 0.08 * wrap) * (1.0 - metal * 0.4) * slopeAo;
  col += evalLights(wp, N, V, albedo, rough, metal) * 0.7;
  col += emit * 0.75;

  float dist = length(uFrame.cameraPos_time.xyz - wp);
  // Fog pull-back: clear near field, haze only past ~40m
  float fog = 1.0 - exp(-max(0.0, dist - 40.0) * 0.00105);
  fog = clamp(fog, 0.0, 0.65);
  vec3 fogDir = normalize(-V);
  vec3 fogCol = evaluateCrystalSky(fogDir, uFrame.cameraPos_time.w,
                                   uFrame.sprintScore_flags.x, uFrame.sprintScore_flags.w,
                                   vec2(0.5)) * 0.5;
  col = mix(col, fogCol, fog * 0.42);

  outColor = vec4(col, 1.0);
}
