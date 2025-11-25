#pragma once
#include "SplatMap.h"
#include "../engine/rendering/Shader.h"

class SplatPainter {
private:
    Shader* paintShader;
    unsigned int quadVAO, quadVBO; // 筆刷模型

public:
    SplatPainter(); // 初始化 Shader 和 Quad

    // 執行繪製指令
    void Paint(SplatMap* map, const glm::vec2& uv, float size, const glm::vec3& color, float rotation) {
        // 1. Bind FBO (map->fbo)
        // 2. Setup Shader (paintShader)
        // 3. Draw Quad
        // 4. Update CPU Data (map->UpdateCPUData)
        // 移植 InkMap::Paint 的內容到這裡
    }
};