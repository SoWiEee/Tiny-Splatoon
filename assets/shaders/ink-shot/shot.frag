#version 450 core

struct DirectionalLight { vec3 direction; vec3 color; };
struct HemisphereLight { vec3 direction; vec3 skyColor; vec3 groundColor; };
uniform vec3 ambientLightColor;
uniform DirectionalLight directionalLights[1];
uniform HemisphereLight hemisphereLights[1];
uniform vec4 uViewport;
uniform int uInkShadow;

flat in mat4 vClipToLocal;
flat in mat4 vLocalToClip;
flat in mat4 vLocalToView;
flat in vec4 vShape;
flat in vec3 vInkColor;
flat in float vDetail;
in vec3 vMeshPosition;
in vec3 vMeshNormal;
layout(location = 0) out vec4 fragColor;

float smoothUnion(float a, float b, float k) {
    float h = max(k - abs(a - b), 0.0) / k;
    return min(a, b) - h * h * k * 0.25;
}
float capsule(vec3 p, vec3 a, vec3 b, float radius) {
    vec3 pa = p - a, ba = b - a;
    return length(pa - ba * clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0)) - radius;
}
float inkField(vec3 p) {
    if (vShape.x <= 0.0) return length(p) - 0.92;
    float phase = vShape.z;
    float life = vShape.w;
    float breakup = smoothstep(0.16, 0.62, life);
    float wave = sin(phase + life * 5.0);
    float impulse = vShape.y;
    vec3 bend = vec3(sin(phase + life*6.0), cos(phase*1.3 + life*5.0), 0) * (0.025*vDetail);
    float bodyRadius = 0.72 + 0.025*sin(phase + life*7.0);
    float d = capsule(p, vec3(0.0, 0.0, -0.08) - bend, vec3(0.0, 0.0, 0.32) + bend, bodyRadius);
    if (vDetail <= 0.0) return d;
    float blend = vShape.x * vDetail;
    // One logical round owns these lobes: necks stretch, then detach in flight.
    vec3 shoulder = (vec3(0.48 * cos(phase + life*2.0), 0.33 * sin(phase + life*2.0), -0.40) + bend) * vDetail;
    float shoulderRadius = 0.54 + 0.035*sin(phase*1.7 + life*6.0) + 0.045*impulse;
    d = smoothUnion(d, length(p - shoulder) - shoulderRadius * vDetail, blend);
    vec3 tail = vec3(0.16 * wave, 0.10 * cos(phase + life*3.0), -0.92 - 0.90 * breakup - 0.10*impulse) * vDetail;
    d = smoothUnion(d, length(p - tail) - mix(0.50, 0.29, breakup) * vDetail, blend);
    return d;
}
vec3 fieldNormal(vec3 p) {
    const float e = 0.003;
    return normalize(vec3(
        inkField(p + vec3(e,0,0)) - inkField(p - vec3(e,0,0)),
        inkField(p + vec3(0,e,0)) - inkField(p - vec3(0,e,0)),
        inkField(p + vec3(0,0,e)) - inkField(p - vec3(0,0,e))));
}
vec3 shadeInk(vec3 surface, vec3 localNormal) {
    vec3 normal = normalize(transpose(inverse(mat3(vLocalToView))) * localNormal);
    vec3 viewDirection = normalize(-(vLocalToView * vec4(surface, 1.0)).xyz);
    vec3 sunDirection = normalize(directionalLights[0].direction);
    float sunDiffuse = max(dot(normal, sunDirection), 0.0);
    float hemiFactor = clamp(dot(normal, normalize(hemisphereLights[0].direction)) * 0.5 + 0.5, 0.0, 1.0);
    vec3 hemisphere = mix(hemisphereLights[0].groundColor, hemisphereLights[0].skyColor, hemiFactor);
    vec3 lighting = ambientLightColor + hemisphere * 0.65 + directionalLights[0].color * (0.78 * sunDiffuse);
    vec3 halfSum = sunDirection + viewDirection;
    vec3 halfDirection = dot(halfSum, halfSum) > 1e-6 ? normalize(halfSum) : normal;
    float highlight = pow(max(dot(normal, halfDirection), 0.0), 52.0) * sunDiffuse;
    float fresnel = pow(1.0 - max(dot(normal, viewDirection), 0.0), 3.0);
    return max(vInkColor * lighting + vec3(1.0) * (0.38 * highlight + 0.055 * fresnel), vec3(0));
}
void main() {
    if (vDetail <= 0.0 || vShape.x <= 0.0) {
        if (!gl_FrontFacing) discard;
        gl_FragDepth = gl_FragCoord.z;
        fragColor = uInkShadow != 0 ? vec4(0) : vec4(shadeInk(vMeshPosition, normalize(vMeshNormal)), 1);
        return;
    }
    if (gl_FrontFacing) discard;
    vec2 ndc = (gl_FragCoord.xy - uViewport.xy) / uViewport.zw * 2.0 - 1.0;
    vec4 nearH = vClipToLocal * vec4(ndc, -1.0, 1.0);
    vec4 farH = vClipToLocal * vec4(ndc, 1.0, 1.0);
    vec3 origin = nearH.xyz / nearH.w;
    vec3 direction = normalize(farH.xyz / farH.w - origin);
    float bounds = vShape.x > 0.0 ? 2.6 : 1.05;
    float b = dot(origin, direction);
    float discriminant = b*b - dot(origin, origin) + bounds*bounds;
    if (discriminant < 0.0) discard;
    float t = max(0.0, -b - sqrt(discriminant));
    float end = -b + sqrt(discriminant);
    bool hit = false;
    for (int step = 0; step < 48 && t <= end; ++step) {
        float d = inkField(origin + direction * t);
        if (abs(d) < 0.0025) { hit = true; break; }
        t += max(abs(d) * 0.70, 0.001);
    }
    if (!hit) discard;
    vec3 surface = origin + direction * t;
    vec4 clip = vLocalToClip * vec4(surface, 1.0);
    float depth = clip.z / clip.w * 0.5 + 0.5;
    if (depth < 0.0 || depth > 1.0) discard;
    gl_FragDepth = mix(gl_DepthRange.near, gl_DepthRange.far, depth);
    if (uInkShadow != 0) { fragColor = vec4(0.0); return; }

    fragColor = vec4(shadeInk(surface, fieldNormal(surface)), 1.0);
}
