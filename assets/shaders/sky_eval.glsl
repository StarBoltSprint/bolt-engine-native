// Shared Crystal Nebula sky evaluation — used by sky.frag + deferred_light.frag
// Pass 5: denser micro-detail — stars, flecks, fine nebula, dust
#ifndef BOLT_SKY_EVAL_GLSL
#define BOLT_SKY_EVAL_GLSL

float skyHash21(vec2 p) {
  return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

float skyHash31(vec3 p) {
  return fract(sin(dot(p, vec3(127.1, 311.7, 74.7))) * 43758.5453);
}

float skyNoise2(vec2 p) {
  vec2 i = floor(p);
  vec2 f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  float a = skyHash21(i);
  float b = skyHash21(i + vec2(1.0, 0.0));
  float c = skyHash21(i + vec2(0.0, 1.0));
  float d = skyHash21(i + vec2(1.0, 1.0));
  return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float skyFbm(vec2 p) {
  float v = 0.0;
  float a = 0.5;
  mat2 m = mat2(0.8, -0.6, 0.6, 0.8);
  for (int i = 0; i < 5; ++i) {
    v += a * skyNoise2(p);
    p = m * p * 2.05 + vec2(1.7, 9.2);
    a *= 0.5;
  }
  return v;
}

float skyFbmWarp(vec2 p, float t, float swirl) {
  vec2 q = vec2(skyFbm(p + vec2(0.0, t * 0.15)), skyFbm(p + vec2(5.2, t * 0.12)));
  vec2 r = vec2(skyFbm(p + 1.5 * q + vec2(1.7 + t * 0.08, 9.2)),
                skyFbm(p + 1.5 * q + vec2(8.3, 2.8 - t * 0.06)));
  float ang = swirl * 0.35 + t * 0.02 * swirl;
  float ca = cos(ang), sa = sin(ang);
  r = mat2(ca, -sa, sa, ca) * r;
  return skyFbm(p + 1.8 * r);
}

/** Soft star disk from cell hash (not just 1-pixel step) */
float starDisk(vec2 cellUV, float thresh, float soft) {
  float h = skyHash21(floor(cellUV));
  if (h < thresh) return 0.0;
  vec2 f = fract(cellUV) - 0.5;
  // Jitter center per cell
  f += (vec2(skyHash21(floor(cellUV) + 19.7), skyHash21(floor(cellUV) + 91.3)) - 0.5) * 0.35;
  float d = length(f);
  float size = mix(0.018, 0.055, fract(h * 17.13));
  return (1.0 - smoothstep(size * soft, size * 2.2, d)) * smoothstep(thresh, thresh + 0.04, h);
}

/** View ray from NDC uv via invViewProj (near→far). */
vec3 skyRayFromInvVP(mat4 invViewProj, vec2 uv) {
  vec2 ndc = uv * 2.0 - 1.0;
  vec4 nearP = invViewProj * vec4(ndc, 0.0, 1.0);
  vec4 farP  = invViewProj * vec4(ndc, 1.0, 1.0);
  nearP.xyz /= max(nearP.w, 1e-6);
  farP.xyz  /= max(farP.w, 1e-6);
  return normalize(farP.xyz - nearP.xyz);
}

/**
 * Full Crystal Nebula sky (SkyGenerator visuals).
 * rd = view ray direction, t = time, score = sprint 0..1.4, skyE = energy pack 0..1.5
 * applyVignetteUv: pass vUV for vignette, or vec2(0.5) to skip screen vignette
 */
vec3 evaluateCrystalSky(vec3 rd, float t, float score, float skyE, vec2 applyVignetteUv) {
  score = clamp(score, 0.0, 1.4);
  skyE = clamp(skyE, 0.0, 1.5);

  float nebulaSpeed = 0.08 + score * 0.28 + skyE * 0.1;
  float nebulaBright = 0.95 + score * 0.65 + skyE * 0.35;
  float swirl = 0.35 + score * 0.65 + skyE * 0.25;
  float streamI = 0.35 + score * 0.85 + skyE * 0.3;
  float starV = 0.95 + score * 0.6 + skyE * 0.25;
  float cyanShift = clamp(score * 0.5 + skyE * 0.25, 0.0, 1.0);
  float aurora = max(0.0, (score - 0.7) * 2.4) + max(0.0, skyE - 0.85) * 0.45;
  float horizGlow = 0.65 + score * 0.45;

  float elev = rd.y;
  float h = clamp(elev * 0.5 + 0.5, 0.0, 1.0);

  vec3 zenith  = vec3(0.04, 0.015, 0.1);
  vec3 midDeep = vec3(0.1, 0.04, 0.22);
  vec3 midTeal = mix(vec3(0.07, 0.12, 0.3), vec3(0.05, 0.18, 0.34), cyanShift);
  vec3 horizon = mix(vec3(0.36, 0.12, 0.42), vec3(0.18, 0.22, 0.46), cyanShift * 0.45);
  vec3 glowMag = vec3(0.8, 0.32, 0.9);
  vec3 glowCyan = vec3(0.3, 0.82, 1.0);

  vec3 col = mix(horizon, midDeep, smoothstep(0.0, 0.38, h));
  col = mix(col, midTeal, smoothstep(0.18, 0.7, h) * 0.55);
  col = mix(col, zenith, smoothstep(0.4, 0.98, h));

  float horizBand = exp(-abs(elev) * 4.8);
  col += glowMag * horizBand * (0.7 * horizGlow);
  col += glowCyan * horizBand * (0.35 + cyanShift * 0.25);
  col = mix(col, horizon * 1.4, horizBand * 0.25);

  // —— Swirling nebula (large + medium + micro wisps) ——
  vec2 cloudUV = rd.xz / max(rd.y + 0.42, 0.1);
  cloudUV += vec2(t * 0.005 * nebulaSpeed * 14.0, t * 0.0035 * nebulaSpeed * 14.0);
  cloudUV += vec2(sin(t * 0.07 + cloudUV.y * 0.5), cos(t * 0.05)) * swirl * 0.1;

  float c1 = skyFbmWarp(cloudUV * 0.95, t * nebulaSpeed * 8.0, swirl);
  float c2 = skyFbmWarp(cloudUV * 1.9 + 17.0, t * nebulaSpeed * 6.0 + 3.0, swirl * 0.8);
  float c3 = skyFbm(cloudUV * 3.2 + 40.0 + t * nebulaSpeed * 2.0);
  float c4 = skyFbm(cloudUV * 7.5 + 88.0 + t * nebulaSpeed * 3.5); // micro filaments
  float clouds = smoothstep(0.22, 0.75, c1);
  float wisps = smoothstep(0.3, 0.85, c2);
  float fine = smoothstep(0.38, 0.9, c3);
  float micro = smoothstep(0.42, 0.92, c4);
  float cloudMask = smoothstep(-0.15, 0.55, elev);

  vec3 nebPurple = vec3(0.55, 0.15, 0.65);
  vec3 nebPink   = vec3(0.9, 0.4, 0.8);
  vec3 nebTeal   = vec3(0.18, 0.6, 0.75);
  vec3 nebCyan   = vec3(0.35, 0.85, 1.05);
  nebPurple = mix(nebPurple, nebTeal, cyanShift * 0.4);
  nebPink = mix(nebPink, nebCyan, cyanShift * 0.35);

  col += nebPurple * clouds * cloudMask * 0.75 * nebulaBright;
  col += nebTeal * wisps * cloudMask * 0.55 * nebulaBright;
  col += nebPink * pow(max(clouds, 0.0), 1.8) * cloudMask * 0.32 * nebulaBright;
  col += nebCyan * fine * cloudMask * (0.18 + cyanShift * 0.12) * nebulaBright;
  // Micro filaments — thin bright threads in the clouds
  col += mix(nebPink, nebCyan, 0.5) * micro * cloudMask * 0.22 * nebulaBright;
  // Soft dust grain in dark sky pockets (not stars)
  float dust = skyNoise2(rd.xz * 48.0 + t * 0.02) * skyNoise2(rd.xy * 36.0 - t * 0.015);
  dust = smoothstep(0.55, 0.95, dust) * smoothstep(0.1, 0.75, elev);
  col += vec3(0.35, 0.25, 0.55) * dust * 0.08 * (0.7 + skyE * 0.3);

  // —— Energy streams ——
  float streamCoord = rd.x * 1.8 + rd.z * 0.9 + elev * 0.5;
  float streamFlow = streamCoord - t * (0.1 + score * 0.15);
  float stream1 = pow(1.0 - abs(sin(streamFlow * 2.2 + skyFbm(rd.xz * 3.0) * 1.5)), 7.0);
  float stream2 = pow(1.0 - abs(sin(streamFlow * 1.1 + 2.5 + skyFbm(rd.xz * 2.0 + 5.0))), 9.0);
  float streamMask = smoothstep(0.0, 0.6, elev) * (0.5 + streamI);
  col += vec3(0.3, 0.9, 1.1) * stream1 * streamMask * 0.35 * streamI;
  col += vec3(0.5, 0.6, 1.0) * stream2 * streamMask * 0.22 * streamI;
  float branch = pow(1.0 - abs(sin(rd.x * 4.0 + rd.y * 2.0 - t * 0.15)), 12.0);
  col += vec3(0.4, 0.8, 1.0) * branch * streamMask * 0.14 * streamI;
  // Micro streamlets (thinner, denser)
  float stream3 = pow(1.0 - abs(sin(streamFlow * 4.5 + skyFbm(rd.yz * 5.0) * 2.0)), 14.0);
  col += vec3(0.45, 0.85, 1.15) * stream3 * streamMask * 0.1 * streamI;

  // Shafts
  float angS = atan(rd.x, rd.z);
  float shafts = pow(max(sin(angS * 2.5 + t * 0.1 * (1.0 + score)) * 0.5 + 0.5, 0.0), 8.0);
  shafts *= smoothstep(0.0, 0.55, elev) * (0.45 + score * 0.4 + skyE * 0.2);
  col += mix(vec3(0.6, 0.5, 0.95), vec3(0.45, 0.8, 1.05), cyanShift) * shafts * 0.32;

  // Sun
  vec3 sunDir = normalize(vec3(0.32, 0.52 + score * 0.06, 0.42));
  float sun = pow(max(dot(rd, sunDir), 0.0), 24.0);
  float sunHalo = pow(max(dot(rd, sunDir), 0.0), 2.8);
  col += vec3(1.05, 0.85, 1.0) * sun * (0.7 + score * 0.3);
  col += mix(vec3(0.6, 0.4, 0.9), vec3(0.45, 0.75, 1.0), cyanShift) * sunHalo * 0.45;

  // —— Star field (multi-scale disks that survive ACES) ——
  float elevStar = smoothstep(0.12, 0.85, elev);
  // Spherical-ish UV for even distribution
  vec2 sph = vec2(atan(rd.x, rd.z) / 6.28318 + 0.5, asin(clamp(rd.y, -1.0, 1.0)) / 3.14159 + 0.5);

  // Dense background stars
  float sDense = starDisk(sph * 420.0 + 3.1, 0.991, 0.7);
  float tw1 = 0.55 + 0.45 * sin(t * 2.8 + skyHash21(floor(sph * 420.0)) * 50.0);
  col += vec3(0.92, 0.9, 1.08) * sDense * elevStar * tw1 * 0.95 * starV;

  // Medium layer
  float sMed = starDisk(sph * 220.0 + 11.0, 0.9935, 0.85);
  float tw2 = 0.5 + 0.5 * sin(t * 1.9 + skyHash21(floor(sph * 220.0 + 5.0)) * 33.0);
  col += vec3(0.95, 0.92, 1.05) * sMed * elevStar * tw2 * 1.15 * starV;

  // Sparse bright jewels
  float sBright = starDisk(sph * 110.0 + 41.0, 0.9965, 1.1);
  float tw3 = 0.45 + 0.55 * sin(t * 3.5 + skyHash21(floor(sph * 110.0)) * 60.0);
  col += vec3(1.0, 0.95, 1.1) * sBright * elevStar * tw3 * 1.45 * starV;
  // Cross glint on brightest
  if (sBright > 0.4) {
    vec2 f = fract(sph * 110.0) - 0.5;
    float cross = max(1.0 - abs(f.x) * 28.0, 0.0) * max(1.0 - abs(f.y) * 9.0, 0.0);
    cross += max(1.0 - abs(f.y) * 28.0, 0.0) * max(1.0 - abs(f.x) * 9.0, 0.0);
    col += vec3(0.85, 0.9, 1.15) * cross * sBright * 0.35 * elevStar * starV;
  }

  // Crystal flecks (colored sparkles, not pure white)
  float fleckA = starDisk(sph * 280.0 + t * 0.015 + 7.0, 0.994, 0.9);
  float fleckB = starDisk(rd.xz * 190.0 + rd.y * 40.0 + 2.2, 0.995, 0.75);
  col += vec3(0.55, 0.9, 1.2) * fleckA * elevStar * 1.1 * starV;
  col += vec3(0.85, 0.55, 1.15) * fleckB * elevStar * 0.9 * starV;

  // Tiny dust sparkle field (very dense, low intensity)
  float microStar = step(0.9978, skyHash31(floor(rd * 520.0)));
  col += vec3(0.8, 0.85, 1.0) * microStar * elevStar * 0.55 * starV;

  // Distant galaxies / nebula smudges
  float gal = smoothstep(0.62, 0.95, skyFbm(rd.xz * 1.4 + 30.0));
  gal *= smoothstep(0.25, 0.9, elev) * 0.22;
  col += mix(vec3(0.5, 0.22, 0.65), vec3(0.25, 0.45, 0.65), cyanShift) * gal * (1.0 + score * 0.35);
  // Second galaxy layer
  float gal2 = smoothstep(0.7, 0.96, skyFbm(rd.xy * 2.1 + 55.0 + rd.z));
  gal2 *= smoothstep(0.4, 0.95, elev) * 0.12;
  col += vec3(0.4, 0.2, 0.55) * gal2 * (0.8 + cyanShift * 0.4);

  // Aurora bands (high sprint)
  if (aurora > 0.01) {
    float aur = sin(rd.x * 6.0 + t * (0.4 + score) + elev * 8.0);
    aur = pow(max(aur * 0.5 + 0.5, 0.0), 3.2);
    aur *= smoothstep(0.08, 0.7, elev) * aurora;
    col += vec3(0.35, 0.9, 0.8) * aur * 0.45;
    col += vec3(0.6, 0.4, 1.0) * aur * 0.3;
    // Fine aurora filaments
    float fil = pow(max(sin(rd.x * 18.0 + elev * 22.0 - t * 1.2) * 0.5 + 0.5, 0.0), 8.0);
    col += vec3(0.4, 0.95, 0.85) * fil * aur * 0.25;
  }

  if (score > 0.9) {
    float streak = pow(max(dot(normalize(rd.xz + 1e-4), vec2(0.0, 1.0)), 0.0), 36.0);
    streak *= (score - 0.9) * 2.0 * elevStar;
    col += vec3(0.65, 0.9, 1.05) * streak * 0.2;
  }

  col *= 0.95 + score * 0.1;

  if (abs(applyVignetteUv.x - 0.5) > 0.001 || abs(applyVignetteUv.y - 0.5) > 0.001) {
    vec2 q = applyVignetteUv - 0.5;
    col *= 1.0 - dot(q, q) * 0.26;
  }

  float b = max(dot(col, vec3(0.3, 0.5, 0.2)) - 0.4, 0.0);
  col += col * b * 0.18 * horizBand;

  // Mild compress — slightly less crush so micro stars survive post ACES
  col = col / (col + vec3(0.88));
  col = pow(max(col, 0.0), vec3(0.9));
  return col;
}

#endif
