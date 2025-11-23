#version 450 core
out vec4 FragColor;

in vec2 TexCoord;

uniform vec3 objectColor;
uniform sampler2D floorTexture;
uniform sampler2D inkMap;

void main() {
    vec4 inkValue = texture(inkMap, TexCoord);
    
    // 2. 基礎地板顏色 (這裡暫時用 uniform 顏色，也可以換成地板貼圖)
    vec3 baseColor = objectColor; 
    // 如果你有地板貼圖，就用: vec3 baseColor = texture(floorTexture, TexCoord).rgb;

    // 3. 混合
    vec3 finalColor = mix(baseColor, inkValue.rgb, inkValue.a);

    FragColor = vec4(finalColor, 1.0);
}