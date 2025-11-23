#version 450 core
out vec4 FragColor;

in vec2 TexCoord;

uniform sampler2D brushTexture; // 筆刷形狀貼圖
uniform vec3 paintColor;        // 隊伍顏色

void main() {
    // 讀取筆刷貼圖的 Alpha 值
    float alpha = texture(brushTexture, TexCoord).r;
    
    // 如果貼圖是黑色的部分，就不要畫 (discard)，避免把原本的墨水蓋成黑色
    if (alpha < 0.1) discard;

    // 輸出的顏色: RGB = 隊伍顏色, Alpha = 筆刷形狀強度
    FragColor = vec4(paintColor, alpha);
}