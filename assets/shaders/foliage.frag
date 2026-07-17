#version 450
layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;
layout(location = 3) in float vKind;

layout(set = 0, binding = 0) uniform Frame {
  mat4 viewProj;
  vec4 cameraPos_time;
  vec4 sprintScore_flags;
  vec4 tiling_pad;
} uFrame;

layout(set = 0, binding = 2) uniform sampler2D uAlbedo;
layout(set = 0, binding = 3) uniform sampler2D uNormalMap;
layout(set = 0, binding = 4) uniform sampler2D uRoughness;

layout(location = 0) out vec4 outColor;

void main() {
  vec3 n = normalize(vNormal);
  vec3 L = normalize(vec3(0.35, 1.0, 0.25));
  float ndl = max(dot(n, L), 0.0);

  // Crystal crystal / stalk / flower palette
  vec3 stalk = vec3(0.15, 0.55, 0.75);
  vec3 crystal = vec3(0.55, 0.9, 1.0);
  vec3 flower = vec3(0.75, 0.45, 1.0);
  vec3 albedo = stalk;
  if (vKind > 1.5) albedo = crystal;
  else if (vKind > 0.5) albedo = flower;

  // Vertical gradient + tip glow
  float tip = smoothstep(0.2, 1.0, vUV.y);
  albedo = mix(albedo * 0.55, albedo * 1.25, tip);

  bool hasTex = uFrame.sprintScore_flags.y > 0.5;
  if (hasTex) {
    vec3 tex = texture(uAlbedo, vUV * 1.5 + vWorldPos.xz * 0.03).rgb;
    albedo = mix(albedo, tex, 0.35);
  }

  float rough = hasTex ? texture(uRoughness, vUV).r : 0.25;
  vec3 V = normalize(uFrame.cameraPos_time.xyz - vWorldPos);
  vec3 H = normalize(L + V);
  float spec = pow(max(dot(n, H), 0.0), mix(80.0, 10.0, rough)) * 0.65;

  vec3 col = albedo * (0.2 + 0.8 * ndl) + albedo * 0.35 * tip + vec3(0.8, 0.95, 1.0) * spec;

  float dist = length(uFrame.cameraPos_time.xyz - vWorldPos);
  float fog = smoothstep(50.0, 160.0, dist);
  col = mix(col, vec3(0.02, 0.04, 0.09), fog * 0.8);

  outColor = vec4(col, 1.0);
}
