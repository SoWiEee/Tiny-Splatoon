#version 450 core

in vec3 position;
in mat4 instanceMatrix;
in vec4 aShape;
in vec3 instanceColor;
uniform mat4 modelViewMatrix;
uniform mat4 projectionMatrix;
uniform vec4 uViewport;

flat out mat4 vClipToLocal;
flat out mat4 vLocalToClip;
flat out mat4 vLocalToView;
flat out vec4 vShape;
flat out vec3 vInkColor;
flat out float vDetail;
out vec3 vMeshPosition;
out vec3 vMeshNormal;

void main() {
    vLocalToView = modelViewMatrix * instanceMatrix;
    vLocalToClip = projectionMatrix * vLocalToView;
    vClipToLocal = inverse(vLocalToClip);
    vShape = aShape;
    vInkColor = instanceColor;
    float viewRadius = length(vLocalToView[0].xyz);
    float depth = max(abs(vLocalToView[3].z), 0.001);
    float pixels = viewRadius * abs(projectionMatrix[1][1]) * uViewport.w * 0.5 / depth;
    vDetail = smoothstep(2.0, 5.0, pixels);
    if (vDetail <= 0.0 || aShape.x <= 0.0) {
        vec3 scale = aShape.x > 0.0 ? vec3(0.72, 0.72, 0.92) : vec3(0.92);
        vMeshPosition = position * scale + vec3(0, 0, aShape.x > 0.0 ? 0.12 : 0.0);
        vMeshNormal = normalize(position / scale);
        gl_Position = vLocalToClip * vec4(vMeshPosition, 1.0);
        return;
    }
    vMeshPosition = vec3(0);
    vMeshNormal = vec3(0, 1, 0);
    // Back faces keep covering the field when the camera enters its proxy.
    float bounds = aShape.x > 0.0 ? 2.6 : 1.05;
    gl_Position = vLocalToClip * vec4(position * bounds, 1.0);
}
