// Shared atmosphere: volumetric fog + light shafts (offline, no APIs)
// Tuned light — depth cue only, not a purple wall
#ifndef BOLT_ATMOS_EVAL_GLSL
#define BOLT_ATMOS_EVAL_GLSL

float atmosHash21(vec2 p) {
  return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

/** Height + soft noise density — sparse haze, not soup. */
float atmosDensity(vec3 p, float time, float score) {
  float h = p.y;
  // Thin ground hug (mostly valleys)
  float ground = exp(-max(h - 0.6, 0.0) * 0.35) * 0.45;
  // Soft mid sheet
  float mid = exp(-abs(h - 8.0) * 0.15) * 0.22;
  // Very light high wisps
  float high = exp(-max(h - 22.0, 0.0) * 0.1) * 0.08;
  float dens = ground + mid + high;
  float n = atmosHash21(p.xz * 0.03 + time * 0.01);
  n += 0.4 * atmosHash21(p.xz * 0.08 - time * 0.015 + p.y * 0.04);
  dens *= 0.65 + 0.35 * n;
  // Mild score boost only
  dens *= 0.85 + score * 0.12;
  // Was 0.055 — way too thick; keep subtle
  return dens * 0.012;
}

/** Henyey–Greenstein-ish sun phase (forward scatter for shafts). */
float atmosPhase(float cosTheta, float g) {
  float g2 = g * g;
  float denom = max(1.0 + g2 - 2.0 * g * cosTheta, 1e-3);
  return (1.0 - g2) / (4.0 * 3.14159265 * pow(denom, 1.5));
}

/**
 * Ray-march volumetric fog from camera to surface (or far sky).
 * Light touch: clear near field, soft haze only at distance.
 */
vec3 applyVolumetricFog(vec3 col, vec3 cam, vec3 rd, float dist, vec3 sunDir, vec3 fogSky,
                        float time, float score, int steps) {
  dist = clamp(dist, 0.0, 160.0);
  steps = max(steps, 4);
  float stepLen = dist / float(steps);
  // Mild extinction — was 0.85+score*0.45
  float extScale = 0.32 + score * 0.12;
  float sunScatter = 0.18 + score * 0.12;
  vec3 sunCol = mix(vec3(0.7, 0.45, 0.95), vec3(0.45, 0.8, 1.05), clamp(score * 0.35, 0.0, 1.0));

  vec3 fogAccum = vec3(0.0);
  float transmittance = 1.0;
  // Skip first meters — no camera haze wall
  float t0 = min(4.0, dist * 0.06);

  for (int i = 0; i < 24; ++i) {
    if (i >= steps) break;
    float t = t0 + (float(i) + 0.5) * stepLen;
    if (t >= dist) break;
    vec3 p = cam + rd * t;
    float dens = atmosDensity(p, time, score);
    float dt = stepLen * extScale;
    float extinction = dens * dt;
    float cosT = dot(rd, sunDir);
    float phase = atmosPhase(cosT, 0.48);
    vec3 ambient = fogSky * dens * 0.35;
    vec3 shafts = sunCol * dens * phase * sunScatter;
    vec3 inScat = (ambient + shafts) * dt;
    fogAccum += transmittance * inScat;
    transmittance *= exp(-extinction);
    if (transmittance < 0.05) break;
  }

  // Free air until ~40m, then ramp fog in
  float nearKeep = smoothstep(28.0, 55.0, dist);
  float apply = nearKeep * 0.72; // never full override
  return mix(col, col * transmittance + fogAccum, apply);
}

/** Forward props / Bolt — same light fog model. */
vec3 applyVolumetricFogFast(vec3 col, vec3 cam, vec3 worldPos, vec3 sunDir, vec3 fogSky,
                            float time, float score) {
  vec3 ray = worldPos - cam;
  float dist = length(ray);
  if (dist < 0.05) return col;
  vec3 rd = ray / dist;
  return applyVolumetricFog(col, cam, rd, dist, sunDir, fogSky, time, score, 5);
}

#endif
