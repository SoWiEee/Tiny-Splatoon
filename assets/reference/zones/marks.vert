#version 450 core
#define HIGH_PRECISION
#define DOUBLE_SIDED

#define texture2D texture
uniform mat4 modelMatrix,modelViewMatrix,projectionMatrix,viewMatrix;uniform mat3 normalMatrix;uniform vec3 cameraPosition;
#define varying out
#define attribute in
in vec3 position,normal;in vec2 uv;

attribute vec4 aInfo;           // zone index, kind (0 floor, 1 curtain), d (m from the outline | curtain height 0..1), s (m along the outline)
uniform float uFarK;
varying vec4 vInfo;
varying vec3 vW;
void main() {
  vInfo = aInfo;
  vec4 w = modelMatrix * vec4(position, 1.0);
  // far away the light curtain rises taller (its top edge), so a zone reads across the stage
  if (aInfo.y > 0.5 && aInfo.z > 0.5) w.y += 1.7 * 0.85 * smoothstep(11.0, 44.0, length(w.xyz - cameraPosition)) * uFarK;
  vW = w.xyz;
  gl_Position = projectionMatrix * viewMatrix * w;
}