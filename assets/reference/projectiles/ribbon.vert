#version 450 core
#define HIGH_PRECISION
#define DOUBLE_SIDED
#define TONE_MAPPING

#define texture2D texture
uniform mat4 modelMatrix,modelViewMatrix,projectionMatrix,viewMatrix;uniform mat3 normalMatrix;uniform vec3 cameraPosition;
#define varying out
#define attribute in
in vec3 position,normal;in vec2 uv;

uniform float uWidth, uLen;
varying vec2 vUv;
void main() {
  vec3 P = (modelMatrix * vec4(0.0, 0.0, position.z, 1.0)).xyz;
  vec3 A = normalize((modelMatrix * vec4(0.0, 0.0, 1.0, 0.0)).xyz);
  vec3 toCam = cameraPosition - P;
  vec3 S = cross(A, toCam);
  float sl = length(S);
  S = sl > 1e-5 ? S / sl : vec3(1.0, 0.0, 0.0);
  float w = max(uWidth, length(toCam) * 0.0016);   // never thinner than ~1 px (no shimmer at range)
  P += S * position.x * w;
  vUv = vec2(position.x, position.z * uLen);
  gl_Position = projectionMatrix * viewMatrix * vec4(P, 1.0);
}