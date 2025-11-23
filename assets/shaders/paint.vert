#version 450 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec2 aTexCoord;

out vec2 TexCoord;

uniform vec2 hitUV;      // 擊中點的 UV 座標 (0.0 ~ 1.0)
uniform float brushSize; // 筆刷大小
uniform float rotation;

void main() {
    
    vec2 centerPos = hitUV * 2.0 - 1.0;
    
    float s = sin(rotation);
    float c = cos(rotation);
    mat2 rotMat = mat2(c, -s, s, c);
    
    // 先旋轉頂點，再縮放
    vec2 rotatedPos = rotMat * aPos.xy;
    
    gl_Position = vec4(centerPos + rotatedPos * brushSize, 0.0, 1.0);
    TexCoord = aTexCoord;
}