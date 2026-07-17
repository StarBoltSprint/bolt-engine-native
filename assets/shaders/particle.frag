#version 450
// Soft dust puff

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColorLife;

layout(location = 0) out vec4 outColor;

void main() {
  vec2 p = vUV * 2.0 - 1.0;
  float r = length(p);
  if (r > 1.0) discard;
  float soft = 1.0 - smoothstep(0.1, 1.0, r);
  soft *= soft;
  float life = vColorLife.a;
  float a = soft * life * 0.55;
  vec3 col = vColorLife.rgb;
  // Bright core
  col += vec3(0.4, 0.7, 0.9) * soft * 0.25 * life;
  outColor = vec4(col, a);
}
