#version 450 core
layout(location=0) in vec3 position; uniform mat4 projectionMatrix; uniform mat4 viewMatrix; out vec3 vDir;


void main() {
  vDir = position;
  vec4 p = projectionMatrix * vec4(mat3(viewMatrix) * position, 1.0);
  gl_Position = vec4(p.xy, p.w * 0.99999, p.w);
}
