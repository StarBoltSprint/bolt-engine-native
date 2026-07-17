#version 450
layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;
layout(location = 3) in float vHeight;

layout(location = 0) out vec4 outColor;

// Procedural crystal ground (until albedo texture is bound)
float hash(vec2 p) {
  return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main() {
  vec3 n = normalize(vNormal);
  vec3 light = normalize(vec3(0.35, 1.0, 0.25));
  float ndl = max(dot(n, light), 0.0);

  // Teal crystal base + height color
  vec3 deep = vec3(0.04, 0.12, 0.22);
  vec3 mid = vec3(0.15, 0.45, 0.55);
  vec3 peak = vec3(0.55, 0.85, 0.95);
  float h = smoothstep(-1.0, 10.0, vHeight);
  vec3 albedo = mix(deep, mid, h);
  albedo = mix(albedo, peak, smoothstep(4.0, 12.0, vHeight) * 0.5);

  // Detail freckles
  float f = hash(floor(vWorldPos.xz * 0.5));
  albedo += vec3(0.05, 0.12, 0.15) * f * 0.35;

  vec3 col = albedo * (0.22 + 0.78 * ndl);
  // Soft emissive ridge glow
  col += albedo * 0.08 * smoothstep(3.0, 9.0, vHeight);
  outColor = vec4(col, 1.0);
}
