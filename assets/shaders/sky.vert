#version 450
// Fullscreen triangle — no vertex buffer

layout(location = 0) out vec2 vUV;

void main() {
  // Three clip-space verts covering the screen
  vec2 pos = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
  vUV = pos;
  gl_Position = vec4(pos * 2.0 - 1.0, 1.0, 1.0); // z=1 far plane
}
