#version 450
// Crystal ground — triplanar PBR so materials integrate with noise terrain

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;
layout(location = 3) in float vHeight;

layout(set = 1, binding = 0) uniform sampler2D uAlbedo;
layout(set = 1, binding = 1) uniform sampler2D uNormal;
layout(set = 1, binding = 2) uniform sampler2D uRoughness;

layout(location = 0) out vec4 outColor;

vec3 triplanarAlbedo(vec3 n, vec3 wp, float scale) {
  vec3 b = abs(normalize(n));
  b = max(b - 0.2, 0.0);
  b /= (b.x + b.y + b.z + 1e-4);
  vec3 cx = texture(uAlbedo, wp.zy * scale).rgb;
  vec3 cy = texture(uAlbedo, wp.xz * scale).rgb;
  vec3 cz = texture(uAlbedo, wp.xy * scale).rgb;
  return cx * b.x + cy * b.y + cz * b.z;
}

void main() {
  vec3 albedo = triplanarAlbedo(vNormal, vWorldPos, 0.08);
  // Crystal teal grade
  albedo = mix(albedo, vec3(0.25, 0.55, 0.75), 0.25);
  float ndl = max(dot(normalize(vNormal), normalize(vec3(0.4, 1.0, 0.2))), 0.0);
  vec3 col = albedo * (0.25 + 0.75 * ndl);
  // Soft emissive veins by height
  col += vec3(0.1, 0.25, 0.4) * smoothstep(2.0, 8.0, vHeight) * 0.15;
  outColor = vec4(col, 1.0);
}
