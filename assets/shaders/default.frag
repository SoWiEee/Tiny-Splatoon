#version 450 core
out vec4 FragColor;

in vec2 TexCoord;
in vec3 FragPos;
in vec3 Normal;

// texture
uniform vec3 objectColor;
uniform sampler2D mainTexture;
uniform int useTexture;
uniform float tiling;

// ink
uniform sampler2D inkMap;
uniform int useInk;

// lighting
uniform vec3 viewPos;
vec3 lightDir = normalize(vec3(0.5, 0.8, 0.3)); 
vec3 lightColor = vec3(1.0, 0.95, 0.9);
uniform float alpha = 1.0;

void main() {
    vec4 baseColor = vec4(objectColor, 1.0);
    
    if (useTexture == 1) {
        baseColor = texture(mainTexture, TexCoord * tiling);
    }

    // Ink Mixing
    float inkFactor = 0.0;
    vec3 surfaceNormal = normalize(Normal);
    float roughness = 0.8; // 預設粗糙度 (0=光滑, 1=粗糙)

    if (useInk == 1) {
        vec4 inkSample = texture(inkMap, TexCoord); // 墨水不用 tiling
        inkFactor = inkSample.a;
        baseColor.rgb = mix(baseColor.rgb, inkSample.rgb, inkFactor);
        roughness = mix(0.8, 0.1, inkFactor); 

        // simple bump map
        if (inkFactor > 0.01) {
            float h = 2.0;
            float dx = dFdx(inkFactor) * h;
            float dy = dFdy(inkFactor) * h;
            surfaceNormal = normalize(surfaceNormal + vec3(-dx, -dy, 0.0));
        }
    }

    // Blinn-Phong
    float ambientStrength = 0.4; // 地板有貼圖後可以稍微亮一點
    vec3 ambient = ambientStrength * lightColor;
    float diff = max(dot(surfaceNormal, lightDir), 0.0);
    vec3 diffuse = diff * lightColor;
    vec3 viewDir = normalize(viewPos - FragPos);
    vec3 halfwayDir = normalize(lightDir + viewDir); // Blinn-Phong 核心
    
    // 根據粗糙度計算高光強度
    float specPower = (1.0 - roughness) * 64.0; // 越光滑，次數越高
    float spec = pow(max(dot(surfaceNormal, halfwayDir), 0.0), specPower);
    
    // 越粗糙，高光越弱
    vec3 specular = vec3(1.0) * spec * (1.0 - roughness); 

    vec3 result = (ambient + diffuse) * baseColor.rgb + specular;
    
    // 簡單的霧氣效果 (隨距離變深灰)
    // float dist = length(viewPos - FragPos);
    // float fogFactor = smoothstep(10.0, 50.0, dist);
    // result = mix(result, vec3(0.1, 0.1, 0.1), fogFactor);

    FragColor = vec4(result, alpha);
}