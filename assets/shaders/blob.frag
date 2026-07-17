#version 450
// Radial soft shadow blob (alpha)

layout(location = 0) in vec2 vUV;
layout(location = 1) in float vScale;

layout(location = 0) out vec4 outColor;

void main() {
  vec2 p = vUV * 2.0 - 1.0;
  float r = length(p);
  if (r > 1.0) discard;
  // Soft penumbra
  float a = (1.0 - smoothstep(0.15, 1.0, r));
  a *= a;
  a *= 0.42;
  outColor = vec4(0.0, 0.02, 0.05, a);
}
