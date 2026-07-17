#version 450
// Pure white GSD — triplanar fur PBR + energy eyes (same pipeline as terrain materials)

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

// bolt_fur PBR set (Imagine → bolt_grok_import)
layout(set = 0, binding = 15) uniform sampler2D uBoltAlbedo;
layout(set = 0, binding = 16) uniform sampler2D uBoltNormal;
layout(set = 0, binding = 17) uniform sampler2D uBoltRough;

layout(location = 0) out vec4 outColor;

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

float triplanarRough(sampler2D tex, vec3 n, vec3 wp, float scale) {
  vec3 b = triplanarWeights(n);
  return texture(tex, wp.zy * scale).r * b.x
       + texture(tex, wp.xz * scale).r * b.y
       + texture(tex, wp.xy * scale).r * b.z;
}

vec3 triplanarNormal(sampler2D tex, vec3 geomN, vec3 wp, float scale) {
  vec3 b = triplanarWeights(geomN);
  vec3 nx = texture(tex, wp.zy * scale).xyz * 2.0 - 1.0;
  vec3 ny = texture(tex, wp.xz * scale).xyz * 2.0 - 1.0;
  vec3 nz = texture(tex, wp.xy * scale).xyz * 2.0 - 1.0;
  vec3 detail = normalize(nx * b.x + ny * b.y + nz * b.z);
  return normalize(mix(normalize(geomN), normalize(geomN + detail * 0.55), 0.7));
}

void main() {
  vec3 geomN = normalize(vNormal);
  float tiling = 0.55;
  int matId = int(vMatId + 0.5);

  vec3 albedo;
  float rough;
  vec3 N = geomN;
  vec3 emissive = vec3(0.0);

  if (matId == 1) {
    // Cyan energy eyes / shoulder blazes
    albedo = vec3(0.55, 0.92, 1.0);
    rough = 0.15;
    float e = 0.55 + vEnergy * 1.4;
    emissive = vec3(0.15, 0.75, 1.0) * e;
  } else if (matId == 2) {
    // Nose / pupil — dark wet
    albedo = vec3(0.06, 0.07, 0.1);
    rough = 0.28;
  } else if (matId == 3) {
    // Ear inner — soft pink
    albedo = vec3(1.0, 0.72, 0.78);
    rough = 0.75;
  } else if (matId == 4) {
    // Paw pads
    albedo = vec3(0.12, 0.14, 0.18);
    rough = 0.85;
  } else {
    // Pure white fur — Imagine PBR triplanar (NO white floor crush)
    vec3 furA = triplanarAlbedo(uBoltAlbedo, geomN, vWorldPos, tiling);
    vec3 furB = triplanarAlbedo(uBoltAlbedo, geomN, vWorldPos, tiling * 2.8);
    albedo = mix(furA, furB, 0.3);
    // Keep white GSD readable but preserve strand contrast
    albedo = mix(albedo, vec3(0.97, 0.98, 1.0), 0.35);
    albedo = clamp(albedo, vec3(0.55), vec3(1.0));
    N = triplanarNormal(uBoltNormal, geomN, vWorldPos, tiling);
    rough = mix(0.45, triplanarRough(uBoltRough, geomN, vWorldPos, tiling), 0.75);
    rough = clamp(rough, 0.35, 0.9);
    // Soft self-emissive so white fur never pure black in shade
    emissive = albedo * (0.03 + vEnergy * 0.04);
  }

  vec3 L1 = normalize(vec3(0.4, 0.95, 0.22));
  vec3 L2 = normalize(vec3(-0.5, 0.4, -0.3));
  float ndl1 = max(dot(N, L1), 0.0);
  float wrap = clamp((dot(N, L1) + 0.4) / 1.4, 0.0, 1.0);
  float ndl2 = max(dot(N, L2), 0.0) * 0.3;

  float hemi = N.y * 0.5 + 0.5;
  vec3 ambient = mix(vec3(0.08, 0.1, 0.14), vec3(0.2, 0.28, 0.36), hemi);

  vec3 V = normalize(uFrame.cameraPos_time.xyz - vWorldPos);
  vec3 H = normalize(L1 + V);
  float gloss = mix(96.0, 12.0, rough);
  float spec = pow(max(dot(N, H), 0.0), gloss) * (1.0 - rough) * 0.45;
  float fres = pow(1.0 - max(dot(N, V), 0.0), 2.8);
  vec3 rim = vec3(0.4, 0.8, 1.0) * fres * (0.2 + vEnergy * 0.55);

  vec3 col = ambient * albedo;
  col += albedo * vec3(1.0, 0.97, 0.92) * (0.45 * ndl1 + 0.28 * wrap);
  col += albedo * vec3(0.4, 0.65, 0.95) * ndl2;
  col += vec3(0.9, 0.95, 1.0) * spec + rim + emissive;

  // Sprint energy veil on fur only
  if (matId == 0) {
    col += vec3(0.2, 0.55, 0.75) * vEnergy * 0.12 * fres;
  }

  float dist = length(uFrame.cameraPos_time.xyz - vWorldPos);
  float fog = 1.0 - exp(-dist * 0.0035);
  col = mix(col, vec3(0.12, 0.32, 0.45), fog * 0.28);

  col = col * (2.51 * col + 0.03) / (col * (2.43 * col + 0.59) + 0.14);
  outColor = vec4(col, 1.0);
}
