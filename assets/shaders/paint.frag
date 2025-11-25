#version 450 core
out vec4 FragColor;

in vec2 TexCoord; // 這裡接收到的是 0.0 到 1.0 的方形範圍

// uniform sampler2D brushTexture; // [修改] 我們不再需要這張貼圖了，用數學畫
uniform vec3 paintColor;        // 隊伍顏色

void main() {
    // --- 數學畫圓核心邏輯 ---

    // 1. 定義中心點 (在 UV 座標系中，中心是 0.5, 0.5)
    vec2 center = vec2(0.5, 0.5);

    // 2. 計算當前像素距離中心的距離
    float dist = distance(TexCoord, center);

    // 3. 計算圓形 Alpha 值 (使用 smoothstep 製作柔和邊緣)
    // 筆刷的半徑是 0.5 (因為 UV 是 0~1)
    // smoothstep(下界, 上界, 輸入值):
    // 如果 輸入值 < 下界，回傳 0.0
    // 如果 輸入值 > 上界，回傳 1.0
    // 如果在中間，回傳平滑插值
    
    // 我們希望距離中心越近越不透明(Alpha=1)，越靠近邊緣越透明(Alpha=0)
    // 這裡設定：距離中心 0.35 內是實心，0.35 到 0.5 之間漸變透明
    float alpha = 1.0 - smoothstep(0.35, 0.5, dist);

    // 4. 丟棄完全透明的像素 (優化效能，也避免留下方形的透明外框)
    if (alpha <= 0.01) discard;

    // 5. 輸出最終顏色
    FragColor = vec4(paintColor, alpha);
}