#version 450 core
out vec4 FragColor;

in vec2 TexCoord; // 0.0 ~ 1.0

uniform float scoreA; // 紅隊分數 (0.0 ~ 1.0)
uniform float scoreB; // 綠隊分數 (0.0 ~ 1.0)
uniform vec3 colorA;  // 紅隊顏色
uniform vec3 colorB;  // 綠隊顏色

void main() {
    // 定義背景色 (深灰色)
    vec4 bgColor = vec4(0.2, 0.2, 0.2, 0.8);
    
    vec4 finalColor = bgColor;

    // 邏輯：根據 UV 的 x 座標來決定畫什麼顏色
    
    // 1. 左邊 (0.0 -> scoreA) 顯示 A 隊顏色
    if (TexCoord.x < scoreA) {
        finalColor = vec4(colorA, 1.0);
    }
    // 2. 右邊 (1.0 - scoreB -> 1.0) 顯示 B 隊顏色
    else if (TexCoord.x > (1.0 - scoreB)) {
        finalColor = vec4(colorB, 1.0);
    }
    
    // 增加一點立體感/邊框 (可選)
    // 如果在 A 隊進度條的邊緣
    if (abs(TexCoord.x - scoreA) < 0.005 && TexCoord.x < scoreA) {
        finalColor += vec4(0.2); // 加亮邊緣
    }

    FragColor = finalColor;
}