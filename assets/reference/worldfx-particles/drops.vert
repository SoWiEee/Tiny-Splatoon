#version 450 core
#define HIGH_PRECISION
#define OPAQUE

#define texture2D texture
uniform mat4 modelMatrix,modelViewMatrix,projectionMatrix,viewMatrix;uniform mat3 normalMatrix;uniform vec3 cameraPosition;
#define varying out
#define attribute in
in vec3 position,normal;in vec2 uv;

attribute vec4 aPosR;   // xyz, radius
attribute vec4 aVelS;   // velocity, stretch
attribute vec4 aColA;   // rgb, gloss
varying vec2 vUv;
varying vec3 vCol;
varying vec2 vAx;
varying float vGloss;
varying float vTail;
void main() {
  vec4 c = viewMatrix * vec4(aPosR.xyz, 1.0);
  vec3 vv = mat3(viewMatrix) * aVelS.xyz;
  float s3 = length(vv), s2 = length(vv.xy);
  vec2 ax = s2 > 1e-4 ? vv.xy / s2 : vec2(0.0, 1.0);
  float st = 1.0 + (aVelS.w - 1.0) * (s3 > 1e-4 ? s2 / s3 : 0.0);
  vec2 bx = vec2(ax.y, -ax.x);   // right-handed with ax so the quad stays front-facing
  float r = aPosR.w;
  c.xy += ax * position.y * r * st + bx * position.x * r / sqrt(st);
  c.xyz += normalize(-c.xyz) * r * 0.6;
  vUv = position.xy;
  vCol = aColA.rgb;
  vGloss = aColA.a;
  vAx = ax;
  vTail = clamp((st - 1.15) / 1.1, 0.0, 1.0);
  gl_Position = projectionMatrix * c;
}
