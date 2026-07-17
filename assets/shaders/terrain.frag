#version 450
// Crystal biome — multi-material triplanar + atmosphere lighting

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;
layout(location = 3) in float vHeight;

layout(set = 0, binding = 0) uniform Frame {
  mat4 viewProj;
  vec4 cameraPos_time;
  vec4 sprintScore_flags; // x=score y=matFlags
  vec4 tiling_pad;        // x=tiling y=pathHalfW z=pathEdge w=meanderAmp
  mat4 invViewProj;
} uFrame;

layout(set = 0, binding = 2) uniform sampler2D uGroundAlbedo;
layout(set = 0, binding = 3) uniform sampler2D uGroundNormal;
layout(set = 0, binding = 4) uniform sampler2D uGroundRough;
layout(set = 0, binding = 5) uniform sampler2D uRockAlbedo;
layout(set = 0, binding = 6) uniform sampler2D uRockNormal;
layout(set = 0, binding = 7) uniform sampler2D uRockRough;
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
  return texture(tex, wp.zy * scale).rgb * b.x
       + texture(tex, wp.xz * scale).rgb * b.y
       + texture(tex, wp.xy * scale).rgb * b.z;
}

float triplanarRough(sampler2D tex, vec3 n, vec3 wp, float scale) {
  vec3 b = triplanarWeights(n);
  return texture(tex, wp.zy * scale).r * b.x
       + texture(tex, wp.xz * scale).r * b.y
       + texture(tex, wp.xy * scale).r * b.z;
}

vec3 triplanarNormal(sampler2D tex, vec3 geomN, vec3 wp, float scale) {
  vec3 b = triplanarWeights(geomN);
  vec3 nx = texture(tex, wp.zy * scale).xyz * 2.0 - 1.0;
  vec3 ny = texture(tex, wp.xz * scale).xyz * 2.0 - 1.0;
  vec3 nz = texture(tex, wp.xy * scale).xyz * 2.0 - 1.0;
  vec3 detail = normalize(nx * b.x + ny * b.y + nz * b.z);
  return normalize(mix(normalize(geomN), normalize(geomN + detail * 0.5), 0.7));
}

float pathMaskAt(vec3 wp, float halfW, float edge, float meanderAmp) {
  float meander = sin(wp.z * 0.045 + 0.7) * meanderAmp
                + sin(wp.z * 0.12 + 1.3) * (meanderAmp * 0.35);
  float d = abs(wp.x - meander);
  float w = halfW + sin(wp.z * 0.08) * 0.6;
  return 1.0 - smoothstep(w, w + edge, d);
}

// Atmosphere fog matching sky.frag
vec3 atmosphereColor(vec3 viewDir, float dist) {
  float elev = viewDir.y;
  float h = clamp(elev * 0.5 + 0.5, 0.0, 1.0);
  vec3 zenith  = vec3(0.02, 0.04, 0.09);
  vec3 midSky  = vec3(0.05, 0.12, 0.24);
  vec3 horizon = vec3(0.14, 0.38, 0.50);
  vec3 sky = mix(horizon, midSky, smoothstep(0.0, 0.45, h));
  sky = mix(sky, zenith, smoothstep(0.35, 0.95, h));
  float horizBand = exp(-abs(elev) * 7.0);
  sky += vec3(0.3, 0.65, 0.85) * horizBand * 0.35;
  // Distance density lifts color toward soft cyan haze
  float dens = 1.0 - exp(-dist * 0.0022);
  return mix(sky, vec3(0.18, 0.42, 0.55), dens * 0.35);
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
    vec3 a1 = triplanarAlbedo(uGroundAlbedo, geomN, vWorldPos, tiling);
    vec3 a2 = triplanarAlbedo(uGroundAlbedo, geomN, vWorldPos, tiling * 3.7);
    albedo = mix(a1, a2, 0.32);
    // Micro variation to break tiling
    float grit = fbm2(vWorldPos.xz * 0.11);
    albedo *= 0.92 + grit * 0.16;
    N = triplanarNormal(uGroundNormal, geomN, vWorldPos, tiling);
    rough = triplanarRough(uGroundRough, geomN, vWorldPos, tiling);
    float lum = dot(albedo, vec3(0.3, 0.5, 0.2));
    albedo = mix(vec3(lum), albedo, 0.88);
    albedo = mix(albedo, vec3(0.18, 0.52, 0.68), 0.08);
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
    albedo *= (1.0 - slope * 0.18);
  }

  float pMask = pathMaskAt(vWorldPos, pathHalf, pathEdge, meanderAmp);
  pMask *= smoothstep(0.55, 0.25, slope);
  if (hasPath && pMask > 0.001) {
    vec3 pa = triplanarAlbedo(uPathAlbedo, geomN, vWorldPos, tiling * 1.15);
    vec3 pn = triplanarNormal(uPathNormal, geomN, vWorldPos, tiling * 1.15);
    float pr = triplanarRough(uPathRough, geomN, vWorldPos, tiling * 1.15);
    pa = mix(pa, pa * vec3(1.18, 1.22, 1.14), 0.4);
    albedo = mix(albedo, pa, pMask);
    N = normalize(mix(N, pn, pMask * 0.6));
    rough = mix(rough, mix(pr, 0.32, 0.45), pMask);
    albedo += vec3(0.08, 0.18, 0.22) * pMask * pMask * 0.5;
  }

  // —— Lighting ——
  vec3 L1 = normalize(vec3(0.4, 0.95, 0.22));   // key sun
  vec3 L2 = normalize(vec3(-0.55, 0.35, -0.35)); // cool fill
  vec3 L3 = normalize(vec3(0.1, 0.2, 0.9));      // rim / bounce

  float ndl1 = max(dot(N, L1), 0.0);
  // Soft wrap lighting
  float wrap = clamp((dot(N, L1) + 0.35) / 1.35, 0.0, 1.0);
  float ndl2 = max(dot(N, L2), 0.0) * 0.35;
  float ndl3 = max(dot(N, L3), 0.0) * 0.18;

  // Hemisphere ambient (sky vs ground bounce)
  float hemi = N.y * 0.5 + 0.5;
  vec3 ambient = mix(vec3(0.05, 0.07, 0.10), vec3(0.12, 0.22, 0.32), hemi);
  // Fake AO in creases / slopes
  float ao = mix(0.55, 1.0, clamp(geomN.y * 0.85 + 0.15, 0.0, 1.0));
  ao *= mix(1.0, 0.82, rockW * 0.6);

  vec3 V = normalize(uFrame.cameraPos_time.xyz - vWorldPos);
  vec3 H = normalize(L1 + V);
  float gloss = mix(110.0, 10.0, rough);
  float spec = pow(max(dot(N, H), 0.0), gloss) * (1.0 - rough) * 0.55;
  // Fresnel-ish rim
  float fres = pow(1.0 - max(dot(N, V), 0.0), 3.0);
  vec3 rim = vec3(0.25, 0.55, 0.75) * fres * 0.35;

  vec3 sunCol = vec3(1.0, 0.97, 0.92);
  vec3 fillCol = vec3(0.45, 0.7, 0.95);
  vec3 col = ambient * albedo * ao;
  col += albedo * sunCol * (0.42 * ndl1 + 0.28 * wrap);
  col += albedo * fillCol * (ndl2 + ndl3);
  col += sunCol * spec + rim;
  // Height emissive veins
  col += vec3(0.08, 0.28, 0.42) * 0.1 * smoothstep(2.0, 9.0, vHeight);
  // Wet path specular boost
  col += vec3(0.5, 0.85, 1.0) * spec * pMask * 0.35;

  float dist = length(uFrame.cameraPos_time.xyz - vWorldPos);
  float fog = 1.0 - exp(-dist * 0.0045);
  fog = smoothstep(0.0, 1.0, fog);
  vec3 fogCol = atmosphereColor(V, dist);
  // Height fog: lower ground hazes more
  float hFog = exp(-max(vWorldPos.y, 0.0) * 0.08) * 0.25;
  fog = clamp(fog + hFog * fog, 0.0, 0.95);
  col = mix(col, fogCol, fog * 0.92);

  // ACES-ish tone map + slight contrast
  col = col * (2.51 * col + 0.03) / (col * (2.43 * col + 0.59) + 0.14);
  col = pow(max(col, 0.0), vec3(0.95));
  // Subtle vignette in post (screen-ish via view)
  outColor = vec4(col, 1.0);
}
