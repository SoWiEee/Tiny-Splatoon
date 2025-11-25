#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include <cmath>
#include <algorithm>

class SplatMap {
public:
    unsigned int fbo;
    unsigned int textureID;
    int width, height;

    // CPU 端邏輯地圖 (用於潛水判定與快速顏色查詢)
    static const int GRID_SIZE = 100; // 100x100 的邏輯網格
    int gridData[GRID_SIZE][GRID_SIZE]; // 0:無, 1:紅隊, 2:綠隊

    SplatMap(int w, int h) : width(w), height(h) {
        InitFBO();
        ClearCPUData();
    }

    ~SplatMap() {
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &textureID);
    }

    // 綁定貼圖供 Shader 讀取
    void BindTexture(int slot) const {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, textureID);
    }

    // 更新 CPU 端的邏輯網格 (當塗地發生時呼叫)
    void UpdateCPUData(float u, float v, int teamID, int radius = 2) {
        int cx = (int)(u * GRID_SIZE);
        int cy = (int)(v * GRID_SIZE);

        for (int x = cx - radius; x <= cx + radius; x++) {
            for (int y = cy - radius; y <= cy + radius; y++) {
                if (x >= 0 && x < GRID_SIZE && y >= 0 && y < GRID_SIZE) {
                    gridData[x][y] = teamID;
                }
            }
        }
    }

    // 寬容判定：檢查某個位置周圍是否有特定隊伍的顏色
    bool IsColorInArea(float u, float v, int teamID, int radius = 1) {
        int cx = (int)(u * GRID_SIZE);
        int cy = (int)(v * GRID_SIZE);

        for (int x = cx - radius; x <= cx + radius; x++) {
            for (int y = cy - radius; y <= cy + radius; y++) {
                if (x >= 0 && x < GRID_SIZE && y >= 0 && y < GRID_SIZE) {
                    if (gridData[x][y] == teamID) return true;
                }
            }
        }
        return false;
    }

    // 計算分數 (使用 GPU Mipmap 加速)
    glm::vec2 CalculateScore() {
        glBindTexture(GL_TEXTURE_2D, textureID);
        glGenerateMipmap(GL_TEXTURE_2D);

        // 算出最高層級 (1x1 像素)
        int maxLevel = (int)std::floor(std::log2(std::max(width, height)));

        float pixelData[4];
        glGetTexImage(GL_TEXTURE_2D, maxLevel, GL_RGBA, GL_FLOAT, pixelData);

        // pixelData[0] 是紅色通道總和 (Team 1)
        // pixelData[1] 是綠色通道總和 (Team 2)
        return glm::vec2(pixelData[0], pixelData[1]);
    }

private:
    void InitFBO() {
        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);

        glGenTextures(1, &textureID);
        glBindTexture(GL_TEXTURE_2D, textureID);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureID, 0);

        // 清空為黑色
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void ClearCPUData() {
        for (int i = 0; i < GRID_SIZE; i++)
            for (int j = 0; j < GRID_SIZE; j++)
                gridData[i][j] = 0;
    }
};