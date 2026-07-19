#version 450
// Crystal Nebula Plains — soft Star Moss, energy path, IBL + ORM materials
#include "common_pbr.glsl"
#include "atmos_eval.glsl"

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;
layout(location = 3) in float vHeight;
layout(location = 4) in float vFeature; // +ridge/rock, -crater/basin

layout(set = 0, binding = 0) uniform Frame {
  mat4 viewProj;
  vec4 cameraPos_time;
  vec4 sprintScore_flags;
  vec4 tiling_pad;
  mat4 invViewProj;
  mat4 prevViewProj;
  vec4 taaJitter;
  mat4 lightViewProj[3];
  vec4 shadowParams;
  vec4 cascadeSplits;
  vec4 cascadeOrigin;
} uFrame;

layout(set = 0, binding = 24) uniform sampler2DArray uShadowMap;

layout(set = 0, binding = 2) uniform sampler2D uGroundAlbedo;
layout(set = 0, binding = 3) uniform sampler2D uGroundNormal;
layout(set = 0, binding = 4) uniform sampler2D uGroundRough;
layout(set = 0, binding = 5) uniform sampler2D uRockAlbedo;
layout(set = 0, binding = 6) uniform sampler2D uRockNormal;
layout(set = 0, binding = 7) uniform sampler2D uRockRough;
layout(set = 0, binding = 8) uniform sampler2D uPathAlbedo;
layout(set = 0, binding = 9) uniform sampler2D uPathNormal;
layout(set = 0, binding = 10) uniform sampler2D uPathRough;
layout(set = 0, binding = 19) uniform sampler2D uGroundEmit;
layout(set = 0, binding = 20) uniform sampler2D uRockEmit;
layout(set = 0, binding = 21) uniform sampler2D uPathEmit;

// Crystal / ruin point lights
struct CrystalLight {
  vec4 posRange;       // xyz world, w radius
  vec4 colorIntensity; // rgb, w intensity
};
layout(std430, set = 0, binding = 18) readonly buffer CrystalLights {
  uint lightCount;
  uint _pad0;
  uint _pad1;
  uint _pad2;
  CrystalLight lights[];
} uLights;

layout(location = 0) out vec4 outColor;

vec3 evalCrystalLights(vec3 wp, vec3 N, vec3 albedo, float rough) {
  vec3 sum = vec3(0.0);
  uint n = min(uLights.lightCount, 64u);
  for (uint i = 0u; i < n; ++i) {
    vec3 Lpos = uLights.lights[i].posRange.xyz;
    float range = max(uLights.lights[i].posRange.w, 0.5);
    vec3 toL = Lpos - wp;
    float dist = length(toL);
    float atten = 1.0 - smoothstep(range * 0.55, range, dist);
    if (atten < 0.004) continue;
    vec3 Ldir = toL / max(dist, 1e-3);
    float ndl = max(dot(N, Ldir), 0.0);
    float wrap = clamp((dot(N, Ldir) + 0.35) / 1.35, 0.0, 1.0);
    vec3 col = uLights.lights[i].colorIntensity.rgb;
    float inten = uLights.lights[i].colorIntensity.w;
    // Inverse-square-ish soft falloff inside range
    float fall = atten * atten * (1.0 / (1.0 + dist * dist * 0.012));
    sum += albedo * col * (0.55 * ndl + 0.35 * wrap) * inten * fall;
    // Soft specular glint
    vec3 V = normalize(uFrame.cameraPos_time.xyz - wp);
    vec3 H = normalize(Ldir + V);
    float spec = pow(max(dot(N, H), 0.0), mix(48.0, 12.0, rough)) * (1.0 - rough) * 0.35;
    sum += col * spec * inten * fall;
  }
  return sum;
}

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

// Packed ORM: R=rough G=metal B=height
vec3 triplanarORM(sampler2D tex, vec3 n, vec3 wp, float scale) {
  vec3 b = triplanarWeights(n);
  return texture(tex, wp.zy * scale).rgb * b.x
       + texture(tex, wp.xz * scale).rgb * b.y
       + texture(tex, wp.xy * scale).rgb * b.z;
}

float triplanarRough(sampler2D tex, vec3 n, vec3 wp, float scale) {
  return triplanarORM(tex, n, wp, scale).r;
}

vec3 triplanarEmit(sampler2D tex, vec3 n, vec3 wp, float scale) {
  vec3 b = triplanarWeights(n);
  return texture(tex, wp.zy * scale).rgb * b.x
       + texture(tex, wp.xz * scale).rgb * b.y
       + texture(tex, wp.xy * scale).rgb * b.z;
}

vec3 triplanarNormalSample(sampler2D tex, vec3 geomN, vec3 wp, float scale) {
  vec3 b = triplanarWeights(geomN);
  vec3 nx = texture(tex, wp.zy * scale).xyz * 2.0 - 1.0;
  vec3 ny = texture(tex, wp.xz * scale).xyz * 2.0 - 1.0;
  vec3 nz = texture(tex, wp.xy * scale).xyz * 2.0 - 1.0;
  return normalize(nx * b.x + ny * b.y + nz * b.z);
}

// Dual-scale normals: large form + fine detail (moss grit / crystal facets)
// Stronger blend so the floor reads grit up close, not a smooth purple slab
vec3 triplanarNormal(sampler2D tex, vec3 geomN, vec3 wp, float scale) {
  vec3 nLarge = triplanarNormalSample(tex, geomN, wp, scale);
  vec3 nFine = triplanarNormalSample(tex, geomN, wp, scale * 5.5);
  vec3 nMicro = triplanarNormalSample(tex, geomN, wp, scale * 14.0);
  vec3 mapN = blendDetailNormalTS(nLarge, nFine, 0.72);
  mapN = blendDetailNormalTS(mapN, nMicro, 0.45);
  float str = 1.15;
  vec3 fromMap = normalize(mix(normalize(geomN), normalize(geomN + mapN * str), 0.9));
  // Procedural micro grit for moss (world-space)
  vec3 micro = proceduralMicroNormal(fromMap, wp, scale * 11.0, 0.65);
  return normalize(mix(fromMap, micro, 0.38));
}

float pathMaskAt(vec3 wp, float halfW, float edge, float meanderAmp) {
  float meander = sin(wp.z * 0.045 + 0.7) * meanderAmp
                + sin(wp.z * 0.12 + 1.3) * (meanderAmp * 0.35);
  float d = abs(wp.x - meander);
  float w = halfW + sin(wp.z * 0.08) * 0.6;
  return 1.0 - smoothstep(w, w + edge, d);
}

vec3 atmosphereColor(vec3 viewDir, float dist, float score) {
  float elev = viewDir.y;
  float h = clamp(elev * 0.5 + 0.5, 0.0, 1.0);
  vec3 zenith  = vec3(0.08, 0.04, 0.16);
  vec3 midSky  = vec3(0.16, 0.1, 0.28);
  vec3 horizon = vec3(0.32, 0.18, 0.42);
  vec3 sky = mix(horizon, midSky, smoothstep(0.0, 0.4, h));
  sky = mix(sky, zenith, smoothstep(0.35, 0.95, h));
  float horizBand = exp(-abs(elev) * 5.5);
  sky += vec3(0.55, 0.35, 0.85) * horizBand * 0.55;
  sky += vec3(0.25, 0.65, 0.85) * horizBand * 0.28;
  float dens = 1.0 - exp(-dist * 0.0015);
  vec3 haze = mix(vec3(0.18, 0.22, 0.38), vec3(0.28, 0.16, 0.4), 0.45 + score * 0.15);
  return mix(sky, haze, dens * 0.55);
}

void main() {
  vec3 geomN = normalize(vNormal);
  float tiling = max(uFrame.tiling_pad.x, 0.008);
  float pathHalf = max(uFrame.tiling_pad.y, 2.0);
  float pathEdge = max(uFrame.tiling_pad.z, 1.0);
  float meanderAmp = max(uFrame.tiling_pad.w, 0.5);
  float score = clamp(uFrame.sprintScore_flags.x, 0.0, 1.4);
  float t = uFrame.cameraPos_time.w;
  int flags = int(uFrame.sprintScore_flags.y + 0.5);
  bool hasGround = (flags & 1) != 0;
  bool hasRock = (flags & 2) != 0;
  bool hasPath = (flags & 4) != 0;

  // Path ribbon mesh (main/branch/hidden/bridge) — pure energy surface
  // Linear HDR out (tonemap only in post — avoid double ACES wash)
  if (vFeature > 50.0) {
    float glow = clamp(vFeature - 100.0, 0.25, 2.8);
    // Hidden trails brighten when camera is near
    float distCam = length(uFrame.cameraPos_time.xz - vWorldPos.xz);
    float nearBoost = 1.0 + (1.0 - smoothstep(8.0, 28.0, distCam)) * 1.4;
    glow *= nearBoost;
    vec3 albedo = mix(vec3(0.12, 0.45, 0.7), vec3(0.35, 0.8, 1.0), clamp(glow * 0.45, 0.0, 1.0));
    float pulse = 0.55 + 0.45 * sin(t * 3.2 + vWorldPos.x * 0.2 + vWorldPos.z * 0.15);
    vec3 emit = vec3(0.25, 0.75, 1.0) * glow * pulse * (0.45 + score * 0.4);
    vec3 N = normalize(geomN + vec3(0.0, 0.2, 0.0));
    vec3 V = normalize(uFrame.cameraPos_time.xyz - vWorldPos);
    float fres = pow(1.0 - max(dot(N, V), 0.0), 2.5);
    vec3 col = albedo * 0.28 + emit + vec3(0.4, 0.8, 1.1) * fres * glow * 0.35;
    outColor = vec4(col, 1.0);
    return;
  }

  // Soft multi-scale noise for Star Moss fields
  float n1 = fbm2(vWorldPos.xz * 0.035);
  float n2 = fbm2(vWorldPos.xz * 0.09 + 4.2);
  float n3 = fbm2(vWorldPos.xz * 0.22 + t * 0.02);
  float n4 = fbm2(vWorldPos.xz * 0.55 + 11.0);

  // Design: high-contrast Star Moss floor — dark valleys, lit ridges, grit normals
  vec3 mossDeep = vec3(0.01, 0.035, 0.055);   // near-black teal pits
  vec3 mossMid  = vec3(0.025, 0.09, 0.12);
  vec3 mossHi   = vec3(0.06, 0.16, 0.2);      // clearer highlight moss
  vec3 mossLav  = vec3(0.07, 0.04, 0.11);
  vec3 mossGlow = vec3(0.06, 0.32, 0.38);

  // Wider contrast curve between deep / mid / hi
  vec3 albedo = mix(mossDeep, mossMid, smoothstep(0.15, 0.8, n1));
  albedo = mix(albedo, mossHi, smoothstep(0.5, 0.92, n2) * 0.55);
  albedo = mix(albedo, mossLav, smoothstep(0.35, 0.88, n3) * 0.28);
  // Micro sparkle clusters (resonance dust) — sparse
  float spark = smoothstep(0.78, 0.96, n4);
  albedo += mossGlow * spark * 0.18;

  // Macro contrast from multi-scale noise (breaks flat purple slab)
  float macro = n1 * 0.55 + n2 * 0.3 + n3 * 0.15;
  albedo *= mix(0.55, 1.2, smoothstep(0.2, 0.85, macro));

  float rough = 0.82 - spark * 0.14;
  float metal = 0.0;
  float heightS = 0.5;
  vec3 N = geomN;
  vec3 mapEmit = vec3(0.0);

  // Slightly denser triplanar so grit is visible (still world-scale)
  float groundTile = tiling * 1.35;

  if (hasGround) {
    vec3 orm0 = triplanarORM(uGroundRough, geomN, vWorldPos, groundTile);
    heightS = orm0.b;
    // Height-aware sample offset (cheap parallax) — stronger
    vec3 wpH = vWorldPos + geomN * ((heightS - 0.5) * 0.12);
    vec3 a1 = triplanarAlbedo(uGroundAlbedo, geomN, wpH, groundTile * 0.85);
    // Crush bright maps into dark moss, keep micro variation
    float lum = dot(a1, vec3(0.299, 0.587, 0.114));
    a1 *= mix(0.5, 0.14, smoothstep(0.2, 0.88, lum));
    a1 = mix(a1, a1 * a1, 0.5);
    a1 *= vec3(0.45, 0.8, 0.92);
    // Height contrast: low height darker, high height slightly teal-lit
    a1 *= mix(0.65, 1.15, heightS);
    albedo = mix(albedo, albedo * 0.55 + a1 * 0.45, 0.55);
    albedo *= 0.78;
    N = triplanarNormal(uGroundNormal, geomN, wpH, groundTile * 0.95);
    // Extra procedural grit so flat normals never win
    vec3 grit = proceduralMicroNormal(N, vWorldPos, groundTile * 18.0, 0.85);
    N = normalize(mix(N, grit, 0.4));
    vec3 orm = triplanarORM(uGroundRough, geomN, wpH, groundTile);
    float rMap = remapRoughness(orm.r, 0.52, 0.96, 1.45);
    rough = mix(rough, rMap, 0.65);
    metal = mix(metal, orm.g * 0.35, 0.12);
    heightS = orm.b;
    rough = clamp(rough + (0.55 - heightS) * 0.18, 0.45, 0.97);
    mapEmit += triplanarEmit(uGroundEmit, geomN, wpH, groundTile * 0.85) * 0.14;
  } else {
    // No maps: still give readable normals via procedural grit
    N = proceduralMicroNormal(geomN, vWorldPos, 0.12, 0.9);
    N = normalize(mix(geomN, N, 0.65));
  }

  // Height-based contrast: low basins darker, high ground cooler mid-tone
  float hT = smoothstep(-1.0, 7.0, vHeight);
  albedo = mix(albedo * vec3(0.72, 0.78, 0.85), albedo * vec3(0.95, 1.02, 1.08), hT * 0.55);
  // AO-ish: flatten-facing tops slightly brighter, keep pits dark
  float faceUp = clamp(geomN.y * 0.7 + 0.3, 0.0, 1.0);
  albedo *= mix(0.62, 1.08, faceUp);
  // Hard ceiling so floor never approaches white / path
  albedo = min(albedo, vec3(0.16, 0.24, 0.28));
  albedo = max(albedo, vec3(0.008, 0.02, 0.03));

  // —— Landform color coding (make features unmistakable) ——
  float craterW = smoothstep(-0.4, -1.8, vFeature);
  float ridgeW  = smoothstep(0.85, 2.6, vFeature);
  float rockFW  = smoothstep(0.9, 3.2, vFeature) * (1.0 - ridgeW * 0.3);
  float valleyW = (1.0 - craterW) * smoothstep(0.35, -0.15, vFeature) *
                  smoothstep(0.55, 0.15, ridgeW + rockFW);

  vec3 valleyMoss = vec3(0.015, 0.1, 0.14);
  vec3 craterMoss = vec3(0.025, 0.16, 0.2);
  albedo = mix(albedo, valleyMoss, valleyW * 0.6);
  albedo = mix(albedo, craterMoss, craterW * 0.65);
  mapEmit += vec3(0.06, 0.28, 0.35) * craterW * (0.2 + score * 0.15);
  mapEmit += vec3(0.04, 0.18, 0.24) * valleyW * 0.1;
  rough = mix(rough, 0.85, valleyW * 0.45);
  rough = mix(rough, 0.55, craterW * 0.35);

  // Ridge: exposed crystal veins + rock
  albedo = mix(albedo, vec3(0.18, 0.14, 0.28), ridgeW * 0.55);
  albedo = mix(albedo, vec3(0.28, 0.4, 0.58), ridgeW * 0.22);
  mapEmit += vec3(0.28, 0.38, 0.75) * ridgeW * (0.25 + score * 0.22);
  rough = mix(rough, 0.26, ridgeW * 0.65);
  metal = mix(metal, 0.22, ridgeW * 0.4);

  float slope = 1.0 - clamp(geomN.y, 0.0, 1.0);
  // Rock shelves earlier + wider — gentle slopes already show rock shelves
  float rockW = smoothstep(0.04, 0.28, slope);
  rockW = max(rockW, rockFW * 0.95);
  rockW = max(rockW, ridgeW * 0.7);
  // Noise break-up so rock isn't a perfect slope band
  float rockNoise = fbm2(vWorldPos.xz * 0.08 + 3.1);
  rockW *= mix(0.55, 1.15, rockNoise);
  rockW = clamp(rockW, 0.0, 1.0);
  if (hasRock && rockW > 0.001) {
    float rockTile = tiling * 1.1;
    vec3 ra = triplanarAlbedo(uRockAlbedo, geomN, vWorldPos, rockTile * 0.85);
    float rLum = dot(ra, vec3(0.299, 0.587, 0.114));
    ra *= mix(0.65, 0.28, smoothstep(0.25, 0.85, rLum));
    ra = mix(ra, vec3(0.1, 0.09, 0.14), 0.5);
    // Crystal protrusions on rock (lavender-cyan) — controlled
    ra = mix(ra, vec3(0.24, 0.28, 0.45), rockFW * 0.45 + ridgeW * 0.25);
    // Contrast streaks on rock faces
    float rN = fbm2(vWorldPos.xy * 0.35 + vWorldPos.z * 0.2);
    ra *= mix(0.7, 1.25, rN);
    vec3 rn = triplanarNormal(uRockNormal, geomN, vWorldPos, rockTile * 0.9);
    vec3 rGrit = proceduralMicroNormal(rn, vWorldPos, rockTile * 10.0, 0.75);
    rn = normalize(mix(rn, rGrit, 0.35));
    vec3 ormR = triplanarORM(uRockRough, geomN, vWorldPos, rockTile * 0.85);
    albedo = mix(albedo, ra, rockW * 0.95);
    N = normalize(mix(N, rn, rockW * 0.92));
    rough = mix(rough, remapRoughness(ormR.r, 0.32, 0.9, 1.5), rockW);
    metal = mix(metal, max(ormR.g, 0.08), rockW * 0.5);
    mapEmit += triplanarEmit(uRockEmit, geomN, vWorldPos, rockTile * 0.85) * rockW * 0.22;
    mapEmit += vec3(0.18, 0.32, 0.6) * rockFW * 0.22;
  } else if (rockW > 0.05) {
    // No rock maps: still darken / roughen slopes so shelves read
    albedo = mix(albedo, vec3(0.08, 0.07, 0.1), rockW * 0.75);
    rough = mix(rough, 0.7, rockW * 0.5);
    N = proceduralMicroNormal(N, vWorldPos, 0.2, 0.7);
  }

  // Energy path — luminous highway (the bright run line on dark floor)
  float pMask = pathMaskAt(vWorldPos, pathHalf, pathEdge, meanderAmp);
  pMask *= smoothstep(0.55, 0.22, slope);
  // Soft moss glow near path (trail on Star Moss)
  float pathHalo = pathMaskAt(vWorldPos, pathHalf * 1.65, pathEdge * 1.8, meanderAmp) *
                   smoothstep(0.6, 0.2, slope);
  float wet = 0.0;
  if (pathHalo > 0.001 && pMask < 0.5) {
    albedo = mix(albedo, albedo * vec3(0.75, 1.1, 1.2), pathHalo * 0.28);
    mapEmit += vec3(0.1, 0.4, 0.55) * pathHalo * pathHalo * (0.15 + score * 0.12);
  }
  if (pMask > 0.001) {
    // Path stays the brightest band (run line) — controlled, not white
    vec3 pathCol = vec3(0.28, 0.7, 0.9);
    vec3 pathCore = vec3(0.55, 0.9, 1.05);
    vec3 pathLav = vec3(0.5, 0.4, 0.85);
    if (hasPath) {
      vec3 pa = triplanarAlbedo(uPathAlbedo, geomN, vWorldPos, tiling * 0.9);
      float pLum = dot(pa, vec3(0.299, 0.587, 0.114));
      pa *= mix(0.75, 0.4, smoothstep(0.3, 0.85, pLum));
      pa = mix(pa, pathCol, 0.55);
      albedo = mix(albedo, pa, pMask * 0.9);
      N = normalize(mix(N, triplanarNormal(uPathNormal, geomN, vWorldPos, tiling * 0.9), pMask * 0.55));
      vec3 ormP = triplanarORM(uPathRough, geomN, vWorldPos, tiling * 0.9);
      float rPath = remapRoughness(ormP.r, 0.1, 0.45, 1.45);
      rough = mix(rough, mix(rPath, 0.16, 0.5), pMask * 0.9);
      metal = mix(metal, ormP.g * 0.4, pMask * 0.35);
      mapEmit += triplanarEmit(uPathEmit, geomN, vWorldPos, tiling * 0.9) * pMask * 0.55;
    } else {
      albedo = mix(albedo, pathCol, pMask * 0.92);
      rough = mix(rough, 0.16, pMask);
    }
    wet = pMask;
    float pulse = 0.5 + 0.5 * sin(vWorldPos.z * 0.15 - t * 2.2 + n2 * 3.0);
    albedo += mix(pathCore, pathLav, pulse) * pMask * pMask * (0.22 + score * 0.2);
    mapEmit += mix(pathCore, pathLav, pulse) * pMask * pMask * (0.32 + score * 0.3);
  }

  // —— Lighting: key/fill + IBL + crystal lights (lower energy, less wrap) ——
  vec3 L1 = normalize(vec3(0.35, 0.88, 0.35));
  vec3 L2 = normalize(vec3(-0.55, 0.4, -0.3));
  vec3 L3 = normalize(vec3(0.15, 0.25, 0.85));

  float ndl1 = max(dot(N, L1), 0.0);
  // Tight wrap — deep shade on grazing faces (was washing floor white)
  float wrap = clamp((dot(N, L1) + 0.12) / 1.12, 0.0, 1.0);
  wrap *= wrap; // quadratic falloff
  float ndl2 = max(dot(N, L2), 0.0) * 0.28;
  float ndl3 = max(dot(N, L3), 0.0) * 0.14;

  float ao = mix(0.42, 1.0, clamp(geomN.y * 0.85 + 0.15, 0.0, 1.0));
  ao *= mix(1.0, 0.72, rockW * 0.55);

  vec3 V = normalize(uFrame.cameraPos_time.xyz - vWorldPos);
  vec3 H = normalize(L1 + V);
  float fres = pow(1.0 - max(dot(N, V), 0.0), 3.0);
  float a = max(rough * rough, 0.02);
  float NdH = max(dot(N, H), 0.0);
  float a2 = a * a;
  float dggx = a2 / max(3.14159 * pow(NdH * NdH * (a2 - 1.0) + 1.0, 2.0), 1e-4);
  vec3 F0 = mix(vec3(0.04), albedo, metal);
  vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
  float ggxSpec = dggx * mix(0.35, 0.95, wet) * (0.2 + 0.65 * fres);
  float wetSpec = pow(max(dot(N, H), 0.0), mix(40.0, 160.0, wet)) * wet * 0.55;
  vec3 rim = vec3(0.35, 0.45, 0.85) * fres * (0.18 + wet * 0.28);

  float distPre = length(uFrame.cameraPos_time.xyz - vWorldPos);
  float aerial = smoothstep(40.0, 200.0, distPre);

  float sh = 1.0;
  if (uFrame.shadowParams.z > 0.5) {
    sh = sampleShadowCSM(uShadowMap, uFrame.lightViewProj[0], uFrame.lightViewProj[1],
                         uFrame.lightViewProj[2], uFrame.cascadeOrigin.xyz,
                         uFrame.cascadeSplits.xyz, vWorldPos, N, uFrame.shadowParams.x,
                         uFrame.shadowParams.w);
    sh = mix(1.0, sh, uFrame.shadowParams.y);
    // Punch: shadowed regions go darker (depth under trees / rocks)
    sh = mix(0.12, 1.0, sh);
  }

  vec3 sunCol = vec3(0.95, 0.88, 0.92);
  vec3 fillCol = vec3(0.35, 0.28, 0.62);
  // Linear HDR — tonemap only in post; key stronger, fill weaker → contrast
  vec3 col = evalIBL(N, V, albedo, rough, metal) * ao * 0.42;
  col += albedo * sunCol * (0.42 * ndl1 + 0.1 * wrap) * (1.0 - metal * 0.5) * sh;
  col += albedo * fillCol * ndl2 * mix(0.35, 0.75, sh);
  col += albedo * vec3(0.25, 0.48, 0.7) * ndl3;
  col += sunCol * F * ggxSpec * sh * 0.75 + rim;
  col += vec3(0.5, 0.8, 0.95) * wetSpec;
  col += evalCrystalLights(vWorldPos, N, albedo, rough) * ao * 0.7;
  col = mix(col, col * vec3(0.8, 0.78, 0.95), aerial * 0.4);

  float mossPulse = 0.5 + 0.5 * sin(t * 1.1 + n1 * 6.0 + vWorldPos.x * 0.08);
  col += mossGlow * spark * (0.1 + score * 0.08) * mossPulse;
  col += vec3(0.05, 0.12, 0.18) * 0.06 * n2;
  col += vec3(0.25, 0.6, 0.8) * wet * wet * (0.12 + score * 0.1);
  col += vec3(0.4, 0.25, 0.7) * wet * 0.05 * mossPulse;
  // Texture emissive maps (path/ground glow)
  col += mapEmit * (0.35 + score * 0.25) * (0.65 + 0.25 * mossPulse);
  // Height micro-shadow
  col *= mix(0.82, 1.0, heightS);

  // Volumetric fog (forward path) — same model as deferred
  {
    vec3 cam = uFrame.cameraPos_time.xyz;
    vec3 sunDir = normalize(vec3(0.35, 0.88, 0.35));
    float dist = length(cam - vWorldPos);
    vec3 fogSky = atmosphereColor(normalize(vWorldPos - cam), dist, score) * 0.55;
    col = applyVolumetricFogFast(col, cam, vWorldPos, sunDir, fogSky, t, score);
  }

  // Tiny path sparkle lift only (no full-frame bloom)
  float bloom = max(dot(col, vec3(0.3, 0.5, 0.2)) - 0.75, 0.0);
  col += col * bloom * 0.12 * wet;

  outColor = vec4(col, 1.0);
}
