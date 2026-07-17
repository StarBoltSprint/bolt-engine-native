#version 450
// White GSD — UV + triplanar fur PBR, energy eyes, lightning aura shell

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
} uFrame;

layout(set = 0, binding = 15) uniform sampler2D uBoltAlbedo;
layout(set = 0, binding = 16) uniform sampler2D uBoltNormal;
layout(set = 0, binding = 17) uniform sampler2D uBoltRough;

layout(location = 0) out vec4 outColor;

vec3 triplanarWeights(vec3 n) {
  vec3 b = pow(abs(normalize(n)), vec3(4.0));
  return b / (b.x + b.y + b.z + 1e-4);
}

vec3 sampleFurAlbedo(vec3 n, vec3 wp, vec2 uv) {
  // Prefer mesh UVs; blend triplanar to hide seams
  vec3 uvA = texture(uBoltAlbedo, uv * 2.2).rgb;
  vec3 b = triplanarWeights(n);
  float sc = 0.45;
  vec3 tA = texture(uBoltAlbedo, wp.zy * sc).rgb * b.x
          + texture(uBoltAlbedo, wp.xz * sc).rgb * b.y
          + texture(uBoltAlbedo, wp.xy * sc).rgb * b.z;
  return mix(tA, uvA, 0.55);
}

vec3 sampleFurNormal(vec3 geomN, vec3 wp, vec2 uv) {
  vec3 un = texture(uBoltNormal, uv * 2.2).xyz * 2.0 - 1.0;
  vec3 b = triplanarWeights(geomN);
  float sc = 0.45;
  vec3 nx = texture(uBoltNormal, wp.zy * sc).xyz * 2.0 - 1.0;
  vec3 ny = texture(uBoltNormal, wp.xz * sc).xyz * 2.0 - 1.0;
  vec3 nz = texture(uBoltNormal, wp.xy * sc).xyz * 2.0 - 1.0;
  vec3 tn = normalize(nx * b.x + ny * b.y + nz * b.z);
  vec3 detail = normalize(mix(tn, un, 0.5));
  return normalize(mix(normalize(geomN), normalize(geomN + detail * 0.5), 0.75));
}

float sampleFurRough(vec3 n, vec3 wp, vec2 uv) {
  float ur = texture(uBoltRough, uv * 2.2).r;
  vec3 b = triplanarWeights(n);
  float sc = 0.45;
  float tr = texture(uBoltRough, wp.zy * sc).r * b.x
           + texture(uBoltRough, wp.xz * sc).r * b.y
           + texture(uBoltRough, wp.xy * sc).r * b.z;
  return mix(tr, ur, 0.55);
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

  if (matId == 5) {
    // Lightning aura shell — additive cyan/purple, scales with sprint energy
    float e = energy;
    if (e < 0.05) discard;
    float fres = pow(1.0 - max(dot(geomN, normalize(uFrame.cameraPos_time.xyz - vWorldPos)), 0.0), 2.0);
    vec3 cyan = vec3(0.25, 0.85, 1.0);
    vec3 purp = vec3(0.65, 0.35, 1.0);
    albedo = mix(cyan, purp, 0.35 + 0.3 * sin(uFrame.cameraPos_time.w * 4.0 + vWorldPos.y));
    emissive = albedo * (0.4 + e * 1.6) * (0.35 + fres);
    rough = 0.2;
    alpha = clamp(0.12 + e * 0.45 + fres * 0.25, 0.0, 0.75);
    N = geomN;
  } else if (matId == 1) {
    albedo = vec3(0.55, 0.95, 1.0);
    rough = 0.12;
    emissive = vec3(0.2, 0.85, 1.0) * (0.7 + energy * 1.8);
  } else if (matId == 2) {
    albedo = vec3(0.05, 0.06, 0.08);
    rough = 0.25;
  } else if (matId == 3) {
    albedo = vec3(1.0, 0.72, 0.78);
    rough = 0.8;
  } else if (matId == 4) {
    albedo = vec3(0.12, 0.13, 0.16);
    rough = 0.88;
  } else {
    // Fur — Imagine PBR on UVs + triplanar blend
    albedo = sampleFurAlbedo(geomN, vWorldPos, vUV);
    albedo = mix(albedo, vec3(0.96, 0.97, 1.0), 0.28);
    albedo = clamp(albedo, vec3(0.5), vec3(1.0));
    N = sampleFurNormal(geomN, vWorldPos, vUV);
    rough = clamp(mix(0.5, sampleFurRough(geomN, vWorldPos, vUV), 0.7), 0.35, 0.92);
    emissive = albedo * (0.025 + energy * 0.05);
  }

  vec3 L1 = normalize(vec3(0.4, 0.95, 0.22));
  vec3 L2 = normalize(vec3(-0.5, 0.4, -0.3));
  float ndl1 = max(dot(N, L1), 0.0);
  float wrap = clamp((dot(N, L1) + 0.4) / 1.4, 0.0, 1.0);
  float ndl2 = max(dot(N, L2), 0.0) * 0.28;
  float hemi = N.y * 0.5 + 0.5;
  vec3 ambient = mix(vec3(0.07, 0.09, 0.12), vec3(0.18, 0.26, 0.34), hemi);

  vec3 V = normalize(uFrame.cameraPos_time.xyz - vWorldPos);
  vec3 H = normalize(L1 + V);
  float gloss = mix(100.0, 12.0, rough);
  float spec = pow(max(dot(N, H), 0.0), gloss) * (1.0 - rough) * 0.5;
  float fres = pow(1.0 - max(dot(N, V), 0.0), 2.6);
  vec3 rim = vec3(0.35, 0.8, 1.0) * fres * (0.18 + energy * 0.65);

  vec3 col = ambient * albedo;
  col += albedo * vec3(1.0, 0.97, 0.92) * (0.42 * ndl1 + 0.28 * wrap);
  col += albedo * vec3(0.4, 0.65, 0.95) * ndl2;
  col += vec3(0.9, 0.95, 1.0) * spec + rim + emissive;

  if (matId == 0) {
    col += vec3(0.15, 0.5, 0.7) * energy * 0.1 * fres;
  }

  float dist = length(uFrame.cameraPos_time.xyz - vWorldPos);
  float fog = 1.0 - exp(-dist * 0.0035);
  col = mix(col, vec3(0.12, 0.32, 0.45), fog * 0.25);

  col = col * (2.51 * col + 0.03) / (col * (2.43 * col + 0.59) + 0.14);
  outColor = vec4(col, alpha);
}
