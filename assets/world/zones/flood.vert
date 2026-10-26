#version 450 core
#define HIGH_PRECISION
#define OPAQUE

#define texture2D texture
uniform mat4 modelMatrix,modelViewMatrix,projectionMatrix,viewMatrix;uniform mat3 normalMatrix;uniform vec3 cameraPosition;
#define varying out
#define attribute in
in vec3 position,normal;in vec2 uv;

attribute vec2 aPos;
attribute vec3 aW;
varying vec3 vW;
void main() { vW = aW; gl_Position = vec4(aPos, 0.0, 1.0); }