#version 450 core
out vec4 FragColor;

in vec2 TexCoord;

uniform sampler2D floorTexture;
uniform sampler2D inkMap;
uniform vec3 objectColor;

uniform int useInk; 

void main() {
    // 預設顏色
    vec3 finalColor = objectColor;

    // 只有當 useInk 開啟時，才去混合墨水
    if (useInk == 1) {
        vec4 inkValue = texture(inkMap, TexCoord);
        // 混合：底色 + 墨水色 (根據墨水 Alpha)
        finalColor = mix(finalColor, inkValue.rgb, inkValue.a);
    }

    FragColor = vec4(finalColor, 1.0);
}