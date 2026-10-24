#version 450 core
#define HIGH_PRECISION
#define OPAQUE

#define texture2D texture
uniform mat4 modelMatrix,modelViewMatrix,projectionMatrix,viewMatrix;uniform mat3 normalMatrix;uniform vec3 cameraPosition;
#define varying out
#define attribute in
in vec3 position,normal;in vec2 uv;

      attribute vec3 aNeon; varying vec3 vN; varying vec3 vV; varying vec3 vNe; varying vec3 vLsW;
      void main() { vec4 w = modelMatrix * vec4(position, 1.0); vLsW = w.xyz; vN = normalize(mat3(modelMatrix) * normal); vV = cameraPosition - w.xyz; vNe = aNeon;
        gl_Position = projectionMatrix * viewMatrix * w; }