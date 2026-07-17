#version 450
// Pure white German Shepherd sprite + optional fur micro-detail

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;
layout(location = 3) in float vEnergy;

layout(set = 0, binding = 0) uniform Frame {
  mat4 viewProj;
  vec4 cameraPos_time;
  vec4 sprintScore_flags;
  vec4 tiling_pad;
  mat4 invViewProj;
} uFrame;

// Sprite (chroma-keyed Imagine GSD) + fur albedo micro detail
layout(set = 0, binding = 15) uniform sampler2D uBoltSprite;
layout(set = 0, binding = 16) uniform sampler2D uBoltFur;

layout(location = 0) out vec4 outColor;

void main() {
  vec4 spr = texture(uBoltSprite, vUV);
  if (spr.a < 0.08) discard;

  vec3 albedo = spr.rgb;
  // Soft pure-white lift so fur never goes grey on teal world
  float lum = dot(albedo, vec3(0.3, 0.5, 0.2));
  albedo = mix(vec3(lum), albedo, 0.9);
  albedo = max(albedo, vec3(0.85));

  // Micro fur detail (subtle)
  vec3 fur = texture(uBoltFur, vUV * 3.5 + vWorldPos.xz * 0.02).rgb;
  albedo *= mix(vec3(1.0), fur * 1.05, 0.22);

  vec3 n = normalize(vNormal);
  // Fake sphere-ish lighting from UV for billboard card
  vec2 c = vUV * 2.0 - 1.0;
  vec3 nBill = normalize(vec3(c.x * 0.55, (1.0 - vUV.y) * 0.35 + 0.4, 1.0));
  n = normalize(mix(n, nBill, 0.85));

  vec3 L = normalize(vec3(0.4, 0.95, 0.22));
  float ndl = max(dot(n, L), 0.0);
  float wrap = clamp((dot(n, L) + 0.55) / 1.55, 0.0, 1.0);
  vec3 V = normalize(uFrame.cameraPos_time.xyz - vWorldPos);
  float fres = pow(1.0 - max(dot(n, V), 0.0), 2.2);

  vec3 col = albedo * (0.42 + 0.45 * ndl + 0.28 * wrap);
  // Cyan energy rim + eye boost from Imagine glow already in sprite
  col += vec3(0.35, 0.8, 1.0) * fres * (0.25 + vEnergy * 0.55);
  col += vec3(0.2, 0.55, 0.75) * vEnergy * 0.12;

  float dist = length(uFrame.cameraPos_time.xyz - vWorldPos);
  float fog = 1.0 - exp(-dist * 0.0035);
  col = mix(col, vec3(0.12, 0.32, 0.45), fog * 0.3);

  col = col * (2.51 * col + 0.03) / (col * (2.43 * col + 0.59) + 0.14);
  outColor = vec4(col, spr.a);
}
