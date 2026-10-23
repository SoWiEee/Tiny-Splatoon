#version 450 core
#define HIGH_PRECISION
#define DOUBLE_SIDED

#define texture2D texture
uniform mat4 modelMatrix,modelViewMatrix,projectionMatrix,viewMatrix;uniform mat3 normalMatrix;uniform vec3 cameraPosition;
#define varying out
#define attribute in
in vec3 position,normal;in vec2 uv;

attribute vec4 aPosR;   // xyz, radius (current)
attribute vec4 aNrmT;   // normal, t (0..1)
attribute vec4 aColA;
attribute vec4 aMisc;   // x seed, y style, z thickness
varying vec2 vUv;
varying vec4 vCol;
varying vec4 vMisc;
varying float vT;
void main() {
  vec3 n = normalize(aNrmT.xyz);
  vec3 ref = abs(n.y) < 0.95 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
  vec3 t = normalize(cross(ref, n));
  vec3 b = cross(n, t);
  vec3 wp = aPosR.xyz + (t * position.x + b * position.y) * aPosR.w + n * 0.025;
  vUv = position.xy;
  vCol = aColA; vMisc = aMisc; vT = aNrmT.w;
  gl_Position = projectionMatrix * viewMatrix * vec4(wp, 1.0);
}
