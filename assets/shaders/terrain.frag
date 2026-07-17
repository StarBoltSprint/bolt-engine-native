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

float hash21(vec2 p) {
  return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float noise2(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  float a = hash21(i);
  float b = hash21(i + vec2(1.0, 0.0));
  float c = hash21(i + vec2(0.0, 1.0));
  float d = hash21(i + vec2(1.0, 1.0));
  return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float fbm2(vec2 p) {
  float v = 0.0;
  float a = 0.5;
  for (int i = 0; i < 5; ++i) {
    v += a * noise2(p);
    p *= 2.03;
    a *= 0.5;
  }
  return v;
}

vec3 triplanarAlbedo(vec3 n, vec3 wp, float scale) {
  vec3 b = pow(abs(normalize(n)), vec3(4.0));
  b /= (b.x + b.y + b.z + 1e-4);
  vec3 cx = texture(uAlbedo, wp.zy * scale).rgb;
  vec3 cy = texture(uAlbedo, wp.xz * scale).rgb;
  vec3 cz = texture(uAlbedo, wp.xy * scale).rgb;
  return cx * b.x + cy * b.y + cz * b.z;
}

float triplanarRough(vec3 n, vec3 wp, float scale) {
  vec3 b = pow(abs(normalize(n)), vec3(4.0));
  b /= (b.x + b.y + b.z + 1e-4);
  float rx = texture(uRoughness, wp.zy * scale).r;
  float ry = texture(uRoughness, wp.xz * scale).r;
  float rz = texture(uRoughness, wp.xy * scale).r;
  return rx * b.x + ry * b.y + rz * b.z;
}

vec3 triplanarNormal(vec3 geomN, vec3 wp, float scale) {
  vec3 b = pow(abs(normalize(geomN)), vec3(4.0));
  b /= (b.x + b.y + b.z + 1e-4);
  vec3 nx = texture(uNormalMap, wp.zy * scale).xyz * 2.0 - 1.0;
  vec3 ny = texture(uNormalMap, wp.xz * scale).xyz * 2.0 - 1.0;
  vec3 nz = texture(uNormalMap, wp.xy * scale).xyz * 2.0 - 1.0;
  // Soft blend with geometry normal (avoids harsh seams)
  vec3 detail = normalize(nx * b.x + ny * b.y + nz * b.z);
  return normalize(mix(normalize(geomN), normalize(geomN + detail * 0.45), 0.65));
}

void main() {
  vec3 geomN = normalize(vNormal);
  float tiling = max(uFrame.tiling_pad.x, 0.02);
  bool hasTex = uFrame.sprintScore_flags.y > 0.5;

  vec3 albedo;
  float rough;
  vec3 N = geomN;

  if (hasTex) {
    // Macro + detail triplanar (breaks big checker tiles)
    vec3 a1 = triplanarAlbedo(geomN, vWorldPos, tiling);
    vec3 a2 = triplanarAlbedo(geomN, vWorldPos, tiling * 3.7);
    albedo = mix(a1, a2, 0.28);
    N = triplanarNormal(geomN, vWorldPos, tiling);
    rough = triplanarRough(geomN, vWorldPos, tiling);
    // Desaturate harsh UV grid, push crystal grade
    float lum = dot(albedo, vec3(0.3, 0.5, 0.2));
    albedo = mix(vec3(lum), albedo, 0.85);
    albedo = mix(albedo, vec3(0.2, 0.55, 0.7), 0.12);
  } else {
    float n = fbm2(vWorldPos.xz * 0.035);
    vec3 deep = vec3(0.03, 0.10, 0.18);
    vec3 midc = vec3(0.12, 0.42, 0.52);
    vec3 peak = vec3(0.45, 0.82, 0.95);
    float h = smoothstep(-0.5, 9.0, vHeight);
    albedo = mix(deep, midc, h);
    albedo = mix(albedo, peak, smoothstep(3.5, 11.0, vHeight) * 0.55);
    albedo += vec3(0.04, 0.1, 0.12) * n;
    rough = 0.55;
  }

  // Slope darken (valleys)
  float slope = 1.0 - clamp(geomN.y, 0.0, 1.0);
  albedo *= (1.0 - slope * 0.25);

  vec3 lightDir = normalize(vec3(0.4, 0.95, 0.2));
  vec3 light2 = normalize(vec3(-0.5, 0.4, -0.3));
  float ndl = max(dot(N, lightDir), 0.0);
  float ndl2 = max(dot(N, light2), 0.0) * 0.25;
  vec3 ambient = vec3(0.08, 0.12, 0.18);

  vec3 V = normalize(uFrame.cameraPos_time.xyz - vWorldPos);
  vec3 H = normalize(lightDir + V);
  float gloss = mix(96.0, 12.0, rough);
  float spec = pow(max(dot(N, H), 0.0), gloss) * (1.0 - rough) * 0.45;

  vec3 col = ambient * albedo + albedo * (0.55 * ndl + ndl2) + vec3(0.65, 0.9, 1.0) * spec;
  // Height glow veins
  col += vec3(0.1, 0.35, 0.5) * 0.08 * smoothstep(2.5, 8.0, vHeight);

  // Distance fog into nebula sky
  float dist = length(uFrame.cameraPos_time.xyz - vWorldPos);
  float fog = smoothstep(40.0, 180.0, dist);
  vec3 sky = vec3(0.02, 0.04, 0.09);
  col = mix(col, sky, fog * 0.85);

  // Gentle grade
  col = pow(max(col, 0.0), vec3(0.95));
  outColor = vec4(col, 1.0);
}
