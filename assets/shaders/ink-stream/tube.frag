#version 450 core
in vec3 vNormal;
in vec3 vViewPosition;
in vec3 vColor;
layout(location=0) out vec4 outColor;
uniform bool uShadow;
uniform vec3 ambientLightColor;
struct DirectionalLight { vec3 direction; vec3 color; };
struct HemisphereLight { vec3 direction; vec3 skyColor; vec3 groundColor; };
uniform DirectionalLight directionalLights[1];
uniform HemisphereLight hemisphereLights[1];
uniform vec3 fogColor;
uniform float fogNear;
uniform float fogFar;
uniform float toneMappingExposure;
vec3 shadeInk(vec3 normal, vec3 viewPosition, vec3 color) {
    vec3 n = normalize(normal);
    vec3 l = normalize(directionalLights[0].direction);
    float diffuse = max(dot(n, l), 0.0);
    float hemiAmount = clamp(dot(n, normalize(hemisphereLights[0].direction)) * 0.5 + 0.5, 0.0, 1.0);
    vec3 hemi = mix(hemisphereLights[0].groundColor, hemisphereLights[0].skyColor, hemiAmount);
    vec3 halfVector = normalize(l + normalize(-viewPosition));
    float specular = pow(max(dot(n, halfVector), 0.0), 32.0) * 0.12;
    return color * max(ambientLightColor + hemi + directionalLights[0].color * diffuse, vec3(0.0))
         + directionalLights[0].color * specular;
}
void main() {
    if (uShadow) { outColor = vec4(0.0); return; }
    vec3 color = shadeInk(vNormal, vViewPosition, vColor) * toneMappingExposure;
    color = mix(color, fogColor, smoothstep(fogNear, max(fogFar, fogNear + 1e-4), -vViewPosition.z));
    outColor = vec4(color, 1.0);
}
