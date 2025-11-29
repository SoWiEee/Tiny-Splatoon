#version 450 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D splatTexture;
uniform vec3 paintColor;

void main()
{
    // 讀取貼圖的 Alpha 值 (形狀)
    float alpha = texture(splatTexture, TexCoords).a;

    // 硬邊緣處理 (Hard Edge)：讓墨水看起來更像卡通風格，避免邊緣糊糊的
    // 也可以改成 if (alpha < 0.1) discard; 
    if (alpha < 0.5) 
        discard;

    // 輸出顏色
    // RGB = 墨水顏色
    // A = 1.0 (完全覆蓋舊的顏色)
    FragColor = vec4(paintColor, 1.0);
}