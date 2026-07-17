#version 450
// White animal mesh — high-contrast lighting so silhouette reads on cyan path

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
  vec3 un = texture(uBoltNormal, uv * 1.8).xyz * 2.0 - 1.0;
  vec3 b = triplanarWeights(geomN);
  float sc = 0.35;
  vec3 nx = texture(uBoltNormal, wp.zy * sc).xyz * 2.0 - 1.0;
  vec3 ny = texture(uBoltNormal, wp.xz * sc).xyz * 2.0 - 1.0;
  vec3 nz = texture(uBoltNormal, wp.xy * sc).xyz * 2.0 - 1.0;
  vec3 tn = normalize(nx * b.x + ny * b.y + nz * b.z);
  vec3 detail = normalize(mix(tn, un, 0.45));
  return normalize(mix(normalize(geomN), normalize(geomN + detail * 0.55), 0.8));
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
    // Subtle aura — do not drown the mesh
    float e = energy;
    if (e < 0.08) discard;
    float fres = pow(1.0 - max(dot(geomN, normalize(uFrame.cameraPos_time.xyz - vWorldPos)), 0.0), 2.2);
    vec3 cyan = vec3(0.3, 0.85, 1.0);
    albedo = cyan;
    emissive = cyan * (0.25 + e * 0.55) * fres;
    rough = 0.25;
    alpha = clamp(0.08 + e * 0.22 + fres * 0.12, 0.0, 0.4);
  } else if (matId == 1) {
    albedo = vec3(0.45, 0.95, 1.0);
    rough = 0.12;
    emissive = vec3(0.25, 0.9, 1.0) * (0.8 + energy * 1.5);
  } else if (matId == 2) {
    albedo = vec3(0.04, 0.05, 0.07);
    rough = 0.3;
  } else {
    // Off-white fur so body separates from cyan path
    albedo = sampleFurAlbedo(geomN, vWorldPos, vUV);
    // Pull toward warm paper-white, keep strand contrast
    albedo = mix(albedo, vec3(0.92, 0.93, 0.95), 0.45);
    albedo = clamp(albedo, vec3(0.35), vec3(0.98));
    N = sampleFurNormal(geomN, vWorldPos, vUV);
    rough = 0.55;
    // Slight sky bounce emissive so underside never pure black
    emissive = vec3(0.04, 0.06, 0.09);
  }

  // Strong key + cool fill + warm rim — silhouette against bright terrain
  vec3 L1 = normalize(vec3(0.55, 0.85, 0.15));
  vec3 L2 = normalize(vec3(-0.65, 0.35, -0.4));
  vec3 L3 = normalize(vec3(-0.2, 0.15, -0.95)); // back rim from camera-ish
  float ndl1 = max(dot(N, L1), 0.0);
  float wrap = clamp((dot(N, L1) + 0.25) / 1.25, 0.0, 1.0);
  float ndl2 = max(dot(N, L2), 0.0);
  float ndl3 = max(dot(N, L3), 0.0);

  // Darker ambient so lit faces pop
  float hemi = N.y * 0.5 + 0.5;
  vec3 ambient = mix(vec3(0.06, 0.08, 0.12), vec3(0.16, 0.2, 0.28), hemi);

  // Fake contact darkening toward feet
  float heightAO = clamp((vWorldPos.y - uFrame.cameraPos_time.y + 6.0) * 0.15, 0.55, 1.0);
  // simpler: darker when normal points down
  float ao = mix(0.55, 1.0, clamp(N.y * 0.65 + 0.35, 0.0, 1.0));

  vec3 V = normalize(uFrame.cameraPos_time.xyz - vWorldPos);
  vec3 H = normalize(L1 + V);
  float gloss = mix(80.0, 14.0, rough);
  float spec = pow(max(dot(N, H), 0.0), gloss) * (1.0 - rough) * 0.55;
  float fres = pow(1.0 - max(dot(N, V), 0.0), 2.4);
  // Strong cyan rim so outline reads on bright ground
  vec3 rim = vec3(0.25, 0.75, 1.0) * fres * (0.45 + energy * 0.4);
  rim += vec3(1.0, 0.95, 0.85) * ndl3 * 0.35;

  vec3 col = ambient * albedo * ao;
  col += albedo * vec3(1.05, 1.0, 0.95) * (0.55 * ndl1 + 0.35 * wrap);
  col += albedo * vec3(0.35, 0.55, 0.85) * ndl2 * 0.45;
  col += vec3(1.0, 0.98, 0.95) * spec + rim + emissive;

  // Keep body brighter than pure path-matching whiteout
  col = max(col, albedo * 0.12);

  float dist = length(uFrame.cameraPos_time.xyz - vWorldPos);
  float fog = 1.0 - exp(-dist * 0.0025);
  col = mix(col, vec3(0.12, 0.32, 0.45), fog * 0.2);

  col = col * (2.4 * col + 0.04) / (col * (2.3 * col + 0.55) + 0.14);
  outColor = vec4(col, alpha);
}
