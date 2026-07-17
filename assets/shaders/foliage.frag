#version 450
// Crystal stalks — PBR, rim glow, atmosphere fog

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;
layout(location = 3) in float vKind;

layout(set = 0, binding = 0) uniform Frame {
  mat4 viewProj;
  vec4 cameraPos_time;
  vec4 sprintScore_flags;
  vec4 tiling_pad;
  mat4 invViewProj;
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
layout(set = 0, binding = 11) uniform sampler2D uStalkAlbedo;
layout(set = 0, binding = 12) uniform sampler2D uStalkNormal;
layout(set = 0, binding = 13) uniform sampler2D uStalkRough;

layout(location = 0) out vec4 outColor;

vec3 atmosphereColor(vec3 viewDir, float dist) {
  float elev = viewDir.y;
  float h = clamp(elev * 0.5 + 0.5, 0.0, 1.0);
  vec3 zenith  = vec3(0.02, 0.04, 0.09);
  vec3 midSky  = vec3(0.05, 0.12, 0.24);
  vec3 horizon = vec3(0.14, 0.38, 0.50);
  vec3 sky = mix(horizon, midSky, smoothstep(0.0, 0.45, h));
  sky = mix(sky, zenith, smoothstep(0.35, 0.95, h));
  float horizBand = exp(-abs(elev) * 7.0);
  sky += vec3(0.3, 0.65, 0.85) * horizBand * 0.35;
  float dens = 1.0 - exp(-dist * 0.0022);
  return mix(sky, vec3(0.18, 0.42, 0.55), dens * 0.35);
}

void main() {
  vec3 n = normalize(vNormal);
  vec3 L = normalize(vec3(0.4, 0.95, 0.22));
  vec3 L2 = normalize(vec3(-0.5, 0.4, -0.3));
  float ndl = max(dot(n, L), 0.0);
  float wrap = clamp((dot(n, L) + 0.4) / 1.4, 0.0, 1.0);
  float ndl2 = max(dot(n, L2), 0.0) * 0.3;

  vec3 stalk = vec3(0.12, 0.48, 0.68);
  vec3 crystal = vec3(0.55, 0.9, 1.0);
  vec3 flower = vec3(0.72, 0.42, 0.95);
  vec3 albedo = stalk;
  if (vKind > 1.5) albedo = crystal;
  else if (vKind > 0.5) albedo = flower;

  float tip = smoothstep(0.15, 1.0, vUV.y);
  float base = 1.0 - smoothstep(0.0, 0.35, vUV.y);
  albedo = mix(albedo * 0.45, albedo * 1.3, tip);

  int flags = int(uFrame.sprintScore_flags.y + 0.5);
  bool hasStalk = (flags & 8) != 0;
  float rough = 0.22;

  if (hasStalk) {
    vec2 uvA = vUV * vec2(1.8, 2.4) + vec2(vWorldPos.x, vWorldPos.y) * 0.04;
    vec2 uvB = vUV * vec2(0.9, 1.2) + vWorldPos.xz * 0.02;
    vec3 tex = mix(texture(uStalkAlbedo, uvA).rgb, texture(uStalkAlbedo, uvB).rgb, 0.35);
    albedo = mix(albedo, tex, 0.78);
    vec3 nMap = texture(uStalkNormal, uvA).xyz * 2.0 - 1.0;
    n = normalize(mix(n, normalize(n + nMap * 0.55), 0.6));
    rough = mix(0.18, texture(uStalkRough, uvA).r, 0.7);
  }

  vec3 V = normalize(uFrame.cameraPos_time.xyz - vWorldPos);
  vec3 H = normalize(L + V);
  float gloss = mix(100.0, 12.0, rough);
  float spec = pow(max(dot(n, H), 0.0), gloss) * (1.0 - rough) * 0.85;
  float fres = pow(1.0 - max(dot(n, V), 0.0), 2.5);
  vec3 rim = vec3(0.4, 0.8, 1.0) * fres * (0.25 + tip * 0.55);

  float hemi = n.y * 0.5 + 0.5;
  vec3 ambient = mix(vec3(0.04, 0.06, 0.1), vec3(0.14, 0.28, 0.4), hemi);

  // Contact darkening near base
  float contact = mix(0.55, 1.0, tip * 0.5 + (1.0 - base) * 0.5);

  vec3 col = ambient * albedo * contact;
  col += albedo * vec3(1.0, 0.97, 0.92) * (0.45 * ndl + 0.3 * wrap);
  col += albedo * vec3(0.4, 0.65, 0.95) * ndl2;
  col += vec3(0.85, 0.95, 1.0) * spec + rim;
  col += albedo * tip * 0.35;

  if (vKind > 1.5) col += vec3(0.2, 0.55, 0.7) * tip * 0.5;
  else if (vKind > 0.5) col += vec3(0.4, 0.15, 0.55) * tip * 0.4;

  float dist = length(uFrame.cameraPos_time.xyz - vWorldPos);
  float fog = 1.0 - exp(-dist * 0.0045);
  fog = clamp(fog, 0.0, 0.95);
  vec3 fogCol = atmosphereColor(V, dist);
  col = mix(col, fogCol, fog * 0.9);

  col = col * (2.51 * col + 0.03) / (col * (2.43 * col + 0.59) + 0.14);
  col = pow(max(col, 0.0), vec3(0.95));
  outColor = vec4(col, 1.0);
}
