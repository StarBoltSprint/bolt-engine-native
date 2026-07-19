#version 450
// GBuffer terrain — material only (lighting in deferred_light.frag)
#include "common_pbr.glsl"

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;
layout(location = 3) in float vHeight;
layout(location = 4) in float vFeature;

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

layout(set = 0, binding = 2) uniform sampler2D uGroundAlbedo;
layout(set = 0, binding = 3) uniform sampler2D uGroundNormal;
layout(set = 0, binding = 4) uniform sampler2D uGroundRough;
layout(set = 0, binding = 5) uniform sampler2D uRockAlbedo;
layout(set = 0, binding = 6) uniform sampler2D uRockNormal;
layout(set = 0, binding = 7) uniform sampler2D uRockRough;
layout(set = 0, binding = 8) uniform sampler2D uPathAlbedo;
layout(set = 0, binding = 9) uniform sampler2D uPathNormal;
layout(set = 0, binding = 10) uniform sampler2D uPathRough;
layout(set = 0, binding = 19) uniform sampler2D uGroundEmit;
layout(set = 0, binding = 20) uniform sampler2D uRockEmit;
layout(set = 0, binding = 21) uniform sampler2D uPathEmit;

layout(location = 0) out vec4 outAlbedoMetal; // rgb albedo, a metal
layout(location = 1) out vec4 outNormalRough; // rgb normal 0-1, a rough
layout(location = 2) out vec4 outEmit;        // rgb emissive

vec3 triplanarWeights(vec3 n) {
  vec3 b = pow(abs(normalize(n)), vec3(4.0));
  return b / (b.x + b.y + b.z + 1e-4);
}
vec3 triplanarAlbedo(sampler2D tex, vec3 n, vec3 wp, float scale) {
  vec3 b = triplanarWeights(n);
  return texture(tex, wp.zy * scale).rgb * b.x
       + texture(tex, wp.xz * scale).rgb * b.y
       + texture(tex, wp.xy * scale).rgb * b.z;
}
vec3 triplanarORM(sampler2D tex, vec3 n, vec3 wp, float scale) {
  vec3 b = triplanarWeights(n);
  return texture(tex, wp.zy * scale).rgb * b.x
       + texture(tex, wp.xz * scale).rgb * b.y
       + texture(tex, wp.xy * scale).rgb * b.z;
}
vec3 triplanarNormal(sampler2D tex, vec3 geomN, vec3 wp, float scale) {
  vec3 b = triplanarWeights(geomN);
  vec3 nx = texture(tex, wp.zy * scale).xyz * 2.0 - 1.0;
  vec3 ny = texture(tex, wp.xz * scale).xyz * 2.0 - 1.0;
  vec3 nz = texture(tex, wp.xy * scale).xyz * 2.0 - 1.0;
  vec3 nLarge = normalize(nx * b.x + ny * b.y + nz * b.z);
  // Fine detail octave
  vec3 nx2 = texture(tex, wp.zy * scale * 5.5).xyz * 2.0 - 1.0;
  vec3 ny2 = texture(tex, wp.xz * scale * 5.5).xyz * 2.0 - 1.0;
  vec3 nz2 = texture(tex, wp.xy * scale * 5.5).xyz * 2.0 - 1.0;
  vec3 nFine = normalize(nx2 * b.x + ny2 * b.y + nz2 * b.z);
  vec3 detail = normalize(nLarge + nFine * 0.7);
  return normalize(mix(normalize(geomN), normalize(geomN + detail * 1.1), 0.88));
}
float pathMaskAt(vec3 wp, float halfW, float edge, float meanderAmp) {
  float meander = sin(wp.z * 0.045 + 0.7) * meanderAmp
                + sin(wp.z * 0.12 + 1.3) * (meanderAmp * 0.35);
  float d = abs(wp.x - meander);
  float w = halfW + sin(wp.z * 0.08) * 0.6;
  return 1.0 - smoothstep(w, w + edge, d);
}

void main() {
  vec3 geomN = normalize(vNormal);
  float tiling = max(uFrame.tiling_pad.x, 0.008);
  float pathHalf = max(uFrame.tiling_pad.y, 2.0);
  float pathEdge = max(uFrame.tiling_pad.z, 1.0);
  float meanderAmp = max(uFrame.tiling_pad.w, 0.5);
  float score = clamp(uFrame.sprintScore_flags.x, 0.0, 1.4);
  float t = uFrame.cameraPos_time.w;
  int flags = int(uFrame.sprintScore_flags.y + 0.5);

  // High-contrast Star Moss base (match terrain.frag)
  vec3 albedo = vec3(0.018, 0.07, 0.1);
  float rough = 0.82;
  float metal = 0.0;
  vec3 N = geomN;
  vec3 emit = vec3(0.0);
  float groundTile = tiling * 1.35;

  if ((flags & 1) != 0) {
    vec3 orm = triplanarORM(uGroundRough, geomN, vWorldPos, groundTile);
    vec3 wpH = vWorldPos + geomN * ((orm.b - 0.5) * 0.12);
    vec3 a1 = triplanarAlbedo(uGroundAlbedo, geomN, wpH, groundTile * 0.85);
    float lum = dot(a1, vec3(0.299, 0.587, 0.114));
    a1 *= mix(0.5, 0.14, smoothstep(0.2, 0.88, lum));
    a1 = mix(a1, a1 * a1, 0.5);
    a1 *= vec3(0.45, 0.8, 0.92);
    a1 *= mix(0.65, 1.15, orm.b);
    albedo = mix(albedo, albedo * 0.55 + a1 * 0.45, 0.55);
    albedo *= 0.78;
    N = triplanarNormal(uGroundNormal, geomN, wpH, groundTile * 0.95);
    rough = mix(rough, orm.r, 0.6);
    metal = orm.g * 0.12;
    emit += triplanarAlbedo(uGroundEmit, geomN, wpH, groundTile * 0.85) * 0.14;
  }
  float faceUp = clamp(geomN.y * 0.7 + 0.3, 0.0, 1.0);
  albedo *= mix(0.62, 1.08, faceUp);
  albedo = min(albedo, vec3(0.16, 0.24, 0.28));
  albedo = max(albedo, vec3(0.008, 0.02, 0.03));

  // Landform visibility (must match forward terrain.frag)
  float craterW = smoothstep(-0.4, -1.8, vFeature);
  float ridgeW  = smoothstep(0.85, 2.6, vFeature);
  float rockFW  = smoothstep(0.9, 3.2, vFeature) * (1.0 - ridgeW * 0.3);
  float valleyW = (1.0 - craterW) * smoothstep(0.35, -0.15, vFeature) *
                  smoothstep(0.55, 0.15, ridgeW + rockFW);
  albedo = mix(albedo, vec3(0.015, 0.1, 0.14), valleyW * 0.6);
  albedo = mix(albedo, vec3(0.025, 0.16, 0.2), craterW * 0.65);
  emit += vec3(0.06, 0.28, 0.35) * craterW * (0.2 + score * 0.15);
  albedo = mix(albedo, vec3(0.18, 0.14, 0.28), ridgeW * 0.55);
  emit += vec3(0.28, 0.38, 0.75) * ridgeW * (0.25 + score * 0.22);
  rough = mix(rough, 0.26, ridgeW * 0.65);
  metal = mix(metal, 0.22, ridgeW * 0.4);

  float slope = 1.0 - clamp(geomN.y, 0.0, 1.0);
  // Earlier / wider rock shelves on slopes
  float rockW = smoothstep(0.04, 0.28, slope);
  rockW = max(rockW, rockFW * 0.95);
  rockW = max(rockW, ridgeW * 0.7);
  float rockNoise = fract(sin(dot(vWorldPos.xz, vec2(12.9898, 78.233))) * 43758.5453);
  rockW *= mix(0.55, 1.15, rockNoise);
  rockW = clamp(rockW, 0.0, 1.0);
  if ((flags & 2) != 0 && rockW > 0.001) {
    float rockTile = tiling * 1.1;
    vec3 ra = triplanarAlbedo(uRockAlbedo, geomN, vWorldPos, rockTile * 0.85);
    float rLum = dot(ra, vec3(0.299, 0.587, 0.114));
    ra *= mix(0.65, 0.28, smoothstep(0.25, 0.85, rLum));
    ra = mix(ra, vec3(0.1, 0.09, 0.14), 0.5);
    ra = mix(ra, vec3(0.24, 0.28, 0.45), rockFW * 0.45 + ridgeW * 0.25);
    albedo = mix(albedo, ra, rockW * 0.95);
    N = normalize(mix(N, triplanarNormal(uRockNormal, geomN, vWorldPos, rockTile * 0.9), rockW * 0.92));
    vec3 ormR = triplanarORM(uRockRough, geomN, vWorldPos, rockTile * 0.85);
    rough = mix(rough, ormR.r, rockW);
    metal = mix(metal, max(ormR.g, 0.08), rockW * 0.5);
    emit += vec3(0.18, 0.32, 0.6) * rockFW * 0.22;
  } else if (rockW > 0.05) {
    albedo = mix(albedo, vec3(0.08, 0.07, 0.1), rockW * 0.75);
    rough = mix(rough, 0.7, rockW * 0.5);
  }

  float pMask = pathMaskAt(vWorldPos, pathHalf, pathEdge, meanderAmp) * smoothstep(0.55, 0.22, slope);
  if (pMask > 0.001) {
    if ((flags & 4) != 0) {
      vec3 pa = triplanarAlbedo(uPathAlbedo, geomN, vWorldPos, tiling * 0.9);
      float pLum = dot(pa, vec3(0.299, 0.587, 0.114));
      pa *= mix(0.75, 0.4, smoothstep(0.3, 0.85, pLum));
      pa = mix(pa, vec3(0.28, 0.7, 0.9), 0.55);
      albedo = mix(albedo, pa, pMask * 0.9);
      N = normalize(mix(N, triplanarNormal(uPathNormal, geomN, vWorldPos, tiling * 0.9), pMask * 0.5));
      emit += triplanarAlbedo(uPathEmit, geomN, vWorldPos, tiling * 0.9) * pMask * 0.55;
    } else {
      albedo = mix(albedo, vec3(0.28, 0.7, 0.9), pMask * 0.92);
    }
    rough = mix(rough, 0.16, pMask * 0.85);
    float pulse = 0.5 + 0.5 * sin(vWorldPos.z * 0.15 - t * 2.2);
    emit += vec3(0.35, 0.75, 0.95) * pMask * pMask * (0.22 + score * 0.18) * pulse;
  }

  outAlbedoMetal = vec4(albedo, metal);
  outNormalRough = vec4(N * 0.5 + 0.5, clamp(rough, 0.04, 1.0));
  outEmit = vec4(emit, 1.0);
}
