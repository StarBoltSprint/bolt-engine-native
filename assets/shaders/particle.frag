#version 450
// Soft dust / pawprint ring / crystal spark / aura spark shapes

layout(location = 0) in vec2 vUV;
layout(location = 1) in vec4 vColorLife;
layout(location = 2) flat in float vKind;

layout(location = 0) out vec4 outColor;

void main() {
  vec2 p = vUV * 2.0 - 1.0;
  float r = length(p);
  float life = vColorLife.a;
  vec3 col = vColorLife.rgb;
  float a = 0.0;

  if (vKind < 0.5) {
    // 0 soft dust puff
    if (r > 1.0) discard;
    float soft = 1.0 - smoothstep(0.1, 1.0, r);
    soft *= soft;
    a = soft * life * 0.55;
    col += vec3(0.4, 0.7, 0.9) * soft * 0.25 * life;
  } else if (vKind < 1.5) {
    // 1 Star Moss pawprint — soft ring / pad shape
    if (r > 1.0) discard;
    float ring = smoothstep(0.15, 0.35, r) * (1.0 - smoothstep(0.55, 0.95, r));
    float pad = (1.0 - smoothstep(0.0, 0.4, r)) * 0.55;
    float m = max(ring, pad);
    a = m * life * 0.75;
    col = mix(col, vec3(0.35, 0.95, 1.0), 0.35);
    col += vec3(0.5, 0.3, 0.95) * ring * 0.4 * life;
  } else if (vKind < 2.5) {
    // 2 crystal dust — sharp spark core + cross glint
    if (r > 1.0) discard;
    float core = 1.0 - smoothstep(0.0, 0.35, r);
    float arm = max(1.0 - abs(p.x) * 4.0, 0.0) * max(1.0 - abs(p.y) * 1.2, 0.0);
    arm = max(arm, max(1.0 - abs(p.y) * 4.0, 0.0) * max(1.0 - abs(p.x) * 1.2, 0.0));
    float m = max(core, arm * 0.65);
    a = m * life * 0.85;
    col += vec3(0.7, 0.5, 1.0) * core * 0.5;
  } else {
    // 3 aura spark — soft diamond glow
    if (r > 1.0) discard;
    float d = abs(p.x) + abs(p.y);
    float soft = 1.0 - smoothstep(0.2, 1.1, d);
    soft *= soft;
    a = soft * life * 0.7;
    col = mix(col, vec3(0.75, 0.45, 1.0), 0.4);
    col += vec3(0.3, 0.9, 1.0) * soft * 0.35 * life;
  }

  outColor = vec4(col, a);
}
