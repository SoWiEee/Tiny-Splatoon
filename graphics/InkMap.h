#pragma once
#include "Shader.h"
#include <glad/glad.h>
#include <iostream>
#include <vector>

class InkMap {
public:
    unsigned int FBO;
    unsigned int textureID;
    int width, height;

    // 繪圖相關資源
    unsigned int quadVAO = 0, quadVBO;
    Shader* paintShader;
    // CPU 端的地圖資料
    static const int GRID_SIZE = 100;
    int mapData[GRID_SIZE][GRID_SIZE]; // 0:無, 1:紅隊, 2:綠隊

    InkMap(int w, int h) : width(w), height(h) {
        Init();
        InitPaintResources();

        for (int i = 0; i < GRID_SIZE; i++)
            for (int j = 0; j < GRID_SIZE; j++)
                mapData[i][j] = 0;
    }

    ~InkMap() {
        glDeleteFramebuffers(1, &FBO);
        glDeleteTextures(1, &textureID);
        glDeleteVertexArrays(1, &quadVAO);
        glDeleteBuffers(1, &quadVBO);
        delete paintShader;
    }

    void Init() {
        glGenFramebuffers(1, &FBO);
        glBindFramebuffer(GL_FRAMEBUFFER, FBO);

        // 建立 Texture
        glGenTextures(1, &textureID);
        glBindTexture(GL_TEXTURE_2D, textureID);

        // RGBA 格式，浮點數 (為了精確混合，雖然 unsigned byte 也可以)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        // 避免 UV 超出 0-1 時重複
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        // 3. 把 Texture 綁定到 FBO 的顏色輸出
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureID, 0);

        // 檢查 FBO 是否完整
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::cout << "ERROR::FRAMEBUFFER:: Framebuffer is not complete!" << std::endl;

        // 4. 清空畫布為黑色 (完全透明/無墨水)
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // 初始化畫筆用的 Shader 和 Quad
    void InitPaintResources() {
        paintShader = new Shader("assets/shaders/paint.vert", "assets/shaders/paint.frag");

        // 建立一個簡單的 Quad (-1 到 1)
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
    }

    void BindTexture(int slot = 0) {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, textureID);
    }

    void EnableWriting() {
        glBindFramebuffer(GL_FRAMEBUFFER, FBO);
        glViewport(0, 0, width, height); // Viewport 必須設為貼圖大小
    }

    void DisableWriting(int screenW, int screenH) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, screenW, screenH); // Viewport 改回螢幕大小
    }

    // 在指定 UV 位置畫一筆
    // brushTexID: 筆刷形狀的 Texture ID
    void Paint(glm::vec2 hitUV, float size, glm::vec3 color, unsigned int brushTexID, float rotation = 0.0f) {
        glBindFramebuffer(GL_FRAMEBUFFER, FBO);
        glViewport(0, 0, width, height);

        // 2. 希望新的墨水直接蓋上去
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        // 如果想要永遠覆蓋不變淡，可以用: glBlendFunc(GL_ONE, GL_ZERO);

        // 3. 設定 Shader
        paintShader->use();
        paintShader->setVec2("hitUV", hitUV);
        paintShader->setFloat("brushSize", size);
        paintShader->setVec3("paintColor", color);
        paintShader->setFloat("rotation", rotation); // 傳入旋轉角度給 Shader

        // 綁定筆刷形狀貼圖
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, brushTexID);
        paintShader->setInt("brushTexture", 0);

        // 4. 畫出 Quad
        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        // 5. 復原狀態
        glDisable(GL_BLEND);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        // 恢復原本螢幕的 Viewport，假設是 1280x720
        glViewport(0, 0, 1280, 720);
    }

    // 更新 CPU 地圖資料 (當子彈擊中時呼叫)
    // u, v: 0.0 ~ 1.0
    // colorID: 1=Red, 2=Green
    void UpdateMapData(float u, float v, int colorID) {
        int cx = (int)(u * GRID_SIZE);
        int cy = (int)(v * GRID_SIZE);

        // 簡單塗一個 3x3 的格子，模擬墨水擴散
        int radius = 2;
        for (int x = cx - radius; x <= cx + radius; x++) {
            for (int y = cy - radius; y <= cy + radius; y++) {
                if (x >= 0 && x < GRID_SIZE && y >= 0 && y < GRID_SIZE) {
                    mapData[x][y] = colorID;
                }
            }
        }
    }

    // 查詢某個位置的顏色
    int GetColorAt(float u, float v) {
        int x = (int)(u * GRID_SIZE);
        int y = (int)(v * GRID_SIZE);
        if (x >= 0 && x < GRID_SIZE && y >= 0 && y < GRID_SIZE) {
            return mapData[x][y];
        }
        return 0; // 沒墨水
    }
};