#version 450
// Crystal Nebula gradient sky + horizon glow + soft clouds

layout(location = 0) in vec2 vUV;

layout(set = 0, binding = 0) uniform Frame {
  mat4 viewProj;
  vec4 cameraPos_time;
  vec4 sprintScore_flags;
  vec4 tiling_pad;
  mat4 invViewProj;
} uFrame;

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

float fbm(vec2 p) {
  float v = 0.0;
  float a = 0.5;
  for (int i = 0; i < 5; ++i) {
    v += a * noise2(p);
    p = p * 2.11 + vec2(1.7, 9.2);
    a *= 0.5;
  }
  return v;
}

vec3 skyRay(vec2 uv) {
  // uv is 0..1 from fullscreen triangle; NDC is -1..1
  vec2 ndc = uv * 2.0 - 1.0;
  // Vulkan Y is already flipped in our projection
  vec4 nearP = uFrame.invViewProj * vec4(ndc, 0.0, 1.0);
  vec4 farP  = uFrame.invViewProj * vec4(ndc, 1.0, 1.0);
  nearP.xyz /= nearP.w;
  farP.xyz  /= farP.w;
  return normalize(farP.xyz - nearP.xyz);
}

void main() {
  vec3 rd = skyRay(vUV);
  float t = uFrame.cameraPos_time.w;

  // Vertical gradient: zenith → horizon
  float elev = rd.y; // -1..1
  float h = clamp(elev * 0.5 + 0.5, 0.0, 1.0);

  vec3 zenith  = vec3(0.01, 0.02, 0.06);
  vec3 midSky  = vec3(0.04, 0.10, 0.22);
  vec3 horizon = vec3(0.12, 0.35, 0.48);
  vec3 glow    = vec3(0.35, 0.75, 0.95);

  vec3 col = mix(horizon, midSky, smoothstep(0.0, 0.45, h));
  col = mix(col, zenith, smoothstep(0.35, 0.95, h));

  // Bright horizon band
  float horizBand = exp(-abs(elev) * 8.0) * 0.85;
  col += glow * horizBand * 0.55;
  col = mix(col, horizon * 1.4, horizBand * 0.35);

  // Soft nebula clouds (slow drift)
  vec2 cloudUV = rd.xz / max(rd.y + 0.35, 0.08);
  cloudUV += vec2(t * 0.008, t * 0.005);
  float clouds = fbm(cloudUV * 1.4);
  clouds = smoothstep(0.35, 0.85, clouds);
  float cloudMask = smoothstep(-0.05, 0.55, elev);
  col += vec3(0.08, 0.22, 0.35) * clouds * cloudMask * 0.45;
  col += vec3(0.25, 0.55, 0.7) * pow(clouds, 3.0) * cloudMask * 0.15;

  // Sun glow (matches terrain key light)
  vec3 sunDir = normalize(vec3(0.4, 0.75, 0.25));
  float sun = pow(max(dot(rd, sunDir), 0.0), 32.0);
  float sunHalo = pow(max(dot(rd, sunDir), 0.0), 4.0);
  col += vec3(0.9, 0.95, 1.0) * sun * 0.65;
  col += vec3(0.3, 0.55, 0.75) * sunHalo * 0.25;

  // Subtle star flecks high up
  float stars = step(0.997, hash21(floor(rd.xy * 180.0)));
  stars *= smoothstep(0.25, 0.7, elev);
  col += vec3(0.7, 0.9, 1.0) * stars * 0.55;

  // Mild vignette on sky only
  vec2 q = vUV - 0.5;
  col *= 1.0 - dot(q, q) * 0.25;

  // Tone map lightly so sky matches terrain pass
  col = col / (col + vec3(1.0));
  col = pow(max(col, 0.0), vec3(0.95));

  outColor = vec4(col, 1.0);
}
