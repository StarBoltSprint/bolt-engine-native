#version 450
layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;
layout(location = 3) in float vKind;

layout(location = 0) out vec4 outColor;

void main() {
  vec3 n = normalize(vNormal);
  float ndl = max(dot(n, normalize(vec3(0.3, 1.0, 0.2))), 0.0);
  vec3 stalk = vec3(0.2, 0.7, 0.85);
  vec3 crystal = vec3(0.55, 0.85, 1.0);
  vec3 flower = vec3(0.7, 0.5, 1.0);
  vec3 albedo = stalk;
  if (vKind > 1.5) albedo = crystal;
  else if (vKind > 0.5) albedo = flower;
  vec3 col = albedo * (0.3 + 0.7 * ndl);
  col += albedo * 0.25; // emissive crystal feel
  outColor = vec4(col, 1.0);
}
