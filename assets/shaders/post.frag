#version 450
// Post: depth-reprojected TAA + velocity motion blur + bloom + ACES + vignette

layout(location = 0) in vec2 vUV;
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D uScene;
layout(set = 0, binding = 1) uniform PostUBO {
  vec4 res_time;        // xy invRes, z time, w score
  vec4 near_far_mb_taa; // x near, y far, z motionBlur 0-1, w taa history weight
  mat4 invViewProj;
  mat4 prevViewProj;
  vec4 jitter;          // xy curr NDC, zw prev NDC
  vec4 sun_ray;         // xy sun UV, z godRay strength, w grade strength
} uPost;
layout(set = 0, binding = 2) uniform sampler2D uDepth;
layout(set = 0, binding = 3) uniform sampler2D uHistory;

float hash21(vec2 p) {
  return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

vec3 aces(vec3 x) {
  return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0);
}

float luma(vec3 c) {
  return dot(c, vec3(0.2126, 0.7152, 0.0722));
}

// Reconstruct world position from Vulkan depth (0..1) and UV
vec3 worldFromDepth(vec2 uv, float depth) {
  // Clip: xy NDC, z depth. Our proj flips Y.
  vec2 ndc = uv * 2.0 - 1.0;
  vec4 clip = vec4(ndc, depth, 1.0);
  vec4 wpos = uPost.invViewProj * clip;
  return wpos.xyz / max(wpos.w, 1e-6);
}

vec2 projectToUv(vec3 world) {
  vec4 clip = uPost.prevViewProj * vec4(world, 1.0);
  vec3 ndc = clip.xyz / max(clip.w, 1e-6);
  return ndc.xy * 0.5 + 0.5;
}

vec3 clipAABB(vec3 q, vec3 aabbMin, vec3 aabbMax) {
  vec3 p = 0.5 * (aabbMax + aabbMin);
  vec3 e = 0.5 * (aabbMax - aabbMin);
  vec3 v = q - p;
  vec3 t = e / max(abs(v), vec3(1e-4));
  float m = min(min(t.x, t.y), t.z);
  return m < 1.0 ? p + v * m : q;
}

void main() {
  vec2 texel = uPost.res_time.xy;
  float t = uPost.res_time.z;
  float score = uPost.res_time.w;
  float zNear = uPost.near_far_mb_taa.x;
  float zFar = uPost.near_far_mb_taa.y;
  float mbStr = clamp(uPost.near_far_mb_taa.z, 0.0, 1.0);
  float taaW = clamp(uPost.near_far_mb_taa.w, 0.0, 0.95);

  vec3 current = texture(uScene, vUV).rgb;
  float depth = texture(uDepth, vUV).r;

  // --- Velocity via depth reprojection ---
  vec3 world = worldFromDepth(vUV, depth);
  vec2 histUv = projectToUv(world);
  // Un-jitter history UV slightly (use prev jitter offset)
  // histUv already from prevViewProj (unjittered matrices preferred from CPU)

  vec2 velocity = histUv - vUV;
  // Reject huge velocities (disocclusion / sky)
  float vLen = length(velocity);
  bool validHist = histUv.x > 0.0 && histUv.x < 1.0 && histUv.y > 0.0 && histUv.y < 1.0 &&
                   depth < 0.9999 && vLen < 0.12;

  // --- Neighborhood clamp (3x3) ---
  vec3 nMin = current;
  vec3 nMax = current;
  vec3 nAvg = current * 0.2;
  for (int y = -1; y <= 1; ++y) {
    for (int x = -1; x <= 1; ++x) {
      if (x == 0 && y == 0) continue;
      vec3 s = texture(uScene, vUV + vec2(float(x), float(y)) * texel).rgb;
      nMin = min(nMin, s);
      nMax = max(nMax, s);
      nAvg += s * 0.1;
    }
  }

  vec3 history = texture(uHistory, histUv).rgb;
  history = clipAABB(history, nMin, nMax);

  // Dilute TAA when moving fast (less ghosting)
  float speedKill = smoothstep(0.02, 0.08, vLen);
  float histBlend = taaW * (1.0 - speedKill * 0.65) * (validHist ? 1.0 : 0.0);
  // Sky / far: less history
  histBlend *= (1.0 - smoothstep(0.98, 1.0, depth));

  vec3 taa = mix(current, history, histBlend);

  // --- Motion blur (camera + sprint velocity) ---
  vec3 blurred = taa;
  if (mbStr > 0.02 && vLen > 0.0005 && validHist) {
    // Sample along velocity (light directional blur for sprint)
    vec2 dir = velocity * (0.65 + mbStr * 1.4);
    const int SAMPLES = 6;
    vec3 acc = taa;
    float wsum = 1.0;
    for (int i = 1; i <= SAMPLES; ++i) {
      float f = float(i) / float(SAMPLES);
      // Symmetric-ish trail
      vec2 o1 = dir * f * 0.5;
      vec2 o2 = -dir * f * 0.25;
      float w = 1.0 - f * 0.7;
      acc += texture(uScene, clamp(vUV + o1, vec2(0.001), vec2(0.999))).rgb * w;
      acc += texture(uScene, clamp(vUV + o2, vec2(0.001), vec2(0.999))).rgb * w * 0.6;
      wsum += w * 1.6;
    }
    blurred = mix(taa, acc / wsum, clamp(mbStr * 0.85 + score * 0.1, 0.0, 0.85));
  }

  // --- Bloom (tight threshold — only path/emissive peaks, not whole floor) ---
  vec3 bloom = vec3(0.0);
  float wsumB = 0.0;
  float thr = 0.62;
  float l0 = luma(blurred);
  if (l0 > thr) {
    bloom += blurred * (l0 - thr);
    wsumB += 1.0;
  }
  for (int i = 0; i < 4; ++i) {
    float o = 1.5 + float(i) * 2.2;
    float w = 0.45 / (1.0 + float(i) * 0.7);
    vec2 d = texel * o;
    vec3 avg =
        (texture(uScene, vUV + vec2(d.x, 0.0)).rgb + texture(uScene, vUV - vec2(d.x, 0.0)).rgb +
         texture(uScene, vUV + vec2(0.0, d.y)).rgb + texture(uScene, vUV - vec2(0.0, d.y)).rgb +
         texture(uScene, vUV + d).rgb + texture(uScene, vUV - d).rgb) *
        0.1667;
    float l = luma(avg);
    bloom += avg * max(l - thr, 0.0) * w;
    wsumB += w;
  }
  bloom /= max(wsumB, 1e-3);
  bloom *= vec3(1.05, 0.95, 1.12);

  // --- SSAO punch (stronger contact darkening under trees / Bolt / rocks) ---
  float ao = 1.0;
  if (depth < 0.999) {
    vec3 pos = world;
    vec3 pR = worldFromDepth(vUV + vec2(texel.x, 0.0), texture(uDepth, vUV + vec2(texel.x, 0.0)).r);
    vec3 pU = worldFromDepth(vUV + vec2(0.0, texel.y), texture(uDepth, vUV + vec2(0.0, texel.y)).r);
    vec3 N = normalize(cross(pR - pos, pU - pos));
    float occ = 0.0;
    const int K = 12;
    for (int i = 0; i < K; ++i) {
      float fi = float(i);
      float ang = fi * 2.399963 + t * 0.55;
      // Wider radius for readable contact under props
      float rad = (0.55 + fract(hash21(vUV * 40.0 + fi)) * 1.75) * (0.4 + score * 0.08);
      vec2 off = vec2(cos(ang), sin(ang)) * texel * rad * 36.0;
      float sd = texture(uDepth, clamp(vUV + off, vec2(0.001), vec2(0.999))).r;
      vec3 sp = worldFromDepth(vUV + off, sd);
      vec3 v = sp - pos;
      float dist = length(v);
      float nd = max(dot(N, v / max(dist, 1e-4)), 0.0);
      float range = smoothstep(2.8, 0.1, dist);
      occ += nd * range;
    }
    // Stronger darken floor (up to ~82% occlusion)
    ao = 1.0 - clamp(occ / float(K) * 1.75, 0.0, 0.82);
    ao = mix(ao, 1.0, smoothstep(0.94, 0.999, depth));
  }
  // Deeper crevices / under trees / against Bolt feet
  blurred *= mix(0.38, 1.0, ao);

  // Mild bloom — path/crystals + sky peaks (god rays feed separately)
  float bloomStr = 0.18 + score * 0.2 + mbStr * 0.06;
  vec3 col = blurred + bloom * bloomStr;

  // --- God rays (SkyGenerator-driven volumetric shafts) ---
  vec2 sunUv = clamp(uPost.sun_ray.xy, vec2(-0.2), vec2(1.2));
  // sun_ray.z already includes SkyGenerator godRayStrength
  float rayStr = clamp(uPost.sun_ray.z, 0.0, 1.5) * (0.4 + score * 0.35);
  if (rayStr > 0.02) {
    vec2 delta = (sunUv - vUV) / 12.0;
    vec2 uvR = vUV;
    vec3 rays = vec3(0.0);
    float decay = 1.0;
    // Only accumulate bright samples (occlusion-ish)
    for (int i = 0; i < 12; ++i) {
      uvR += delta;
      if (uvR.x < 0.0 || uvR.x > 1.0 || uvR.y < 0.0 || uvR.y > 1.0) break;
      vec3 s = texture(uScene, uvR).rgb;
      float br = max(luma(s) - 0.35, 0.0);
      // Prefer sky / distant (high depth) for shafts
      float d = texture(uDepth, uvR).r;
      float skyW = smoothstep(0.92, 0.999, d);
      rays += s * br * decay * mix(0.35, 1.0, skyW);
      decay *= 0.88;
    }
    rays *= vec3(1.15, 0.95, 1.25); // magenta-cyan shaft tint
    col += rays * rayStr * 0.085;
  }

  // --- Score-reactive color grade (Crystal Nebula intensifies with sprint) ---
  float grade = clamp(uPost.sun_ray.w, 0.0, 1.5) * (0.35 + score * 0.7);
  float L = luma(col);
  // Shadows: deeper purple (stronger — restores contrast after wash fix)
  col = mix(col, col * vec3(1.08, 0.82, 1.18), (1.0 - smoothstep(0.0, 0.32, L)) * (0.32 + grade * 0.28));
  // Mids: teal, not lift toward white
  col = mix(col, col * vec3(0.88, 1.08, 1.12), smoothstep(0.12, 0.55, L) * (0.1 + grade * 0.14));
  // Highlights: controlled cyan energy (path/Bolt)
  col = mix(col, col * vec3(1.04, 1.02, 0.98) + vec3(0.02, 0.05, 0.08) * score,
            smoothstep(0.55, 0.95, L) * (0.08 + grade * 0.14));
  // Saturation boost with score
  col = mix(vec3(L), col, 1.0 + grade * 0.22 + 0.08);

  // Slight exposure pull-down so dark floor stays dark; tiny score celebration
  col *= 0.88 + score * 0.04;

  // Single ACES tonemap (forward/deferred write linear HDR)
  col = aces(col);
  // Mild contrast (gamma slightly >1 darkens mids)
  col = pow(max(col, 0.0), vec3(1.05));

  vec2 q = vUV - 0.5;
  // Softer vignette when sprinting (more open feel)
  float vigAmt = mix(1.05, 0.8, score);
  col *= mix(0.62, 1.0, clamp(1.0 - dot(q, q) * vigAmt, 0.0, 1.0));
  col += (hash21(vUV * 1100.0 + t * 0.8) - 0.5) * 0.012;

  outColor = vec4(clamp(col, 0.0, 1.0), 1.0);
}
