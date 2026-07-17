#version 450
// Crystal biome — multi-material triplanar: ground + slope rock + path ribbon

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;
layout(location = 3) in float vHeight;

layout(set = 0, binding = 0) uniform Frame {
  mat4 viewProj;
  vec4 cameraPos_time;
  vec4 sprintScore_flags; // x=score y=matFlags (1 ground 2 rock 4 path 8 stalk)
  vec4 tiling_pad;        // x=tiling y=pathHalfW z=pathEdge w=meanderAmp
} uFrame;

// ground
layout(set = 0, binding = 2) uniform sampler2D uGroundAlbedo;
layout(set = 0, binding = 3) uniform sampler2D uGroundNormal;
layout(set = 0, binding = 4) uniform sampler2D uGroundRough;
// rock
layout(set = 0, binding = 5) uniform sampler2D uRockAlbedo;
layout(set = 0, binding = 6) uniform sampler2D uRockNormal;
layout(set = 0, binding = 7) uniform sampler2D uRockRough;
// path
layout(set = 0, binding = 8) uniform sampler2D uPathAlbedo;
layout(set = 0, binding = 9) uniform sampler2D uPathNormal;
layout(set = 0, binding = 10) uniform sampler2D uPathRough;

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

vec3 triplanarWeights(vec3 n) {
  vec3 b = pow(abs(normalize(n)), vec3(4.0));
  return b / (b.x + b.y + b.z + 1e-4);
}

vec3 triplanarAlbedo(sampler2D tex, vec3 n, vec3 wp, float scale) {
  vec3 b = triplanarWeights(n);
  vec3 cx = texture(tex, wp.zy * scale).rgb;
  vec3 cy = texture(tex, wp.xz * scale).rgb;
  vec3 cz = texture(tex, wp.xy * scale).rgb;
  return cx * b.x + cy * b.y + cz * b.z;
}

float triplanarRough(sampler2D tex, vec3 n, vec3 wp, float scale) {
  vec3 b = triplanarWeights(n);
  float rx = texture(tex, wp.zy * scale).r;
  float ry = texture(tex, wp.xz * scale).r;
  float rz = texture(tex, wp.xy * scale).r;
  return rx * b.x + ry * b.y + rz * b.z;
}

vec3 triplanarNormal(sampler2D tex, vec3 geomN, vec3 wp, float scale) {
  vec3 b = triplanarWeights(geomN);
  vec3 nx = texture(tex, wp.zy * scale).xyz * 2.0 - 1.0;
  vec3 ny = texture(tex, wp.xz * scale).xyz * 2.0 - 1.0;
  vec3 nz = texture(tex, wp.xy * scale).xyz * 2.0 - 1.0;
  vec3 detail = normalize(nx * b.x + ny * b.y + nz * b.z);
  return normalize(mix(normalize(geomN), normalize(geomN + detail * 0.45), 0.65));
}

// Soft S-curve sprint corridor through Crystal plains (world-space ribbon)
float pathMaskAt(vec3 wp, float halfW, float edge, float meanderAmp) {
  float meander = sin(wp.z * 0.045 + 0.7) * meanderAmp
                + sin(wp.z * 0.12 + 1.3) * (meanderAmp * 0.35);
  float d = abs(wp.x - meander);
  // Slight width breathing so ribbon feels organic
  float w = halfW + sin(wp.z * 0.08) * 0.6;
  return 1.0 - smoothstep(w, w + edge, d);
}

void main() {
  vec3 geomN = normalize(vNormal);
  float tiling = max(uFrame.tiling_pad.x, 0.02);
  float pathHalf = max(uFrame.tiling_pad.y, 2.0);
  float pathEdge = max(uFrame.tiling_pad.z, 1.0);
  float meanderAmp = max(uFrame.tiling_pad.w, 0.5);
  int flags = int(uFrame.sprintScore_flags.y + 0.5);
  bool hasGround = (flags & 1) != 0;
  bool hasRock = (flags & 2) != 0;
  bool hasPath = (flags & 4) != 0;

  vec3 albedo;
  float rough;
  vec3 N = geomN;

  if (hasGround) {
    // Macro + detail ground
    vec3 a1 = triplanarAlbedo(uGroundAlbedo, geomN, vWorldPos, tiling);
    vec3 a2 = triplanarAlbedo(uGroundAlbedo, geomN, vWorldPos, tiling * 3.7);
    albedo = mix(a1, a2, 0.28);
    N = triplanarNormal(uGroundNormal, geomN, vWorldPos, tiling);
    rough = triplanarRough(uGroundRough, geomN, vWorldPos, tiling);
    float lum = dot(albedo, vec3(0.3, 0.5, 0.2));
    albedo = mix(vec3(lum), albedo, 0.85);
    albedo = mix(albedo, vec3(0.2, 0.55, 0.7), 0.10);
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

  // Slope rock mix (steep faces → rock)
  float slope = 1.0 - clamp(geomN.y, 0.0, 1.0);
  float rockW = smoothstep(0.12, 0.52, slope);
  if (hasRock && rockW > 0.001) {
    vec3 ra = triplanarAlbedo(uRockAlbedo, geomN, vWorldPos, tiling * 0.85);
    vec3 rn = triplanarNormal(uRockNormal, geomN, vWorldPos, tiling * 0.85);
    float rr = triplanarRough(uRockRough, geomN, vWorldPos, tiling * 0.85);
    albedo = mix(albedo, ra, rockW * 0.92);
    N = normalize(mix(N, rn, rockW * 0.75));
    rough = mix(rough, rr, rockW);
  } else {
    albedo *= (1.0 - slope * 0.22);
  }

  // Path ribbon (wins over rock on corridor)
  float pMask = pathMaskAt(vWorldPos, pathHalf, pathEdge, meanderAmp);
  // Keep path out of steep cliffs a bit
  pMask *= smoothstep(0.55, 0.25, slope);
  if (hasPath && pMask > 0.001) {
    vec3 pa = triplanarAlbedo(uPathAlbedo, geomN, vWorldPos, tiling * 1.15);
    vec3 pn = triplanarNormal(uPathNormal, geomN, vWorldPos, tiling * 1.15);
    float pr = triplanarRough(uPathRough, geomN, vWorldPos, tiling * 1.15);
    // Brighter worn lane
    pa = mix(pa, pa * vec3(1.15, 1.18, 1.12), 0.35);
    albedo = mix(albedo, pa, pMask);
    N = normalize(mix(N, pn, pMask * 0.6));
    rough = mix(rough, mix(pr, 0.35, 0.4), pMask);
    // Soft center glow so sprint lane reads
    albedo += vec3(0.06, 0.14, 0.18) * pMask * pMask * 0.45;
  }

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
  col += vec3(0.1, 0.35, 0.5) * 0.08 * smoothstep(2.5, 8.0, vHeight);

  float dist = length(uFrame.cameraPos_time.xyz - vWorldPos);
  // Softer, farther fog so the patch edge is less of a hard “wall”
  float fog = smoothstep(70.0, 340.0, dist);
  vec3 sky = vec3(0.02, 0.04, 0.09);
  col = mix(col, sky, fog * 0.9);

  col = pow(max(col, 0.0), vec3(0.95));
  outColor = vec4(col, 1.0);
}
