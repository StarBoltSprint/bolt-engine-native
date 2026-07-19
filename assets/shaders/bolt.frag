#version 450
// Bolt — PBR fur + IBL + approximate SSS + emissive maps + volumetric fog
#include "common_pbr.glsl"
#include "atmos_eval.glsl"

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;
layout(location = 3) in float vEnergy;
layout(location = 4) in float vMatId;

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

layout(set = 0, binding = 15) uniform sampler2D uBoltAlbedo;
layout(set = 0, binding = 16) uniform sampler2D uBoltNormal;
layout(set = 0, binding = 17) uniform sampler2D uBoltRough; // R rough G metal B height
layout(set = 0, binding = 23) uniform sampler2D uBoltEmit;
layout(set = 0, binding = 24) uniform sampler2DArray uShadowMap;

struct CrystalLight {
  vec4 posRange;
  vec4 colorIntensity;
};
layout(std430, set = 0, binding = 18) readonly buffer CrystalLights {
  uint lightCount;
  uint _pad0;
  uint _pad1;
  uint _pad2;
  CrystalLight lights[];
} uLights;

layout(location = 0) out vec4 outColor;

vec3 evalCrystalLights(vec3 wp, vec3 N, vec3 albedo, float rough) {
  vec3 sum = vec3(0.0);
  uint cnt = min(uLights.lightCount, 64u);
  for (uint i = 0u; i < cnt; ++i) {
    vec3 Lpos = uLights.lights[i].posRange.xyz;
    float range = max(uLights.lights[i].posRange.w, 0.5);
    vec3 toL = Lpos - wp;
    float dist = length(toL);
    // Skip self-aura if extremely close (bolt own light)
    if (dist < 0.35) continue;
    float atten = 1.0 - smoothstep(range * 0.45, range, dist);
    if (atten < 0.004) continue;
    vec3 Ldir = toL / max(dist, 1e-3);
    float ndl = max(dot(N, Ldir), 0.0);
    float wrap = clamp((dot(N, Ldir) + 0.25) / 1.25, 0.0, 1.0);
    vec3 col = uLights.lights[i].colorIntensity.rgb;
    float inten = uLights.lights[i].colorIntensity.w;
    float fall = atten * atten * (1.0 / (1.0 + dist * dist * 0.02));
    sum += albedo * col * (0.6 * ndl + 0.3 * wrap) * inten * fall;
    vec3 V = normalize(uFrame.cameraPos_time.xyz - wp);
    vec3 H = normalize(Ldir + V);
    float spec = pow(max(dot(N, H), 0.0), mix(70.0, 14.0, rough)) * (1.0 - rough) * 0.4;
    sum += col * spec * inten * fall;
  }
  return sum;
}

vec3 triplanarWeights(vec3 n) {
  vec3 b = pow(abs(normalize(n)), vec3(3.5));
  return b / (b.x + b.y + b.z + 1e-4);
}

vec3 sampleFurAlbedo(vec3 n, vec3 wp, vec2 uv) {
  vec3 uvA = texture(uBoltAlbedo, uv * 1.8).rgb;
  vec3 b = triplanarWeights(n);
  float sc = 0.35;
  vec3 tA = texture(uBoltAlbedo, wp.zy * sc).rgb * b.x
          + texture(uBoltAlbedo, wp.xz * sc).rgb * b.y
          + texture(uBoltAlbedo, wp.xy * sc).rgb * b.z;
  return mix(tA, uvA, 0.5);
}

vec3 sampleFurNormal(vec3 geomN, vec3 wp, vec2 uv) {
  // Large-scale + fine combing detail (dual UV scale)
  vec3 unL = texture(uBoltNormal, uv * 1.4).xyz * 2.0 - 1.0;
  vec3 unF = texture(uBoltNormal, uv * 4.2 + 0.13).xyz * 2.0 - 1.0;
  // Fake combing direction along U (strand flow)
  unF.x += 0.25;
  unF = normalize(unF);
  vec3 un = blendDetailNormalTS(unL, unF, 0.65);

  vec3 b = triplanarWeights(geomN);
  float sc = 0.28;
  float scF = 0.95;
  vec3 nx = texture(uBoltNormal, wp.zy * sc).xyz * 2.0 - 1.0;
  vec3 ny = texture(uBoltNormal, wp.xz * sc).xyz * 2.0 - 1.0;
  vec3 nz = texture(uBoltNormal, wp.xy * sc).xyz * 2.0 - 1.0;
  vec3 tn = normalize(nx * b.x + ny * b.y + nz * b.z);
  vec3 nxf = texture(uBoltNormal, wp.zy * scF).xyz * 2.0 - 1.0;
  vec3 nyf = texture(uBoltNormal, wp.xz * scF).xyz * 2.0 - 1.0;
  vec3 nzf = texture(uBoltNormal, wp.xy * scF).xyz * 2.0 - 1.0;
  vec3 tnF = normalize(nxf * b.x + nyf * b.y + nzf * b.z);
  tn = blendDetailNormalTS(tn, tnF, 0.5);

  vec3 detail = blendDetailNormalTS(tn, un, 0.55);
  vec3 fromMap = normalize(mix(normalize(geomN), normalize(geomN + detail * 0.85), 0.88));
  // Micro strand noise (world-space)
  vec3 micro = proceduralMicroNormal(fromMap, wp, 2.5, 0.55);
  return normalize(mix(fromMap, micro, 0.2));
}

void main() {
  vec3 geomN = normalize(vNormal);
  int matId = int(vMatId + 0.5);
  float energy = clamp(vEnergy, 0.0, 1.5);

  vec3 albedo;
  float rough;
  vec3 N = geomN;
  vec3 emissive = vec3(0.0);
  float alpha = 1.0;

  float metal = 0.0;
  float isFur = 0.0;

  if (matId == 5) {
    // Lightning aura shell — thin Fresnel-only veil (not full-body ghost)
    float e = energy;
    if (e < 0.1) discard;
    float fres = pow(1.0 - max(dot(geomN, normalize(uFrame.cameraPos_time.xyz - vWorldPos)), 0.0), 2.6);
    vec3 cyan = vec3(0.25, 0.75, 0.95);
    vec3 purp = vec3(0.55, 0.35, 0.9);
    albedo = mix(cyan, purp, 0.35);
    // Only edge glow + sparse map veins
    emissive = mix(cyan, purp, fres) * fres * (0.15 + e * 0.35);
    emissive += texture(uBoltEmit, vUV * 2.0).rgb * e * fres * fres * 0.45;
    rough = 0.35;
    alpha = clamp(0.04 + e * 0.12 + fres * 0.22, 0.0, 0.28);
  } else if (matId == 1) {
    // Eyes — keep bright (hero read)
    albedo = vec3(0.35, 0.85, 0.95);
    rough = 0.15;
    metal = 0.15;
    emissive = vec3(0.2, 0.75, 0.95) * (0.55 + energy * 0.7);
  } else if (matId == 2) {
    albedo = vec3(0.04, 0.04, 0.05);
    rough = 0.35;
    metal = 0.04;
  } else if (matId == 4) {
    // Paw pads — darker, matte (helps ground contact read)
    albedo = vec3(0.12, 0.1, 0.11);
    rough = 0.7;
    metal = 0.0;
  } else {
    // Fur: solid coat, not ice-white ghost
    vec3 orm = texture(uBoltRough, vUV * 1.8).rgb;
    float hgt = orm.b;
    vec2 uvP = vUV + vec2(hgt - 0.5) * 0.03;
    albedo = sampleFurAlbedo(geomN, vWorldPos, uvP);
    // Pull toward warm off-white / cream, not pure white
    albedo = mix(albedo, vec3(0.78, 0.76, 0.74), 0.22);
    // Underbelly / down-facing slightly cooler and darker
    float under = clamp(-geomN.y * 0.5 + 0.5, 0.0, 1.0);
    albedo *= mix(1.0, 0.72, under * 0.55);
    albedo = clamp(albedo, vec3(0.22), vec3(0.88));
    N = sampleFurNormal(geomN, vWorldPos, uvP);
    float tipSheen = clamp(geomN.y * 0.5 + 0.5, 0.0, 1.0);
    float rMap = remapRoughness(orm.r, 0.4, 0.88, 1.3);
    rough = mix(rMap, rMap * 0.78, tipSheen * 0.4);
    rough = mix(rough, 0.72, (1.0 - hgt) * 0.25);
    metal = orm.g * 0.05;
    // Emissive only for lightning veins — heavily gated, not full-body wash
    vec3 emitMap = texture(uBoltEmit, uvP * 1.8).rgb;
    float vein = max(max(emitMap.r, emitMap.g), emitMap.b);
    emissive = emitMap * smoothstep(0.12, 0.55, vein) * (0.08 + energy * 0.18);
    isFur = 1.0;
  }

  vec3 L1 = normalize(vec3(0.55, 0.85, 0.15));
  vec3 L2 = normalize(vec3(-0.65, 0.35, -0.4));
  vec3 L3 = normalize(vec3(-0.2, 0.15, -0.95));
  float ndl1 = max(dot(N, L1), 0.0);
  float wrap = clamp((dot(N, L1) + 0.12) / 1.12, 0.0, 1.0);
  wrap *= wrap;
  float ndl2 = max(dot(N, L2), 0.0);
  float ndl3 = max(dot(N, L3), 0.0);

  // Contact AO: darker belly / lower legs toward ground (feet y≈0 in object… use world normal + height heuristic)
  float upAo = mix(0.48, 1.0, clamp(N.y * 0.75 + 0.28, 0.0, 1.0));
  // Extra darkening when normal faces down (paws / underbelly)
  float downFace = clamp(-N.y, 0.0, 1.0);
  float contactAo = mix(1.0, 0.42, downFace * 0.85);
  // Distance from camera is not feet; use world Y relative to player feet via camera height bias
  // Approximate: lower vertices in view (higher depth of body) — use normal only for stable contact
  float ao = upAo * contactAo;

  vec3 V = normalize(uFrame.cameraPos_time.xyz - vWorldPos);
  vec3 H = normalize(L1 + V);
  float gloss = mix(48.0, 12.0, rough);
  float spec = pow(max(dot(N, H), 0.0), gloss) * (1.0 - rough) * 0.28;
  float fres = pow(1.0 - max(dot(N, V), 0.0), 2.8);
  float score = uFrame.sprintScore_flags.x;
  // Cyan rim ONLY at grazing angles — silhouette, not full body fill
  vec3 rim = vec3(0.18, 0.7, 0.95) * fres * fres * (0.35 + energy * 0.35 + score * 0.15);
  rim += vec3(0.9, 0.85, 0.75) * ndl3 * 0.12;

  float sh = 1.0;
  if (uFrame.shadowParams.z > 0.5 && matId != 5) {
    sh = sampleShadowCSM(uShadowMap, uFrame.lightViewProj[0], uFrame.lightViewProj[1],
                         uFrame.lightViewProj[2], uFrame.cascadeOrigin.xyz,
                         uFrame.cascadeSplits.xyz, vWorldPos, N, uFrame.shadowParams.x,
                         uFrame.shadowParams.w);
    sh = mix(1.0, sh, uFrame.shadowParams.y);
    sh = mix(0.18, 1.0, sh); // punch ground contact shadow on body
  }

  // Solid lit fur — lower IBL (was washing to cyan ghost)
  vec3 col = evalIBL(N, V, albedo, rough, metal) * ao * 0.32;
  col += albedo * vec3(1.0, 0.96, 0.92) * (0.48 * ndl1 + 0.08 * wrap) * sh * ao;
  col += albedo * vec3(0.28, 0.42, 0.7) * ndl2 * 0.28 * ao;
  col += vec3(0.9, 0.92, 0.95) * spec * sh;
  col += rim;
  col += emissive;
  col += evalCrystalLights(vWorldPos, N, albedo, rough) * ao * 0.45;

  // Subtle fur SSS — not a full cyan wash
  if (isFur > 0.5) {
    float thickness = clamp(0.4 + rough * 0.35, 0.25, 0.85);
    col += evalFurSSS(N, L1, V, albedo, thickness) * 0.35;
    col += evalFurSSS(N, L2, V, albedo, thickness) * 0.12;
  }

  // Floor of solid coat (no pure black holes, no force-bright ghost)
  col = max(col, albedo * 0.08 * ao);

  // Volumetric haze so Bolt sits in the same air as terrain/props
  {
    float score = uFrame.sprintScore_flags.x;
    vec3 cam = uFrame.cameraPos_time.xyz;
    vec3 sunDir = normalize(vec3(0.35, 0.88, 0.35));
    vec3 fogSky = vec3(0.12, 0.08, 0.22);
    col = applyVolumetricFogFast(col, cam, vWorldPos, sunDir, fogSky,
                                 uFrame.cameraPos_time.w, score);
  }

  outColor = vec4(col, alpha);
}
