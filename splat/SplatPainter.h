#pragma once
#include "SplatMap.h"
#include "../engine/rendering/Shader.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "stb_image.h"

class SplatPainter {
private:
    Shader* splatShader;
    unsigned int quadVAO, quadVBO;
    unsigned int splatTextureID;

public:
    SplatPainter() {
        splatShader = new Shader("assets/shaders/splat.vert", "assets/shaders/splat.frag");
        InitQuad();
        LoadTexture("assets/textures/splat_01.png");
    }

    ~SplatPainter() {
        delete splatShader;
        glDeleteVertexArrays(1, &quadVAO);
        glDeleteBuffers(1, &quadVBO);
        glDeleteTextures(1, &splatTextureID);
    }

    void LoadTexture(const char* path) {
        glGenTextures(1, &splatTextureID);
        glBindTexture(GL_TEXTURE_2D, splatTextureID);

        // 設定重複模式 (避免邊緣採樣錯誤)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        int w, h, nrChannels;
        // 翻轉 Y 軸，因為 OpenGL 的紋理座標原點在左下角
        stbi_set_flip_vertically_on_load(true);
        unsigned char* data = stbi_load(path, &w, &h, &nrChannels, 0);

        if (data) {
            GLenum format = (nrChannels == 4) ? GL_RGBA : GL_RGB;
            glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, format, GL_UNSIGNED_BYTE, data);
            glGenerateMipmap(GL_TEXTURE_2D);
        }
        else {
            std::cerr << "Failed to load splat texture: " << path << std::endl;
        }
        stbi_image_free(data);
    }

    void Paint(SplatMap* map, const glm::vec2& uv, float size, const glm::vec3& color, float rotation, int teamID) {
        glBindFramebuffer(GL_FRAMEBUFFER, map->fbo);
        glViewport(0, 0, map->width, map->height);
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        splatShader->Bind();
        // --- 計算 Model Matrix ---
        // 我們的 Quad 原始座標是 -1 到 1 (寬度 2)
        // 目標：縮小到 size，移動到 uv 位置，並旋轉

        glm::mat4 model = glm::mat4(1.0f);

        // 1. 位移 Translate
        // 輸入的 uv 是 0~1，需要轉換到 NDC 座標 (-1 ~ 1)
        // x: 0 -> -1, 1 -> 1  =>  x * 2 - 1
        float ndcX = uv.x * 2.0f - 1.0f;
        float ndcY = uv.y * 2.0f - 1.0f;
        model = glm::translate(model, glm::vec3(ndcX, ndcY, 0.0f));

        // 2. 旋轉 Rotate (Z軸)
        model = glm::rotate(model, glm::radians(rotation), glm::vec3(0.0f, 0.0f, 1.0f));

        // 3. 縮放 Scale
        // 原始 Quad 寬度是 2，我們希望最終大小是 size (相對於 0~1 的 UV 空間)
        // 由於 NDC 是 -1~1 (長度2)，所以縮放係數不需要除以 2 也能得到不錯的效果，視 size 定義而定
        // 這裡假設 size 是 UV 空間的大小 (例如 0.1 代表佔地圖 10%)
        model = glm::scale(model, glm::vec3(size, size, 1.0f));

        splatShader->SetMat4("model", model);
        splatShader->SetVec3("paintColor", color);

        // 綁定貼圖到 Slot 0
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, splatTextureID);
        splatShader->SetInt("splatTexture", 0);

        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        glDisable(GL_BLEND);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // 更新 CPU 端數據 (用於計分)
        // 注意：這裡還是只更新中心點，如果要精確計分可能需要遍歷範圍，但效能考量暫時維持這樣
        map->UpdateCPUData(uv.x, uv.y, teamID);
    }

private:
    void InitQuad() {
        float quadVertices[] = {
            // positions   // texCoords
            -1.0f,  1.0f, 0.0f,  0.0f, 1.0f,
            -1.0f, -1.0f, 0.0f,  0.0f, 0.0f,
             1.0f, -1.0f, 0.0f,  1.0f, 0.0f,

            -1.0f,  1.0f, 0.0f,  0.0f, 1.0f,
             1.0f, -1.0f, 0.0f,  1.0f, 0.0f,
             1.0f,  1.0f, 0.0f,  1.0f, 1.0f
        };

        glGenVertexArrays(1, &quadVAO);
        glGenBuffers(1, &quadVBO);
        glBindVertexArray(quadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));

        glBindVertexArray(0);
    }
};