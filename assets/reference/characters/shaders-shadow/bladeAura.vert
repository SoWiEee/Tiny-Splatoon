#version 450 core
#define HIGH_PRECISION
#define USE_SHADOWMAP
#define SHADOWMAP_TYPE_PCF

#define texture2D texture
uniform mat4 modelMatrix,modelViewMatrix,projectionMatrix,viewMatrix;uniform mat3 normalMatrix;uniform vec3 cameraPosition;
#define varying out
#define attribute in
in vec3 position,normal;in vec2 uv;

      varying float vZ; varying vec3 vN; varying vec3 vV;
      void main() {
        vZ = position.z;
        vec4 mv = modelViewMatrix * vec4(position + normal * 0.0016, 1.0);
        vN = normalize(normalMatrix * normal); vV = -mv.xyz;
        gl_Position = projectionMatrix * mv;
      }