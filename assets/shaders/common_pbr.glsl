// Shared PBR helpers — IBL + normal/roughness quality. glslc -I assets/shaders
#ifndef BOLT_COMMON_PBR_GLSL
#define BOLT_COMMON_PBR_GLSL

// Both normals in -1..1 (map space). Whiteout: add xy, keep base z.
vec3 blendDetailNormalTS(vec3 baseN, vec3 detailN, float strength) {
  vec3 n;
  n.xy = baseN.xy + detailN.xy * strength;
  n.z = max(baseN.z, 0.05);
  return normalize(n);
}

// Remap roughness: expand contrast, clamp to material range
float remapRoughness(float r, float minR, float maxR, float contrast) {
  r = clamp(r, 0.0, 1.0);
  r = clamp((r - 0.5) * contrast + 0.5, 0.0, 1.0);
  return mix(minR, maxR, r);
}

// Micro detail normal from procedural noise (fills weak maps)
vec3 proceduralMicroNormal(vec3 geomN, vec3 wp, float scale, float strength) {
  // Finite-difference on hash height field
  float e = 0.08 / max(scale, 0.01);
  float h0 = fract(sin(dot(wp.xz, vec2(127.1, 311.7))) * 43758.5453);
  float hx = fract(sin(dot(wp.xz + vec2(e, 0.0), vec2(127.1, 311.7))) * 43758.5453);
  float hz = fract(sin(dot(wp.xz + vec2(0.0, e), vec2(127.1, 311.7))) * 43758.5453);
  // Second octave
  h0 += 0.45 * fract(sin(dot(wp.xz * 3.1, vec2(269.5, 183.3))) * 43758.5453);
  hx += 0.45 * fract(sin(dot((wp.xz + vec2(e, 0.0)) * 3.1, vec2(269.5, 183.3))) * 43758.5453);
  hz += 0.45 * fract(sin(dot((wp.xz + vec2(0.0, e)) * 3.1, vec2(269.5, 183.3))) * 43758.5453);
  vec3 t = normalize(cross(abs(geomN.y) < 0.99 ? vec3(0,1,0) : vec3(1,0,0), geomN));
  vec3 b = cross(geomN, t);
  float dx = (hx - h0) * strength;
  float dz = (hz - h0) * strength;
  return normalize(geomN - t * dx - b * dz);
}

// "HDRI-like" procedural environment — richer contrast than flat ambient
vec3 envSkyColor(vec3 dir) {
  vec3 d = normalize(dir);
  float elev = d.y;
  float h = clamp(elev * 0.5 + 0.5, 0.0, 1.0);
  // Darker zenith, controlled nebula horizon (avoid IBL white-wash on ground)
  vec3 zenith  = vec3(0.03, 0.015, 0.08);
  vec3 midSky  = vec3(0.09, 0.05, 0.18);
  vec3 horizon = vec3(0.28, 0.14, 0.38);
  vec3 sky = mix(horizon, midSky, smoothstep(0.0, 0.45, h));
  sky = mix(sky, zenith, smoothstep(0.35, 0.95, h));
  float horizBand = exp(-abs(elev) * 5.5);
  sky += vec3(0.5, 0.28, 0.75) * horizBand * 0.45;
  sky += vec3(0.15, 0.5, 0.7) * horizBand * 0.22;
  // Ground / under-canopy bounce (deep teal — dark floor bounce)
  sky = mix(sky, vec3(0.02, 0.05, 0.07), smoothstep(0.15, -0.35, elev) * 0.92);
  vec3 sunDir = normalize(vec3(0.35, 0.88, 0.35));
  float sun = pow(max(dot(d, sunDir), 0.0), 56.0);
  sky += vec3(1.0, 0.92, 0.9) * sun * 0.55; // tighter, cooler sun disk
  float sunSoft = pow(max(dot(d, sunDir), 0.0), 5.0);
  sky += vec3(0.85, 0.72, 0.6) * sunSoft * 0.12;
  return max(sky, vec3(0.0));
}

// Shadow PCF helper — 5x5 soft edges, still reads as solid dark under props
float sampleShadowPCF(sampler2D shadowMap, vec3 lightClip, float bias, float invMap) {
  // lightClip: NDC-like xy 0-1, z depth
  if (lightClip.z <= 0.0 || lightClip.z >= 1.0) return 1.0;
  if (lightClip.x < 0.0 || lightClip.x > 1.0 || lightClip.y < 0.0 || lightClip.y > 1.0)
    return 1.0;
  float shadow = 0.0;
  // Slightly larger kernel for softer penumbra (still dark core)
  for (int y = -2; y <= 2; ++y) {
    for (int x = -2; x <= 2; ++x) {
      vec2 uv = lightClip.xy + vec2(float(x), float(y)) * invMap * 1.15;
      float d = texture(shadowMap, uv).r;
      shadow += (lightClip.z - bias > d) ? 0.0 : 1.0;
    }
  }
  return shadow / 25.0;
}

vec3 worldToShadowClip(mat4 lightVP, vec3 worldPos) {
  vec4 c = lightVP * vec4(worldPos, 1.0);
  vec3 ndc = c.xyz / max(c.w, 1e-6);
  // Vulkan NDC y may be flipped depending on proj; ortho lookAt usually needs y flip for sample
  return vec3(ndc.x * 0.5 + 0.5, ndc.y * 0.5 + 0.5, ndc.z);
}

vec3 iblDiffuse(vec3 N) {
  vec3 n = normalize(N);
  vec3 sum = envSkyColor(n) * 0.45;
  vec3 t = normalize(cross(abs(n.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0), n));
  vec3 b = cross(n, t);
  sum += envSkyColor(normalize(n + t * 0.6)) * 0.15;
  sum += envSkyColor(normalize(n - t * 0.6)) * 0.15;
  sum += envSkyColor(normalize(n + b * 0.6)) * 0.15;
  sum += envSkyColor(normalize(n - b * 0.6)) * 0.10;
  sum += vec3(0.06, 0.10, 0.12) * max(-n.y, 0.0) * 0.35;
  return sum;
}

vec3 iblSpecular(vec3 R, float rough) {
  vec3 r = normalize(R);
  float blur = clamp(rough, 0.04, 1.0);
  vec3 sum = envSkyColor(r);
  vec3 t = normalize(cross(abs(r.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0), r));
  vec3 b = cross(r, t);
  float s = blur * 0.55;
  sum += envSkyColor(normalize(r + t * s));
  sum += envSkyColor(normalize(r - t * s));
  sum += envSkyColor(normalize(r + b * s));
  sum += envSkyColor(normalize(r - b * s));
  sum *= 0.2;
  vec3 sunDir = normalize(vec3(0.35, 0.88, 0.35));
  float sharp = pow(max(dot(r, sunDir), 0.0), mix(8.0, 256.0, 1.0 - blur));
  sum += vec3(1.0, 0.95, 0.98) * sharp * (1.0 - blur) * 0.65;
  return sum;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
  return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 evalIBL(vec3 N, vec3 V, vec3 albedo, float rough, float metal) {
  vec3 n = normalize(N);
  vec3 v = normalize(V);
  float NdV = max(dot(n, v), 0.0);
  vec3 F0 = mix(vec3(0.04), albedo, metal);
  vec3 F = fresnelSchlick(NdV, F0);
  vec3 kD = (vec3(1.0) - F) * (1.0 - metal);
  vec3 R = reflect(-v, n);
  vec3 diff = iblDiffuse(n) * albedo * kD;
  vec3 spec = iblSpecular(R, rough) * F * mix(0.3, 0.85, 1.0 - rough);
  // Slightly lower IBL energy so flat ground stays dark moss
  return diff * 0.72 + spec * 0.7;
}

// Separable approximate SSS for fur (wrap + backscatter + scatter tint)
vec3 evalFurSSS(vec3 N, vec3 L, vec3 V, vec3 albedo, float thickness) {
  float wrap = clamp((dot(N, L) + 0.55) / 1.55, 0.0, 1.0);
  wrap = pow(wrap, 1.15);
  // Light wrapping through fiber volume
  float scatter = wrap * wrap;
  vec3 scatterCol = albedo * vec3(1.05, 0.88, 0.82);
  // Backlit translucency
  float back = pow(max(dot(V, -L), 0.0), 2.5) * (1.0 - thickness);
  float trans = pow(max(dot(-N, L), 0.0), 1.5) * 0.35;
  vec3 sss = scatterCol * (scatter * 0.42 + back * 0.28 + trans * 0.22);
  return sss;
}

#endif
