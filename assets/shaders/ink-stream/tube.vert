#version 450 core
layout(location=0) in vec3 aPosition;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec3 aColor;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat3 uNormalMatrix;
out vec3 vNormal;
out vec3 vViewPosition;
out vec3 vColor;
void main() {
    vec4 viewPosition = uView * vec4(aPosition, 1.0);
    vViewPosition = viewPosition.xyz;
    vNormal = normalize(uNormalMatrix * aNormal);
    vColor = aColor;
    gl_Position = uProjection * viewPosition;
}
