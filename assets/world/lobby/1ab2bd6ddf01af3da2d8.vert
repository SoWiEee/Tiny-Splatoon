#version 450 core
#define USE_INSTANCING
#define USE_INSTANCING_COLOR
in mat4 instanceMatrix;
in vec3 instanceColor;
#define HIGH_PRECISION

#define texture2D texture
uniform mat4 modelMatrix,modelViewMatrix,projectionMatrix,viewMatrix;uniform mat3 normalMatrix;uniform vec3 cameraPosition;
#define varying out
#define attribute in
in vec3 position,normal;in vec2 uv;

      varying vec2 vUv; varying vec3 vCol; varying vec3 vLsW;
      void main() {
        vec3 c = (modelMatrix * instanceMatrix * vec4(0.0, 0.0, 0.0, 1.0)).xyz;
        float r = length(instanceMatrix[0].xyz);
        vLsW = c; vUv = position.xy; vCol = instanceColor;
        vec4 mv = viewMatrix * vec4(c, 1.0);
        vec3 toCam = normalize(-mv.xyz);
        mv.xyz += toCam * min(r * 0.8, 0.6);
        mv.xy += position.xy * r;
        gl_Position = projectionMatrix * mv;
      }