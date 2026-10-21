#version 450 core
#define HIGH_PRECISION
#define OPAQUE

#define texture2D texture
uniform mat4 modelMatrix,modelViewMatrix,projectionMatrix,viewMatrix;uniform mat3 normalMatrix;uniform vec3 cameraPosition;
#define varying out
#define attribute in
in vec3 position,normal;in vec2 uv;

  attribute vec2 aInfo; uniform float uTime; uniform float uLife; varying float vSide; varying float vAge;
  void main(){ vSide = aInfo.x; vAge = clamp((uTime - aInfo.y) / uLife, 0.0, 1.0); gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0); }