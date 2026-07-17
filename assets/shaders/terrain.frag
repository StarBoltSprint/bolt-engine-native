#version 450
layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;
layout(location = 3) in float vHeight;

layout(set = 0, binding = 0) uniform Frame {
  mat4 viewProj;
  vec4 cameraPos_time;
  vec4 sprintScore_flags; // x=score y=hasTex
  vec4 tiling_pad;        // x=tiling
} uFrame;

layout(set = 0, binding = 2) uniform sampler2D uAlbedo;
layout(set = 0, binding = 3) uniform sampler2D uNormalMap;
layout(set = 0, binding = 4) uniform sampler2D uRoughness;

layout(location = 0) out vec4 outColor;

float hash(vec2 p) {
  return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

vec3 triplanarAlbedo(vec3 n, vec3 wp, float scale) {
  vec3 b = abs(normalize(n));
  b = max(b - 0.2, 0.0);
  b /= (b.x + b.y + b.z + 1e-4);
  vec3 cx = texture(uAlbedo, wp.zy * scale).rgb;
  vec3 cy = texture(uAlbedo, wp.xz * scale).rgb;
  vec3 cz = texture(uAlbedo, wp.xy * scale).rgb;
  return cx * b.x + cy * b.y + cz * b.z;
}

vec3 triplanarNormal(vec3 n, vec3 wp, float scale) {
  vec3 b = abs(normalize(n));
  b = max(b - 0.2, 0.0);
  b /= (b.x + b.y + b.z + 1e-4);
  // Sample tangent-space normal maps; blend components simply for Crystal look
  vec3 nx = texture(uNormalMap, wp.zy * scale).xyz * 2.0 - 1.0;
  vec3 ny = texture(uNormalMap, wp.xz * scale).xyz * 2.0 - 1.0;
  vec3 nz = texture(uNormalMap, wp.xy * scale).xyz * 2.0 - 1.0;
  // Reorient approximate (not full TBN) — enough for soft crystal detail
  vec3 wn = normalize(n);
  vec3 t = normalize(cross(wn, vec3(0.0, 1.0, 0.0)) + 1e-4);
  vec3 b2 = cross(wn, t);
  mat3 tbn = mat3(t, b2, wn);
  vec3 blended = normalize(tbn * (nx * b.x + ny * b.y + nz * b.z));
  return normalize(mix(wn, blended, 0.55));
}

float triplanarRough(vec3 n, vec3 wp, float scale) {
  vec3 b = abs(normalize(n));
  b = max(b - 0.2, 0.0);
  b /= (b.x + b.y + b.z + 1e-4);
  float rx = texture(uRoughness, wp.zy * scale).r;
  float ry = texture(uRoughness, wp.xz * scale).r;
  float rz = texture(uRoughness, wp.xy * scale).r;
  return rx * b.x + ry * b.y + rz * b.z;
}

void main() {
  vec3 N = normalize(vNormal);
  float tiling = max(uFrame.tiling_pad.x, 0.04);
  bool hasTex = uFrame.sprintScore_flags.y > 0.5;

  vec3 albedo;
  float rough;
  if (hasTex) {
    albedo = triplanarAlbedo(N, vWorldPos, tiling);
    N = triplanarNormal(N, vWorldPos, tiling);
    rough = triplanarRough(N, vWorldPos, tiling);
  } else {
    // Procedural fallback crystal ground
    vec3 deep = vec3(0.04, 0.12, 0.22);
    vec3 midc = vec3(0.15, 0.45, 0.55);
    vec3 peak = vec3(0.55, 0.85, 0.95);
    float h = smoothstep(-1.0, 10.0, vHeight);
    albedo = mix(deep, midc, h);
    albedo = mix(albedo, peak, smoothstep(4.0, 12.0, vHeight) * 0.5);
    float f = hash(floor(vWorldPos.xz * 0.5));
    albedo += vec3(0.05, 0.12, 0.15) * f * 0.35;
    rough = 0.65;
  }

  vec3 light = normalize(vec3(0.35, 1.0, 0.25));
  float ndl = max(dot(N, light), 0.0);
  // Cheap specular from roughness
  vec3 V = normalize(uFrame.cameraPos_time.xyz - vWorldPos);
  vec3 H = normalize(light + V);
  float spec = pow(max(dot(N, H), 0.0), mix(64.0, 8.0, rough)) * (1.0 - rough) * 0.35;

  vec3 col = albedo * (0.22 + 0.78 * ndl) + vec3(0.7, 0.9, 1.0) * spec;
  col += albedo * 0.06 * smoothstep(3.0, 9.0, vHeight);
  outColor = vec4(col, 1.0);
}
