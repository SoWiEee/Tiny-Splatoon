#version 450 core
#define HIGH_PRECISION

#define texture2D texture
uniform mat4 modelMatrix,modelViewMatrix,projectionMatrix,viewMatrix;uniform mat3 normalMatrix;uniform vec3 cameraPosition;
#define varying out
#define attribute in
in vec3 position,normal;in vec2 uv;

attribute vec4 aPosSize;
attribute vec4 aColA;
attribute vec4 aMisc;   // x rotation, y seed, z t (0..1), w kind
varying vec2 vUv;
varying vec4 vCol;
varying vec4 vMisc;
void main() {
  vec3 camR = vec3(viewMatrix[0][0], viewMatrix[1][0], viewMatrix[2][0]);
  vec3 camU = vec3(viewMatrix[0][1], viewMatrix[1][1], viewMatrix[2][1]);
  float c = cos(aMisc.x), s = sin(aMisc.x);
  vec2 q = mat2(c, s, -s, c) * position.xy;
  vec3 wp = aPosSize.xyz + (camR * q.x + camU * q.y) * aPosSize.w;
  vUv = position.xy * 2.0;
  vCol = aColA;
  vMisc = aMisc;
  gl_Position = projectionMatrix * viewMatrix * vec4(wp, 1.0);
}
