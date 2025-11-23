#version 450 core
out vec4 FragColor;

in vec2 TexCoord;

uniform float inkLevel; // 0.0 到 1.0 (墨水量)
uniform vec3 inkColor;

void main() {
    // 計算當前像素距離中心的距離 (中心點是 0.5, 0.5)
    vec2 center = vec2(0.5);
    float dist = distance(TexCoord, center);

    vec4 finalColor = vec4(0.0); // 預設透明

    // 1. 中心準點 (一個小白點)
    // 半徑 0.05 內是白色
    if (dist < 0.05) {
        finalColor = vec4(1.0, 1.0, 1.0, 1.0); 
    }
    // 黑色邊框
    else if (dist < 0.07) {
        finalColor = vec4(0.0, 0.0, 0.0, 1.0);
    }

    // 2. 外圍墨水計量圈
    // 範圍在半徑 0.35 到 0.45 之間
    float ringWidth = 0.45;
    float ringInner = 0.35;

    if (dist > ringInner && dist < ringWidth) {
        vec2 dir = TexCoord - center;
        float angle = atan(dir.y, dir.x); // -PI ~ PI
        float normalizedAngle = (angle + 3.14159) / (2.0 * 3.14159); 
        
        // 如果這個角度在 inkLevel 範圍內，顯示顏色
        if (normalizedAngle < inkLevel) {
             finalColor = vec4(inkColor, 1.0);
        } else {
             finalColor = vec4(0.2, 0.2, 0.2, 0.5);
        }
    }

    if (finalColor.a == 0.0) discard;

    FragColor = finalColor;
}