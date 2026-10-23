#version 450 core
#define HIGH_PRECISION
#define DOUBLE_SIDED

#define texture2D texture
uniform mat4 modelMatrix,modelViewMatrix,projectionMatrix,viewMatrix;uniform mat3 normalMatrix;uniform vec3 cameraPosition;
#define varying out
#define attribute in
in vec3 position,normal;in vec2 uv;

attribute vec4 aPosR;   // base pos, radius
attribute vec4 aColA;
attribute vec4 aMisc;   // x t, y height, z seed, w mode (0 geyser, 1 pillar)
varying vec3 vN;
varying vec3 vW;
varying float vH;
varying vec4 vCol;
varying vec4 vMisc;
void main() {
  float h = position.y;             // 0..1
  float bulge = aMisc.w > 0.5 ? 1.0 : 1.0 + 0.25 * sin(h * 9.0 + aMisc.z * 10.0 - aMisc.x * 30.0) * (1.0 - h);
  float taper = aMisc.w > 0.5 ? mix(1.0, 0.8, h) : mix(1.0, 0.55, h);
  vec3 wp = aPosR.xyz + vec3(position.x * aPosR.w * bulge * taper, h * aMisc.y, position.z * aPosR.w * bulge * taper);
  vN = normalize(vec3(position.x, 0.0, position.z));
  vW = wp; vH = h; vCol = aColA; vMisc = aMisc;
  gl_Position = projectionMatrix * viewMatrix * vec4(wp, 1.0);
}
