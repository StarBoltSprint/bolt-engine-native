#version 450
// Crystal stalks — sample stalk PBR material with kind-based tint

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;
layout(location = 3) in float vKind;

layout(set = 0, binding = 0) uniform Frame {
  mat4 viewProj;
  vec4 cameraPos_time;
  vec4 sprintScore_flags; // y = mat flags (8 = stalk)
  vec4 tiling_pad;
} uFrame;

// ground unused for foliage but bindings must match layout
layout(set = 0, binding = 2) uniform sampler2D uGroundAlbedo;
layout(set = 0, binding = 3) uniform sampler2D uGroundNormal;
layout(set = 0, binding = 4) uniform sampler2D uGroundRough;
layout(set = 0, binding = 5) uniform sampler2D uRockAlbedo;
layout(set = 0, binding = 6) uniform sampler2D uRockNormal;
layout(set = 0, binding = 7) uniform sampler2D uRockRough;
layout(set = 0, binding = 8) uniform sampler2D uPathAlbedo;
layout(set = 0, binding = 9) uniform sampler2D uPathNormal;
layout(set = 0, binding = 10) uniform sampler2D uPathRough;
// stalk material
layout(set = 0, binding = 11) uniform sampler2D uStalkAlbedo;
layout(set = 0, binding = 12) uniform sampler2D uStalkNormal;
layout(set = 0, binding = 13) uniform sampler2D uStalkRough;

layout(location = 0) out vec4 outColor;

void main() {
  vec3 n = normalize(vNormal);
  vec3 L = normalize(vec3(0.35, 1.0, 0.25));
  float ndl = max(dot(n, L), 0.0);

  // Kind palette underlay
  vec3 stalk = vec3(0.15, 0.55, 0.75);
  vec3 crystal = vec3(0.55, 0.9, 1.0);
  vec3 flower = vec3(0.75, 0.45, 1.0);
  vec3 albedo = stalk;
  if (vKind > 1.5) albedo = crystal;
  else if (vKind > 0.5) albedo = flower;

  float tip = smoothstep(0.2, 1.0, vUV.y);
  albedo = mix(albedo * 0.55, albedo * 1.25, tip);

  int flags = int(uFrame.sprintScore_flags.y + 0.5);
  bool hasStalk = (flags & 8) != 0;
  float rough = 0.25;

  if (hasStalk) {
    // Vertical-ish UV + world wrap so stalks don't look stamped
    vec2 uvA = vUV * vec2(1.8, 2.4) + vec2(vWorldPos.x, vWorldPos.y) * 0.04;
    vec2 uvB = vUV * vec2(0.9, 1.2) + vWorldPos.xz * 0.02;
    vec3 texA = texture(uStalkAlbedo, uvA).rgb;
    vec3 texB = texture(uStalkAlbedo, uvB).rgb;
    vec3 tex = mix(texA, texB, 0.35);
    albedo = mix(albedo, tex, 0.72);

    vec3 nMap = texture(uStalkNormal, uvA).xyz * 2.0 - 1.0;
    n = normalize(mix(n, normalize(n + nMap * 0.5), 0.55));
    rough = mix(0.2, texture(uStalkRough, uvA).r, 0.75);
  }

  vec3 V = normalize(uFrame.cameraPos_time.xyz - vWorldPos);
  vec3 H = normalize(L + V);
  float spec = pow(max(dot(n, H), 0.0), mix(90.0, 12.0, rough)) * 0.7;

  vec3 col = albedo * (0.18 + 0.82 * ndl) + albedo * 0.4 * tip + vec3(0.75, 0.95, 1.0) * spec;
  // Soft emissive tip glow for crystal kinds
  if (vKind > 1.5) col += vec3(0.15, 0.45, 0.55) * tip * 0.35;
  else if (vKind > 0.5) col += vec3(0.35, 0.15, 0.5) * tip * 0.25;

  float dist = length(uFrame.cameraPos_time.xyz - vWorldPos);
  float fog = smoothstep(50.0, 160.0, dist);
  col = mix(col, vec3(0.02, 0.04, 0.09), fog * 0.8);

  outColor = vec4(col, 1.0);
}
