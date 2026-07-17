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
  float ndl = max(dot(n, normalize(vec3(0.3, 1.0, 0.2))), 0.0);
  bool hasTex = uFrame.sprintScore_flags.y > 0.5;

  vec3 stalk = vec3(0.2, 0.7, 0.85);
  vec3 crystal = vec3(0.55, 0.85, 1.0);
  vec3 flower = vec3(0.7, 0.5, 1.0);
  vec3 albedo = stalk;
  if (vKind > 1.5) albedo = crystal;
  else if (vKind > 0.5) albedo = flower;

  if (hasTex) {
    // Tint Grok ground palette onto stalks for style coherence
    vec3 tex = texture(uAlbedo, vUV * 2.0 + vWorldPos.xz * 0.05).rgb;
    albedo = mix(albedo, tex, 0.45);
  }

  float rough = hasTex ? texture(uRoughness, vUV).r : 0.4;
  vec3 V = normalize(uFrame.cameraPos_time.xyz - vWorldPos);
  vec3 L = normalize(vec3(0.3, 1.0, 0.2));
  vec3 H = normalize(L + V);
  float spec = pow(max(dot(n, H), 0.0), mix(48.0, 6.0, rough)) * 0.4;

  vec3 col = albedo * (0.3 + 0.7 * ndl) + albedo * 0.2 + vec3(spec);
  outColor = vec4(col, 1.0);
}
