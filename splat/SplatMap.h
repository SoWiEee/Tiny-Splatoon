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

    static const int GRID_SIZE = 100;
    int gridData[GRID_SIZE][GRID_SIZE];

    SplatMap(int w, int h) : width(w), height(h) {
        InitFBO();
        ClearCPUData();
    }

    ~SplatMap() {
        glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(1, &textureID);
    }

    void BindTexture(int slot) const {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, textureID);
    }

    void UpdateCPUData(float u, float v, float radius, int teamID) {
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

    bool IsColorInArea(float u, float v, int teamID, int radius = 2) {
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

    glm::vec2 CalculateScore() {
        glBindTexture(GL_TEXTURE_2D, textureID);
        glGenerateMipmap(GL_TEXTURE_2D);

        int maxLevel = (int)std::floor(std::log2(std::max(width, height)));

        float pixelData[4];
        glGetTexImage(GL_TEXTURE_2D, maxLevel, GL_RGBA, GL_FLOAT, pixelData);

        return glm::vec2(pixelData[0], pixelData[1]);
    }

    std::pair<float, float> CalculatePercentages() {
        int count1 = 0;
        int count2 = 0;
        int totalPixels = width * height;

        for (int i = 0; i < GRID_SIZE; i++) {
            for (int j = 0; j < GRID_SIZE; j++) {
                if (gridData[i][j] == 1) count1++;
                else if (gridData[i][j] == 2) count2++;
            }
        }

        if (totalPixels == 0) return { 0.0f, 0.0f };

        return { (float)count1 / totalPixels, (float)count2 / totalPixels };
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