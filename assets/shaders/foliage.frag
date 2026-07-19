#version 450
// Crystal stalks — PBR + IBL + ORM, rim glow
#include "common_pbr.glsl"

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;
layout(location = 3) in float vKind;
layout(location = 4) in float vColorSeed;

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

layout(set = 0, binding = 24) uniform sampler2D uShadowMap;

layout(set = 0, binding = 2) uniform sampler2D uGroundAlbedo;
layout(set = 0, binding = 3) uniform sampler2D uGroundNormal;
layout(set = 0, binding = 4) uniform sampler2D uGroundRough;
layout(set = 0, binding = 5) uniform sampler2D uRockAlbedo;
layout(set = 0, binding = 6) uniform sampler2D uRockNormal;
layout(set = 0, binding = 7) uniform sampler2D uRockRough;
layout(set = 0, binding = 8) uniform sampler2D uPathAlbedo;
layout(set = 0, binding = 9) uniform sampler2D uPathNormal;
layout(set = 0, binding = 10) uniform sampler2D uPathRough;
layout(set = 0, binding = 11) uniform sampler2D uStalkAlbedo;
layout(set = 0, binding = 12) uniform sampler2D uStalkNormal;
layout(set = 0, binding = 13) uniform sampler2D uStalkRough;
layout(set = 0, binding = 22) uniform sampler2D uStalkEmit;

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

vec3 evalCrystalLights(vec3 wp, vec3 n, vec3 albedo, float rough) {
  vec3 sum = vec3(0.0);
  uint cnt = min(uLights.lightCount, 64u);
  for (uint i = 0u; i < cnt; ++i) {
    vec3 Lpos = uLights.lights[i].posRange.xyz;
    float range = max(uLights.lights[i].posRange.w, 0.5);
    vec3 toL = Lpos - wp;
    float dist = length(toL);
    float atten = 1.0 - smoothstep(range * 0.5, range, dist);
    if (atten < 0.004) continue;
    vec3 Ldir = toL / max(dist, 1e-3);
    float ndl = max(dot(n, Ldir), 0.0);
    float wrap = clamp((dot(n, Ldir) + 0.4) / 1.4, 0.0, 1.0);
    vec3 col = uLights.lights[i].colorIntensity.rgb;
    float inten = uLights.lights[i].colorIntensity.w;
    float fall = atten * atten * (1.0 / (1.0 + dist * dist * 0.015));
    sum += albedo * col * (0.5 * ndl + 0.4 * wrap) * inten * fall;
    vec3 V = normalize(uFrame.cameraPos_time.xyz - wp);
    vec3 H = normalize(Ldir + V);
    float spec = pow(max(dot(n, H), 0.0), mix(90.0, 16.0, rough)) * (1.0 - rough) * 0.45;
    sum += col * spec * inten * fall;
  }
  return sum;
}

vec3 atmosphereColor(vec3 viewDir, float dist) {
  float elev = viewDir.y;
  float h = clamp(elev * 0.5 + 0.5, 0.0, 1.0);
  vec3 zenith  = vec3(0.08, 0.04, 0.16);
  vec3 midSky  = vec3(0.16, 0.1, 0.28);
  vec3 horizon = vec3(0.32, 0.18, 0.42);
  vec3 sky = mix(horizon, midSky, smoothstep(0.0, 0.4, h));
  sky = mix(sky, zenith, smoothstep(0.35, 0.95, h));
  float horizBand = exp(-abs(elev) * 5.5);
  sky += vec3(0.55, 0.35, 0.85) * horizBand * 0.45;
  sky += vec3(0.25, 0.65, 0.85) * horizBand * 0.22;
  float dens = 1.0 - exp(-dist * 0.0015);
  return mix(sky, vec3(0.2, 0.18, 0.36), dens * 0.5);
}

void main() {
  vec3 n = normalize(vNormal);
  vec3 L = normalize(vec3(0.35, 0.9, 0.3));
  vec3 L2 = normalize(vec3(-0.5, 0.45, -0.25));
  float ndl = max(dot(n, L), 0.0);
  // Tight wrap — stop pale white trunks from over-fill
  float wrap = clamp((dot(n, L) + 0.1) / 1.1, 0.0, 1.0);
  wrap *= wrap;
  float ndl2 = max(dot(n, L2), 0.0) * 0.25;

  // kind: 0–5 veg, 10–13 ruins, 20–24 Detail, 25–28 Flying
  // Base palettes: matte stone/bark + crystal accents (less plastic candy)
  vec3 albedo = vec3(0.12, 0.32, 0.42);           // cyan spear / default
  vec3 emitTint = vec3(0.12, 0.4, 0.55);
  float detailPulse = 0.0;
  bool isRuin = false;
  bool isTree = false;
  bool isCrystalTip = false; // shiny crystal vs matte trunk/stone

  if (vKind > 27.5) {
    albedo = vec3(0.32, 0.28, 0.58);
    emitTint = vec3(0.55, 0.4, 0.95);
    detailPulse = 1.0;
    isCrystalTip = true;
  } else if (vKind > 26.5) {
    albedo = vec3(0.22, 0.2, 0.26); // debris stone
    emitTint = vec3(0.55, 0.4, 0.75);
    detailPulse = 0.55;
  } else if (vKind > 25.5) {
    albedo = vec3(0.32, 0.55, 0.75);
    emitTint = vec3(0.3, 0.75, 1.0);
    detailPulse = 0.85;
    isCrystalTip = true;
  } else if (vKind > 24.5) {
    albedo = vec3(0.38, 0.6, 0.8);
    emitTint = vec3(0.35, 0.7, 0.95);
    detailPulse = 0.75;
    isCrystalTip = true;
  } else if (vKind > 23.5) {
    albedo = vec3(0.16, 0.14, 0.2);
    emitTint = vec3(0.75, 0.6, 0.3);
    detailPulse = 0.45;
  } else if (vKind > 22.5) {
    albedo = vec3(0.35, 0.55, 0.75);
    emitTint = vec3(0.35, 0.75, 1.0);
    detailPulse = 0.7;
    isCrystalTip = true;
  } else if (vKind > 21.5) {
    albedo = vec3(0.1, 0.22, 0.28);
    emitTint = vec3(0.2, 0.85, 0.95);
    detailPulse = 0.95;
  } else if (vKind > 20.5) {
    albedo = vec3(0.28, 0.55, 0.72);
    emitTint = vec3(0.25, 0.75, 0.95);
    detailPulse = 0.8;
    isCrystalTip = true;
  } else if (vKind > 19.5) {
    albedo = vec3(0.22, 0.4, 0.55);
    emitTint = vec3(0.2, 0.55, 0.75);
    detailPulse = 0.3;
  } else if (vKind > 12.5) {
    // Temple — warm weathered stone
    albedo = vec3(0.28, 0.24, 0.2);
    emitTint = vec3(0.85, 0.6, 0.25);
    isRuin = true;
  } else if (vKind > 11.5) {
    // Observatory — cool basalt + cyan glass
    albedo = vec3(0.22, 0.24, 0.28);
    emitTint = vec3(0.4, 0.8, 0.95);
    isRuin = true;
  } else if (vKind > 10.5) {
    // Arch — dark stone
    albedo = vec3(0.2, 0.18, 0.22);
    emitTint = vec3(0.85, 0.5, 0.3);
    isRuin = true;
  } else if (vKind > 9.5) {
    // Monolith — basalt
    albedo = vec3(0.18, 0.19, 0.22);
    emitTint = vec3(0.9, 0.65, 0.28);
    isRuin = true;
  } else if (vKind > 4.5) {
    // Amethyst floater / crystal canopy
    albedo = vec3(0.38, 0.28, 0.55);
    emitTint = vec3(0.45, 0.28, 0.8);
    isCrystalTip = true;
  } else if (vKind > 3.5) {
    // Crystal tree — matte purple bole, crystal crown later via tip
    albedo = vec3(0.22, 0.14, 0.32);
    emitTint = vec3(0.4, 0.25, 0.7);
    isTree = true;
  } else if (vKind > 2.5) {
    albedo = vec3(0.18, 0.32, 0.38); // fern
    emitTint = vec3(0.22, 0.45, 0.55);
  } else if (vKind > 1.5) {
    albedo = vec3(0.28, 0.55, 0.7); // crystal cluster
    emitTint = vec3(0.25, 0.65, 0.85);
    isCrystalTip = true;
  } else if (vKind > 0.5) {
    albedo = vec3(0.45, 0.28, 0.55); // flower
    emitTint = vec3(0.5, 0.22, 0.7);
  }

  float tip = smoothstep(0.12, 1.0, vUV.y);
  float base = 1.0 - smoothstep(0.0, 0.35, vUV.y);
  // Trees/ruins: dark base, not super-bright tips (was plastic 1.35)
  if (isTree || isRuin) {
    albedo = mix(albedo * 0.55, albedo * 1.05, tip);
  } else if (isCrystalTip) {
    albedo = mix(albedo * 0.5, albedo * 1.15, tip);
  } else {
    albedo = mix(albedo * 0.45, albedo * 1.12, tip);
  }

  int flags = int(uFrame.sprintScore_flags.y + 0.5);
  bool hasStalk = (flags & 8) != 0;
  bool hasRock = (flags & 2) != 0;
  float rough = 0.55;
  float metal = 0.02;
  vec3 mapEmit = vec3(0.0);

  // —— Material classes: matte stone / bark vs crystal glass ——
  if (isRuin) {
    // Dark matte stone body; only upper facets slightly smoother
    rough = mix(0.78, 0.42, tip * 0.55);
    metal = mix(0.02, 0.12, tip * tip);
    // Rock PBR on ruins (breaks plastic smooth primitives)
    if (hasRock) {
      vec2 uvR = vUV * vec2(2.2, 2.8) + vWorldPos.xz * 0.03;
      vec2 uvR2 = vWorldPos.xy * 0.08 + vUV.yx * 1.4;
      vec3 tex = mix(texture(uRockAlbedo, uvR).rgb, texture(uRockAlbedo, uvR2).rgb, 0.4);
      float rLum = dot(tex, vec3(0.299, 0.587, 0.114));
      tex *= mix(0.55, 0.28, smoothstep(0.3, 0.85, rLum));
      tex = mix(tex, tex * tex, 0.35);
      albedo = mix(albedo, albedo * 0.4 + tex * 0.6, 0.72);
      vec3 nMap = texture(uRockNormal, uvR).xyz * 2.0 - 1.0;
      n = normalize(mix(n, normalize(n + nMap * 0.85), 0.7));
      vec3 orm = texture(uRockRough, uvR).rgb;
      rough = mix(rough, remapRoughness(orm.r, 0.45, 0.92, 1.4), 0.75);
      metal = mix(metal, orm.g * 0.2, 0.25);
    }
    // Stone micro grit always
    n = proceduralMicroNormal(n, vWorldPos, 2.2, 0.85);
    n = normalize(mix(normalize(vNormal), n, 0.75));
    // Rune / crystal trim only near tips
    mapEmit = emitTint * tip * tip * 0.35;
  } else if (isTree) {
    // Matte trunk / bole; crystal tips slightly glossier
    rough = mix(0.72, 0.28, tip);
    metal = mix(0.02, 0.1, tip * tip);
    if (hasStalk) {
      vec2 uvA = vUV * vec2(1.6, 2.2) + vec2(vWorldPos.x, vWorldPos.y) * 0.035;
      vec2 uvB = vWorldPos.xz * 0.05 + vUV * 1.1;
      vec3 orm = texture(uStalkRough, uvA).rgb;
      float hgt = orm.b;
      uvA += vec2(hgt - 0.5) * 0.03;
      vec3 tex = mix(texture(uStalkAlbedo, uvA).rgb, texture(uStalkAlbedo, uvB).rgb, 0.4);
      // Darken map into bark/crystal, keep detail
      float lum = dot(tex, vec3(0.299, 0.587, 0.114));
      tex *= mix(0.7, 0.35, smoothstep(0.35, 0.9, lum));
      // Base = bark-like dark; tip = crystal
      vec3 bark = albedo * vec3(0.7, 0.65, 0.85);
      vec3 crystal = mix(albedo, tex, 0.55) * vec3(0.9, 0.85, 1.05);
      albedo = mix(bark, crystal, tip);
      vec3 nMap = texture(uStalkNormal, uvA).xyz * 2.0 - 1.0;
      n = normalize(mix(n, normalize(n + nMap * 0.7), mix(0.45, 0.75, tip)));
      float rMap = remapRoughness(orm.r, 0.35, 0.88, 1.35);
      rough = mix(rough, mix(rMap * 1.15, rMap * 0.65, tip), 0.7);
      metal = mix(metal, orm.g * 0.25, tip * 0.4);
      mapEmit = texture(uStalkEmit, uvA).rgb * tip * 0.45;
    }
    n = proceduralMicroNormal(n, vWorldPos, 2.5, mix(0.9, 0.5, tip));
    n = normalize(mix(normalize(vNormal), n, 0.7));
  } else if (hasStalk && (vKind < 3.5 || isCrystalTip)) {
    // Spears / clusters / crystal tips — stalk PBR
    vec2 uvA = vUV * vec2(1.8, 2.4) + vec2(vWorldPos.x, vWorldPos.y) * 0.04;
    vec2 uvB = vUV * vec2(0.9, 1.2) + vWorldPos.xz * 0.02;
    vec3 orm = texture(uStalkRough, uvA).rgb;
    float hgt = orm.b;
    uvA += vec2(hgt - 0.5) * 0.04;
    vec3 tex = mix(texture(uStalkAlbedo, uvA).rgb, texture(uStalkAlbedo, uvB).rgb, 0.35);
    float lum = dot(tex, vec3(0.299, 0.587, 0.114));
    tex *= mix(0.75, 0.4, smoothstep(0.4, 0.9, lum));
    albedo = mix(albedo, tex, 0.6);
    vec3 nMap = texture(uStalkNormal, uvA).xyz * 2.0 - 1.0;
    n = normalize(mix(n, normalize(n + nMap * 0.65), 0.65));
    // Base matte, tip glossier (crystal)
    float rMap = remapRoughness(orm.r, 0.22, 0.78, 1.4);
    rough = mix(mix(0.62, rMap, 0.6), mix(0.18, rMap * 0.5, 0.7), tip);
    metal = mix(0.03, 0.14, tip);
    mapEmit = texture(uStalkEmit, uvA).rgb * 0.55;
    n = proceduralMicroNormal(n, vWorldPos, 3.0, 0.55);
    n = normalize(mix(normalize(vNormal), n, 0.6));
  } else {
    // Generic flora / detail without maps
    rough = mix(0.65, 0.32, tip);
    metal = mix(0.02, 0.08, tip);
    n = proceduralMicroNormal(n, vWorldPos, 2.8, 0.7);
    n = normalize(mix(normalize(vNormal), n, 0.55));
  }

  vec3 V = normalize(uFrame.cameraPos_time.xyz - vWorldPos);
  vec3 H = normalize(L + V);
  // Soften plastic specular: lower peak, roughness-driven
  float gloss = mix(48.0, 10.0, rough);
  float spec = pow(max(dot(n, H), 0.0), gloss) * (1.0 - rough) * mix(0.25, 0.55, tip);
  float fres = pow(1.0 - max(dot(n, V), 0.0), mix(3.2, 2.2, isRuin ? 1.0 : 0.0));
  // Rim separates props from purple fog — cool for crystal, warm for ruins
  vec3 rim = emitTint * fres * mix(0.18, 0.42, tip);
  if (isRuin) rim = mix(vec3(0.15, 0.12, 0.1), emitTint, tip) * fres * 0.35;

  float contact = mix(0.42, 1.0, tip * 0.5 + (1.0 - base) * 0.4);
  // Trees/ruins sit darker at base (ground contact)
  if (isTree || isRuin) contact *= mix(0.72, 1.0, tip);

  // Per-instance variation (hue + value, not bright white)
  float ih = fract(vColorSeed * 7.13 + sin(dot(vWorldPos.xz, vec2(12.9898, 78.233))) * 43758.5453);
  float ih2 = fract(vColorSeed * 3.17 + vWorldPos.x * 0.02);
  albedo *= mix(vec3(0.82, 0.88, 0.95), vec3(1.08, 0.92, 0.9), ih);
  albedo = mix(albedo, albedo * vec3(1.04, 0.88, 1.1), (ih2 - 0.5) * 0.4);
  // Hard clamp — no plastic white towers
  if (isRuin) albedo = min(albedo, vec3(0.42, 0.4, 0.38));
  else if (isTree) albedo = min(albedo, vec3(0.48, 0.4, 0.58));
  else albedo = min(albedo, vec3(0.58, 0.62, 0.72));
  albedo *= mix(0.9, 0.82, float(isRuin));

  float a = max(rough * rough, 0.04);
  float NdH = max(dot(n, H), 0.0);
  float a2 = a * a;
  float dggx = a2 / max(3.14159 * pow(NdH * NdH * (a2 - 1.0) + 1.0, 2.0), 1e-4);
  vec3 F0 = mix(vec3(0.04), albedo, metal);
  vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
  // Less GGX punch on matte stone/bark
  float ggxSpec = dggx * mix(0.12, 0.45, 1.0 - rough) * mix(0.5, 1.0, tip);

  float sh = 1.0;
  if (uFrame.shadowParams.z > 0.5) {
    vec3 sclip = worldToShadowClip(uFrame.lightViewProj, vWorldPos + n * 0.05);
    sh = sampleShadowPCF(uShadowMap, sclip, uFrame.shadowParams.x, uFrame.shadowParams.w);
    sh = mix(1.0, sh, uFrame.shadowParams.y);
    sh = mix(0.1, 1.0, sh);
  }

  // Lower IBL so props don't go flat purple-plastic
  float iblStr = isRuin ? 0.28 : (isTree ? 0.32 : 0.38);
  vec3 col = evalIBL(n, V, albedo, rough, metal) * contact * iblStr;
  col += albedo * vec3(0.98, 0.92, 0.88) * (0.42 * ndl + 0.06 * wrap) * sh;
  col += albedo * vec3(0.32, 0.28, 0.58) * ndl2 * mix(0.35, 0.9, sh);
  col += vec3(0.75, 0.78, 0.9) * (spec * 0.14) * sh + F * ggxSpec * sh * 0.55 + rim * 0.85;
  col += evalCrystalLights(vWorldPos, n, albedo, rough) * contact * 0.55;

  // Crystal SSS only on tips / crystal kinds — not full plastic glow
  float sss = wrap * (0.08 + tip * 0.28) * float(isCrystalTip || isTree);
  col += emitTint * sss * 0.22;
  col += emitTint * tip * tip * (0.12 + uFrame.sprintScore_flags.x * 0.12);
  col += mapEmit * (0.25 + tip * 0.35);

  if (detailPulse > 0.01) {
    float t = uFrame.cameraPos_time.w;
    float score = uFrame.sprintScore_flags.x;
    float pulse = 0.55 + 0.45 * sin(t * (2.2 + detailPulse) + vWorldPos.x * 0.3 + vWorldPos.z * 0.25);
    col += emitTint * detailPulse * pulse * (0.22 + score * 0.28);
    if (vKind > 21.5 && vKind < 22.5)
      col += emitTint * (0.18 + score * 0.28) * (0.35 + tip * 0.7);
    if (vKind > 24.5)
      col += emitTint * fres * (0.28 + score * 0.25) * 0.6;
  }

  // Ruin runes: gold/cyan only on upper facets, not whole body glow
  if (isRuin) {
    float score = uFrame.sprintScore_flags.x;
    float rune = tip * tip * (0.25 + score * 0.4);
    col += emitTint * rune * 0.55;
    col += emitTint * fres * tip * (0.2 + score * 0.15);
  } else if (isTree) {
    col += emitTint * tip * tip * 0.28;
  } else if (isCrystalTip) {
    col += emitTint * tip * 0.2;
  }

  float dist = length(uFrame.cameraPos_time.xyz - vWorldPos);
  float fog = 1.0 - exp(-max(0.0, dist - 38.0) * 0.0011);
  fog = clamp(fog, 0.0, 0.7);
  vec3 fogCol = atmosphereColor(V, dist) * 0.55;
  col = mix(col, fogCol, fog * 0.45);

  outColor = vec4(col, 1.0);
}
