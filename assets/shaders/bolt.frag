#version 450
// Pure white Bolt with soft crystal rim so it reads on teal ground

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec3 vColor;

layout(set = 0, binding = 0) uniform Frame {
  mat4 viewProj;
  vec4 cameraPos_time;
  vec4 sprintScore_flags;
  vec4 tiling_pad;
  mat4 invViewProj;
} uFrame;

layout(location = 0) out vec4 outColor;

void main() {
  vec3 n = normalize(vNormal);
  vec3 L = normalize(vec3(0.4, 0.95, 0.22));
  float ndl = max(dot(n, L), 0.0);
  float wrap = clamp((dot(n, L) + 0.5) / 1.5, 0.0, 1.0);
  vec3 V = normalize(uFrame.cameraPos_time.xyz - vWorldPos);
  float fres = pow(1.0 - max(dot(n, V), 0.0), 2.5);

  // Near-white albedo with cool rim (readable silhouette)
  vec3 albedo = mix(vec3(0.92, 0.95, 1.0), vColor, 0.15);
  vec3 col = albedo * (0.35 + 0.55 * ndl + 0.25 * wrap);
  col += vec3(0.55, 0.85, 1.0) * fres * 0.55;
  col += albedo * 0.12; // slight self-emissive so never pure black in shade

  float dist = length(uFrame.cameraPos_time.xyz - vWorldPos);
  float fog = 1.0 - exp(-dist * 0.004);
  col = mix(col, vec3(0.12, 0.32, 0.45), fog * 0.35);

  col = col * (2.51 * col + 0.03) / (col * (2.43 * col + 0.59) + 0.14);
  outColor = vec4(col, 1.0);
}
