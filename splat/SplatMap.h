#pragma once
#include <glm/glm.hpp>
#include <vector>

class SplatMap {
public:
    unsigned int fbo;
    unsigned int textureID; // RGBA32F texture
    int width, height;

    // CPU 端資料 (供邏輯判斷用)
    static const int GRID_SIZE = 100;
    int gridData[GRID_SIZE][GRID_SIZE];

    SplatMap(int w, int h); // 移植 InkMap 的 Init()
    ~SplatMap();

    void BindTexture(int slot) const;

    // 移植 UpdateMapData
    void UpdateCPUData(float u, float v, int teamID);
    int GetTeamAt(float u, float v);
    bool IsColorInArea(float u, float v, int teamID, int radius);

    // 計算分數
    glm::vec2 CalculateScore();
};