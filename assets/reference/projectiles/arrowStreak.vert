#version 450 core
#define HIGH_PRECISION

#define texture2D texture
uniform mat4 modelMatrix,modelViewMatrix,projectionMatrix,viewMatrix;uniform mat3 normalMatrix;uniform vec3 cameraPosition;
#define varying out
#define attribute in
in vec3 position,normal;in vec2 uv;
uniform vec3 uTint;

      varying float vF; varying vec3 vC;
      void main() {
        vF = clamp(-position.z, 0.0, 1.0);
        vC = uTint;
        #ifdef USE_INSTANCING_COLOR
          vC = instanceColor;
        #endif
        vec4 p = vec4(position, 1.0);
        #ifdef USE_INSTANCING
          p = instanceMatrix * p;
        #endif
        gl_Position = projectionMatrix * modelViewMatrix * p;
      }