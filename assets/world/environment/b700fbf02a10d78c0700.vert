#version 450 core
#define HIGH_PRECISION
#define DOUBLE_SIDED

#define texture2D texture
uniform mat4 modelMatrix,modelViewMatrix,projectionMatrix,viewMatrix;uniform mat3 normalMatrix;uniform vec3 cameraPosition;
#define varying out
#define attribute in
in vec3 position,normal;in vec2 uv;
varying float vA; varying vec3 vN; varying vec3 vV;
        void main(){ vA = clamp(position.x / 150.0, 0.0, 1.0); vec4 wp = modelMatrix * vec4(position, 1.0);
          vN = normalize(mat3(modelMatrix) * normal); vV = normalize(cameraPosition - wp.xyz); gl_Position = projectionMatrix * viewMatrix * wp; }