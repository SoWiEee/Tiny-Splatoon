#version 450 core
in vec2 vUv;
layout(location=0) out vec4 outColor;
const int MAX_NODES = 48;
uniform int uCount;
uniform vec4 uNodes[MAX_NODES];
uniform int uDropCount;
uniform vec4 uDrops[16];
uniform vec3 uBoundsMin;
uniform vec3 uBoundsMax;
uniform vec3 uEye;
uniform mat4 uInverseView;
uniform mat4 uInverseProjection;
uniform mat4 uView;
uniform mat4 uProjection;
uniform bool uOrthographic;
uniform vec2 uDepthRange;
uniform vec3 uInkColor;
uniform vec3 ambientLightColor;
struct DirectionalLight { vec3 direction; vec3 color; };
struct HemisphereLight { vec3 direction; vec3 skyColor; vec3 groundColor; };
uniform DirectionalLight directionalLights[1];
uniform HemisphereLight hemisphereLights[1];
uniform vec3 fogColor;
uniform float fogNear;
uniform float fogFar;
uniform float toneMappingExposure;
float capsuleDistance(vec3 p, vec4 a, vec4 b) {
    vec3 ab = b.xyz - a.xyz;
    float t = dot(ab, ab) > 1e-10 ? clamp(dot(p - a.xyz, ab) / dot(ab, ab), 0.0, 1.0) : 0.0;
    return length(p - mix(a.xyz, b.xyz, t)) - mix(a.w, b.w, t);
}
float smoothMin(float a, float b, float k) {
    float h = max(k - abs(a - b), 0.0) / k;
    return min(a, b) - h * h * k * 0.25;
}
float sceneDistance(vec3 p) {
    float d = length(p - uNodes[0].xyz) - uNodes[0].w;
    for (int i = 1; i < uCount; ++i)
        d = smoothMin(d, capsuleDistance(p, uNodes[i - 1], uNodes[i]), 0.04);
    for (int i = 0; i < uDropCount; ++i)
        d = smoothMin(d, length(p-uDrops[i].xyz)-uDrops[i].w, 0.04);
    return d;
}
vec3 sceneNormal(vec3 p) {
    const float e = 0.002;
    return normalize(vec3(sceneDistance(p + vec3(e,0,0)) - sceneDistance(p - vec3(e,0,0)),
                          sceneDistance(p + vec3(0,e,0)) - sceneDistance(p - vec3(0,e,0)),
                          sceneDistance(p + vec3(0,0,e)) - sceneDistance(p - vec3(0,0,e))));
}
vec3 shadeInk(vec3 normal, vec3 viewPosition) {
    vec3 n = normalize(normal);
    vec3 l = normalize(directionalLights[0].direction);
    float diffuse = max(dot(n, l), 0.0);
    float hemiAmount = clamp(dot(n, normalize(hemisphereLights[0].direction)) * 0.5 + 0.5, 0.0, 1.0);
    vec3 hemi = mix(hemisphereLights[0].groundColor, hemisphereLights[0].skyColor, hemiAmount);
    vec3 halfVector = normalize(l + normalize(-viewPosition));
    float specular = pow(max(dot(n, halfVector), 0.0), 32.0) * 0.12;
    return uInkColor * max(ambientLightColor + hemi + directionalLights[0].color * diffuse, vec3(0.0))
         + directionalLights[0].color * specular;
}
void main() {
    vec2 ndc = vUv * 2.0 - 1.0;
    vec4 nearH = uInverseProjection * vec4(ndc, -1.0, 1.0);
    vec4 farH = uInverseProjection * vec4(ndc, 1.0, 1.0);
    vec3 nearView = nearH.xyz / nearH.w;
    vec3 farView = farH.xyz / farH.w;
    vec3 nearWorld = (uInverseView * vec4(nearView, 1.0)).xyz;
    vec3 farWorld = (uInverseView * vec4(farView, 1.0)).xyz;
    vec3 origin = uOrthographic ? nearWorld : uEye;
    vec3 direction = normalize(uOrthographic ? farWorld - nearWorld : farWorld - uEye);
    vec3 safeDirection = vec3(abs(direction.x) < 1e-6 ? (direction.x < 0.0 ? -1e-6 : 1e-6) : direction.x,
                              abs(direction.y) < 1e-6 ? (direction.y < 0.0 ? -1e-6 : 1e-6) : direction.y,
                              abs(direction.z) < 1e-6 ? (direction.z < 0.0 ? -1e-6 : 1e-6) : direction.z);
    vec3 inverseDirection = 1.0 / safeDirection;
    vec3 t0 = (uBoundsMin - origin) * inverseDirection;
    vec3 t1 = (uBoundsMax - origin) * inverseDirection;
    vec3 lo = min(t0, t1), hi = max(t0, t1);
    float tNear = max(max(lo.x, lo.y), lo.z);
    float tFar = min(min(hi.x, hi.y), hi.z);
    float t = max(tNear, 0.0);
    if (tFar < t) discard;
    vec3 hit = vec3(0.0);
    bool found = false;
    for (int step = 0; step < 48; ++step) {
        vec3 p = origin + direction * t;
        float d = sceneDistance(p);
        if (abs(d) < 0.0015) { hit = p; found = true; break; }
        t += max(abs(d) * 0.65, 0.001);
        if (t > tFar) break;
    }
    if (!found) discard;
    vec3 normal = sceneNormal(hit);
    vec4 viewHit = uView * vec4(hit, 1.0);
    vec4 clip = uProjection * viewHit;
    gl_FragDepth = mix(uDepthRange.x, uDepthRange.y, clip.z / clip.w * 0.5 + 0.5);
    vec3 color = shadeInk(mat3(uView) * normal, viewHit.xyz) * toneMappingExposure;
    color = mix(color, fogColor, smoothstep(fogNear, max(fogFar, fogNear + 1e-4), -viewHit.z));
    outColor = vec4(color, 1.0);
}
