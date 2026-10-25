#version 450 core
in vec2 vUv;
layout(location=0) out vec4 outColor;
uniform sampler2D uColorMask;
uniform sampler2D uDepthTexture;
uniform vec2 uSize;
uniform vec2 uDepthRange;
uniform bool uOrthographic;
uniform mat4 uInverseProjection;
uniform mat4 uProjection;
uniform vec3 ambientLightColor;
struct DirectionalLight { vec3 direction; vec3 color; };
struct HemisphereLight { vec3 direction; vec3 skyColor; vec3 groundColor; };
uniform DirectionalLight directionalLights[1];
uniform HemisphereLight hemisphereLights[1];
uniform vec3 fogColor;
uniform float fogNear;
uniform float fogFar;
uniform float toneMappingExposure;
bool validAt(ivec2 p) {
    ivec2 size = ivec2(uSize);
    if (any(lessThan(p, ivec2(0))) || any(greaterThanEqual(p, size))) return false;
    return texelFetch(uColorMask, p, 0).a > 0.5;
}
float positiveViewDepth(ivec2 p) {
    float depth = texelFetch(uDepthTexture, p, 0).r;
    float ndcDepth = depth * 2.0 - 1.0;
    vec2 ndc = (vec2(p) + 0.5) / uSize * 2.0 - 1.0;
    vec4 v = uInverseProjection * vec4(ndc, ndcDepth, 1.0);
    return -v.z / v.w;
}
float filteredDepth(ivec2 p, float centerDepth) {
    float sum = 0.0, weightSum = 0.0;
    float threshold = max(0.025, centerDepth * 0.025);
    for (int y = -1; y <= 1; ++y) for (int x = -1; x <= 1; ++x) {
        ivec2 q = p + ivec2(x, y);
        if (!validAt(q)) continue;
        float z = positiveViewDepth(q);
        float difference = abs(z - centerDepth);
        if (difference > threshold) continue;
        float spatial = 1.0 / (1.0 + abs(float(x)) + abs(float(y)));
        float weight = spatial * exp(-difference / max(threshold, 1e-4));
        sum += z * weight;
        weightSum += weight;
    }
    return weightSum > 0.0 ? sum / weightSum : centerDepth;
}
vec3 reconstructView(ivec2 p, float positiveDepth) {
    vec2 ndc = (vec2(p) + 0.5) / uSize * 2.0 - 1.0;
    if (uOrthographic) {
        vec4 nearH = uInverseProjection * vec4(ndc, -1.0, 1.0);
        vec3 nearP = nearH.xyz / nearH.w;
        return vec3(nearP.xy, -positiveDepth);
    }
    vec4 farH = uInverseProjection * vec4(ndc, 1.0, 1.0);
    vec3 ray = farH.xyz / farH.w;
    return ray * (positiveDepth / max(-ray.z, 1e-5));
}
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
    ivec2 p = clamp(ivec2(vUv * uSize), ivec2(0), ivec2(uSize) - 1);
    if (!validAt(p)) discard;
    float centerDepth = positiveViewDepth(p);
    float depth = filteredDepth(p, centerDepth);
    vec3 center = reconstructView(p, depth);
    ivec2 leftP = p + ivec2(-1, 0), rightP = p + ivec2(1, 0);
    ivec2 downP = p + ivec2(0, -1), upP = p + ivec2(0, 1);
    bool hasLeft = validAt(leftP), hasRight = validAt(rightP);
    bool hasDown = validAt(downP), hasUp = validAt(upP);
    vec3 left = hasLeft ? reconstructView(leftP, filteredDepth(leftP, positiveViewDepth(leftP))) : center;
    vec3 right = hasRight ? reconstructView(rightP, filteredDepth(rightP, positiveViewDepth(rightP))) : center;
    vec3 down = hasDown ? reconstructView(downP, filteredDepth(downP, positiveViewDepth(downP))) : center;
    vec3 up = hasUp ? reconstructView(upP, filteredDepth(upP, positiveViewDepth(upP))) : center;
    vec3 dx = !hasLeft ? right - center : !hasRight ? center - left
        : abs(right.z - center.z) < abs(left.z - center.z) ? right - center : center - left;
    vec3 dy = !hasDown ? up - center : !hasUp ? center - down
        : abs(up.z - center.z) < abs(down.z - center.z) ? up - center : center - down;
    vec3 normal = cross(dx, dy);
    normal = dot(normal, normal) > 1e-10 ? normalize(normal) : vec3(0.0, 0.0, 1.0);
    if (normal.z < 0.0) normal = -normal;
    vec4 clip = uProjection * vec4(center, 1.0);
    gl_FragDepth = mix(uDepthRange.x, uDepthRange.y, clip.z / clip.w * 0.5 + 0.5);
    vec3 color = texelFetch(uColorMask, p, 0).rgb;
    vec3 lit = shadeInk(normal, center, color) * toneMappingExposure;
    lit = mix(lit, fogColor, smoothstep(fogNear, max(fogFar, fogNear + 1e-4), -center.z));
    outColor = vec4(lit, 1.0);
}
