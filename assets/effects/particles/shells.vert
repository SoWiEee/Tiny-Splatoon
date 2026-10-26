#version 450 core
#define HIGH_PRECISION
#define DOUBLE_SIDED
#define OPAQUE

#define texture2D texture
uniform mat4 modelMatrix,modelViewMatrix,projectionMatrix,viewMatrix;uniform mat3 normalMatrix;uniform vec3 cameraPosition;
#define varying out
#define attribute in
in vec3 position,normal;in vec2 uv;


float fxHash(vec2 p){ vec3 p3 = fract(vec3(p.xyx) * 0.1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z); }
float fxNoise(vec2 p){ vec2 i = floor(p), f = fract(p); vec2 u = f*f*(3.0-2.0*f);
  return mix(mix(fxHash(i), fxHash(i+vec2(1.0,0.0)), u.x), mix(fxHash(i+vec2(0.0,1.0)), fxHash(i+vec2(1.0,1.0)), u.x), u.y); }

float fxH3v(vec3 p) { return fxHash(p.xy + p.z * vec2(37.13, 17.31)); }
float fxN3v(vec3 p) {
  vec3 i = floor(p), f = fract(p); vec3 u = f * f * (3.0 - 2.0 * f);
  float a = mix(fxH3v(i), fxH3v(i + vec3(1.0, 0.0, 0.0)), u.x), b = mix(fxH3v(i + vec3(0.0, 1.0, 0.0)), fxH3v(i + vec3(1.0, 1.0, 0.0)), u.x);
  float c = mix(fxH3v(i + vec3(0.0, 0.0, 1.0)), fxH3v(i + vec3(1.0, 0.0, 1.0)), u.x), d = mix(fxH3v(i + vec3(0.0, 1.0, 1.0)), fxH3v(i + vec3(1.0, 1.0, 1.0)), u.x);
  return mix(mix(a, b, u.y), mix(c, d, u.y), u.z);
}
attribute vec4 aPosR;
attribute vec4 aColA;
attribute vec4 aMisc;   // x t, y seed, z -, w wobble amount
attribute vec4 aAxis;   // crown axis (the surface normal), crown amount (0 = free burst, 1 = crown splash)
uniform float uTime;
varying vec3 vN;
varying vec3 vW;
varying vec4 vCol;
varying vec4 vMisc;
varying vec4 vAxis;
varying float vNear;
void main() {
  vec3 n = normalize(position);
  vNear = 1.0 - smoothstep(aPosR.w * 1.3, aPosR.w * 3.2 + 0.6, distance(cameraPosition, aPosR.xyz));
  float s = aMisc.y * 17.0;
  float w = sin(n.x * 5.1 + s + uTime * 7.0) * sin(n.y * 4.3 + s * 1.7 + uTime * 5.3) * sin(n.z * 4.7 + s * 0.6 - uTime * 6.1);
  w += 0.5 * sin(n.x * 11.0 - n.z * 9.0 + s * 2.1 + uTime * 9.0);
  // jets: the sheet is pushed out into a few blunt spikes where the ink was thrown hardest
  float jet = max(fxN3v(n * 3.3 + s) - 0.52, 0.0) * 2.1;
  float rad = aPosR.w * (1.0 + w * aMisc.w + jet * jet * 0.55 * (1.0 - aAxis.w));
  vec3 ax = aAxis.xyz; float cr = aAxis.w;
  float up = dot(n, ax);
  // crown splash: a squat bowl whose wall flares out toward the (torn-open) rim
  vec3 off = mix(n, (n - ax * up) * (1.0 + 0.4 * smoothstep(0.0, 0.9, up)) + ax * up * 0.62, cr) * rad;
  vec3 wp = aPosR.xyz + off;
  vN = n;
  vW = wp;
  vCol = aColA; vMisc = aMisc; vAxis = aAxis;
  gl_Position = projectionMatrix * viewMatrix * vec4(wp, 1.0);
}
