#pragma once
#include <vector>
#include <glm/glm.hpp>

// 頂點結構
struct LevelVertex {
    glm::vec3 position;
    glm::vec3 normal;
    glm::vec2 uv;
};

class LevelGeometry {
public:
    // 產生一個方塊 (平地、箱子、牆壁都通用)
    // x, y, z: 中心位置
    // size: 大小 (半徑)
    // mapSize: 整個地圖的總寬度 (用來計算全域 UV，這對 SplatMap 很重要！)
    static void AddCube(std::vector<LevelVertex>& verts, float x, float y, float z, float size, float mapSize) {
        // 為了簡化，這裡只產生 "上面 (Top)"、"側面"
        // SplatMap 主要是由上往下投影，所以 UV 計算最重要

        float s = size;

        // 定義 8 個角落
        glm::vec3 p0(-s, -s, s); glm::vec3 p1(s, -s, s);
        glm::vec3 p2(s, s, s); glm::vec3 p3(-s, s, s);
        glm::vec3 p4(-s, -s, -s); glm::vec3 p5(s, -s, -s);
        glm::vec3 p6(s, s, -s); glm::vec3 p7(-s, s, -s);

        // 位移
        glm::vec3 offset(x, y, z);
        p0 += offset; p1 += offset; p2 += offset; p3 += offset;
        p4 += offset; p5 += offset; p6 += offset; p7 += offset;

        // 輔助 Lambda: 產生面
        auto AddFace = [&](glm::vec3 a, glm::vec3 b, glm::vec3 c, glm::vec3 d, glm::vec3 n) {
            // 計算全域 UV (Planar Projection from Top)
            // 將 XZ 座標映射到 0~1
            // 假設地圖中心是 0,0，範圍是 -mapSize/2 到 mapSize/2
            auto GetUV = [&](glm::vec3 p) {
                return glm::vec2((p.x / mapSize) + 0.5f, (p.z / mapSize) + 0.5f);
                };

            verts.push_back({ a, n, GetUV(a) });
            verts.push_back({ b, n, GetUV(b) });
            verts.push_back({ c, n, GetUV(c) });
            verts.push_back({ c, n, GetUV(c) });
            verts.push_back({ d, n, GetUV(d) });
            verts.push_back({ a, n, GetUV(a) });
            };

        // Top Face (最重要的面，用來塗地)
        AddFace(p7, p6, p2, p3, glm::vec3(0, 1, 0));

        // Side Faces (牆壁)
        AddFace(p3, p2, p1, p0, glm::vec3(0, 0, 1)); // Front
        AddFace(p2, p6, p5, p1, glm::vec3(1, 0, 0)); // Right
        AddFace(p6, p7, p4, p5, glm::vec3(0, 0, -1)); // Back
        AddFace(p7, p3, p0, p4, glm::vec3(-1, 0, 0)); // Left
    }

    // 產生斜坡 (假設是向北 ^)
    static void AddRampNorth(std::vector<LevelVertex>& verts, float x, float y, float z, float size, float mapSize) {
        float s = size;
        glm::vec3 offset(x, y, z);

        // 斜坡只有 6 個點 (像個楔子)
        // 低端 (南)
        glm::vec3 l1(-s, -s, s); glm::vec3 l2(s, -s, s);
        // 高端 (北)
        glm::vec3 h1(-s, s, -s); glm::vec3 h2(s, s, -s);
        // 底部
        glm::vec3 b1(-s, -s, -s); glm::vec3 b2(s, -s, -s);

        l1 += offset; l2 += offset; h1 += offset; h2 += offset; b1 += offset; b2 += offset;

        auto GetUV = [&](glm::vec3 p) { return glm::vec2((p.x / mapSize) + 0.5f, (p.z / mapSize) + 0.5f); };

        // 斜面 (Slope)
        // 法線需要計算：(0, 1, 1) normalize -> (0, 0.7, 0.7)
        glm::vec3 n = glm::normalize(glm::vec3(0, 1, 1));

        verts.push_back({ l1, n, GetUV(l1) });
        verts.push_back({ l2, n, GetUV(l2) });
        verts.push_back({ h2, n, GetUV(h2) });
        verts.push_back({ h2, n, GetUV(h2) });
        verts.push_back({ h1, n, GetUV(h1) });
        verts.push_back({ l1, n, GetUV(l1) });

        // 側面 (三角形)
        // ... (為了簡化，側面暫時省略，或是直接用牆壁補)
        // 實際上斜坡通常兩邊會有牆壁，所以側面看不到沒關係
    }
};