#version 450 core
#define HIGH_PRECISION
#define DOUBLE_SIDED

#define texture2D texture
uniform mat4 modelMatrix,modelViewMatrix,projectionMatrix,viewMatrix;uniform mat3 normalMatrix;uniform vec3 cameraPosition;
#define varying out
#define attribute in
in vec3 position,normal;in vec2 uv;
varying vec2 vUv; varying vec3 vLsW; void main() { vUv = uv; vec4 w = modelMatrix * vec4(position, 1.0); vLsW = w.xyz; gl_Position = projectionMatrix * viewMatrix * w; }