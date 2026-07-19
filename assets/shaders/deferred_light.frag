#version 450
// Deferred lighting: GBuffer albedo/normal/emit + depth
// Real SSAO from G-buffer normals + depth; volumetric fog / light shafts
#include "common_pbr.glsl"
#include "sky_eval.glsl"
#include "atmos_eval.glsl"

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
  mat4 lightViewProj[3];
  vec4 shadowParams;
  vec4 cascadeSplits;
  vec4 cascadeOrigin;
} uFrame;

layout(set = 0, binding = 1) uniform sampler2D uGAlbedo; // rgb albedo, a metal
layout(set = 0, binding = 2) uniform sampler2D uGNormal; // rgb normal 0-1, a rough
layout(set = 0, binding = 3) uniform sampler2D uGDepth;
layout(set = 0, binding = 4) uniform sampler2D uGEmit;
// Bound when CSM ready; sampling gated by shadowParams.z
layout(set = 0, binding = 6) uniform sampler2DArray uShadowMap;

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
  vec4 w = uFrame.invViewProj * ndc;
  return w.xyz / max(w.w, 1e-6);
}

vec2 projectUV(vec3 world) {
  vec4 c = uFrame.viewProj * vec4(world, 1.0);
  vec3 ndc = c.xyz / max(c.w, 1e-6);
  return ndc.xy * 0.5 + 0.5;
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

/**
 * Real SSAO: hemisphere samples oriented by G-buffer normal, occluded by depth buffer.
 * World-space kernel projected to UV — proper contact under rocks/ridges.
 */
float computeSSAO(vec2 uv, vec3 N, vec3 wp, float depth, float time) {
  // Build TBN
  vec3 up = abs(N.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
  vec3 T = normalize(cross(up, N));
  vec3 B = cross(N, T);

  float camDist = length(uFrame.cameraPos_time.xyz - wp);
  // Radius shrinks with distance so far ground isn't noisy
  float radius = mix(1.35, 0.55, smoothstep(8.0, 80.0, camDist));
  float occ = 0.0;
  float wsum = 0.0;
  const int K = 16;
  float seed = atmosHash21(uv * 900.0 + time * 0.15);

  for (int i = 0; i < K; ++i) {
    float fi = float(i);
    // Cosine-weighted hemisphere (Hammersley-ish spiral)
    float u1 = fract(seed + fi * 0.6180339887);
    float u2 = fract(seed * 1.7 + fi * 0.381966);
    float phi = 6.2831853 * u1;
    float cosT = sqrt(1.0 - u2);
    float sinT = sqrt(u2);
    vec3 hemi = vec3(cos(phi) * sinT, sin(phi) * sinT, cosT);
    vec3 dir = normalize(T * hemi.x + B * hemi.y + N * hemi.z);
    float r = radius * (0.12 + 0.88 * u2 * u2); // bias samples near surface
    vec3 samplePos = wp + dir * r;

    vec2 suv = projectUV(samplePos);
    if (suv.x < 0.0 || suv.x > 1.0 || suv.y < 0.0 || suv.y > 1.0) {
      wsum += 1.0;
      continue;
    }
    float sd = texture(uGDepth, suv).r;
    if (sd >= 0.9995) {
      // Sky sample → no occlusion
      wsum += 1.0;
      continue;
    }
    vec3 sp = reconstructWorld(suv, sd);
    vec3 v = sp - wp;
    float dist = length(v);
    float nd = max(dot(N, v / max(dist, 1e-4)), 0.0);
    // Range check: only nearby occluders
    float range = 1.0 - smoothstep(radius * 0.15, radius * 1.15, dist);
    // Occluder if sample is closer to camera than the shaded surface
    float sampleCamDist = length(uFrame.cameraPos_time.xyz - sp);
    float closer = sampleCamDist < camDist - 0.02 ? 1.0 : 0.0;
    occ += nd * range * closer;
    wsum += 1.0;
  }

  float ao = 1.0 - clamp(occ / max(wsum, 1.0) * 2.15, 0.0, 0.88);
  // Soften slightly on very distant ground
  ao = mix(ao, 1.0, smoothstep(90.0, 160.0, camDist) * 0.35);
  return ao;
}

void main() {
  float depth = texture(uGDepth, vUV).r;
  float t = uFrame.cameraPos_time.w;
  float score = uFrame.sprintScore_flags.x;
  float skyE = uFrame.sprintScore_flags.w;
  vec3 cam = uFrame.cameraPos_time.xyz;
  vec3 sunDir = normalize(vec3(0.35, 0.88, 0.35));

  // Sky / empty — full Crystal Nebula + volumetric haze along view ray
  if (depth >= 0.9995) {
    vec3 rd = skyRayFromInvVP(uFrame.invViewProj, vUV);
    if (any(isnan(rd)) || length(rd) < 0.01) {
      vec4 clip = vec4(vUV * 2.0 - 1.0, 1.0, 1.0);
      vec4 w = uFrame.invViewProj * clip;
      rd = normalize(w.xyz / max(w.w, 1e-6) - cam);
    }
    vec3 sky = evaluateCrystalSky(rd, t, score, skyE, vec2(0.5));
    // March haze into the sky (depth between layers, light shafts)
    // Short/light sky march — was 160m @ 12 steps (purple wall)
    sky = applyVolumetricFog(sky, cam, rd, 70.0, sunDir, sky * 0.4, t, score, 6);
    outColor = vec4(sky, 1.0);
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
  vec3 V = normalize(cam - wp);
  vec3 rd = -V; // camera → surface

  // —— Real SSAO (G-buffer normals + depth) ——
  float ao = computeSSAO(vUV, N, wp, depth, t);
  // Extra crevice darken when normal faces down/side (under rocks)
  float crevice = mix(0.72, 1.0, clamp(N.y * 0.65 + 0.35, 0.0, 1.0));
  ao *= crevice;

  vec3 L1 = sunDir;
  float ndl = max(dot(N, L1), 0.0);
  float wrap = clamp((dot(N, L1) + 0.12) / 1.12, 0.0, 1.0);
  wrap *= wrap;

  // Optional CSM if bound (binding 6); fall back lit when disabled
  float sh = 1.0;
  if (uFrame.shadowParams.z > 0.5) {
    sh = sampleShadowCSM(uShadowMap, uFrame.lightViewProj[0], uFrame.lightViewProj[1],
                         uFrame.lightViewProj[2], uFrame.cascadeOrigin.xyz,
                         uFrame.cascadeSplits.xyz, wp, N, uFrame.shadowParams.x,
                         uFrame.shadowParams.w);
    sh = mix(1.0, sh, uFrame.shadowParams.y);
  }

  vec3 col = evalIBL(N, V, albedo, rough, metal) * 0.40 * ao;
  col += albedo * vec3(0.98, 0.92, 0.9) * (0.48 * ndl + 0.08 * wrap) * (1.0 - metal * 0.4) * sh * ao;
  col += evalLights(wp, N, V, albedo, rough, metal) * 0.7 * mix(0.55, 1.0, ao);
  col += emit * 0.75;

  // —— Volumetric fog / light shafts (depth-aware, wraps surface) ——
  float dist = length(cam - wp);
  vec3 fogDir = rd;
  vec3 fogSky = evaluateCrystalSky(fogDir, t, score, skyE, vec2(0.5)) * 0.35;
  col = applyVolumetricFog(col, cam, rd, dist, sunDir, fogSky, t, score, 6);

  outColor = vec4(col, 1.0);
}
