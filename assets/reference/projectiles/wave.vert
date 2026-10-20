#version 450 core
#define HIGH_PRECISION
#define DOUBLE_SIDED

#define texture2D texture
uniform mat4 modelMatrix,modelViewMatrix,projectionMatrix,viewMatrix;uniform mat3 normalMatrix;uniform vec3 cameraPosition;
#define varying out
#define attribute in
in vec3 position,normal;in vec2 uv;
attribute float aK; varying vec2 vUv; varying float vK;
void main() { vUv = uv; vK = aK; gl_Position = projectionMatrix * modelViewMatrix * vec4(position, 1.0); }