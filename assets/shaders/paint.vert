#version 450 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 TexCoord;

uniform vec2 hitUV;      // 擊中點的 UV 座標 (0.0 ~ 1.0)
uniform float brushSize; // 筆刷大小

void main() {
    // aPos 是一個標準的 Quad (-1 ~ 1)
    // 我們需要把它縮小，並移動到 hitUV 的位置
    
    // 1. 將 hitUV (0~1) 轉換到 Clip Space (-1~1)
    vec2 centerPos = hitUV * 2.0 - 1.0;
    
    // 2. 計算最終位置
    // 我們直接在 2D 螢幕空間操作，Z 設為 0
    gl_Position = vec4(centerPos + aPos.xy * brushSize, 0.0, 1.0);
    
    TexCoord = aTexCoord;
}