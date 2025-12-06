#pragma once
#include <vector>
#include <iostream>
#include <memory>
#include <glm/glm.hpp>
#include "Entity.h"
#include "FloorMesh.h"
#include "../engine/rendering/Texture.h"
#include "../engine/GameObject.h"

struct AABB {
    glm::vec3 min; // 左下後 (World Space)
    glm::vec3 max; // 右上前 (World Space)
};

class Level {
public:
    FloorMesh* floor = nullptr;
    std::vector<Entity*> walls;
    std::vector<glm::vec3> itemSpawnPoints;

    // 障礙物列表
    std::vector<Entity*> obstacles; // 視覺物件
    std::vector<AABB> colliders;    // 物理碰撞資料

    std::shared_ptr<Texture> floorTex;
    std::shared_ptr<Texture> wallTex;

    float mapSize = 80.0f; // 地圖大小 80x80

    // 重生點 (高空) 與 落地點 (基地)
    // Team 1 (紅): Z 軸負方向 (左下角附近)
    glm::vec3 spawnPointTeam1 = glm::vec3(-30, 25.0f, -30.0f);
    glm::vec3 landingPointTeam1 = glm::vec3(-30, 0.5f, -30.0f);

    // Team 2 (綠): Z 軸正方向 (右上角附近)
    glm::vec3 spawnPointTeam2 = glm::vec3(30, 25.0f, 30.0f);
    glm::vec3 landingPointTeam2 = glm::vec3(30, 0.5f, 30.0f);

    Level() {}

    ~Level() {
        CleanUp();
    }

    void Load() {
        // 1. 載入材質
        floorTex = std::make_shared<Texture>();
        floorTex->Load("assets/textures/floor.jpg");

        wallTex = std::make_shared<Texture>();
        wallTex->Load("assets/textures/wall.jpg");

        // 2. 建立地板
        floor = new FloorMesh(mapSize, mapSize);
        // Tiling 設為 mapSize / 5.0，讓紋理密度適中
        floor->GetComponent<MeshRenderer>()->SetTexture(floorTex, mapSize / 5.0f);

        // 3. 建立四周圍牆 (根據 mapSize 自動計算)
        float halfSize = mapSize / 2.0f;
        float wallHeight = 5.0f;
        float wallThick = 1.0f;
        float offset = halfSize + (wallThick / 2.0f);

        struct WallConfig { glm::vec3 pos; glm::vec3 scale; };
        std::vector<WallConfig> wallConfigs = {
            {{ 0.0f, wallHeight / 2, -offset}, {mapSize, wallHeight, wallThick}}, // 北
            {{ 0.0f, wallHeight / 2,  offset}, {mapSize, wallHeight, wallThick}}, // 南
            {{-offset, wallHeight / 2,  0.0f}, {wallThick, wallHeight, mapSize}}, // 西
            {{ offset, wallHeight / 2,  0.0f}, {wallThick, wallHeight, mapSize}}  // 東
        };

        for (const auto& w : wallConfigs) {
            Entity* wall = new Entity("Wall");
            wall->transform->position = w.pos;
            wall->transform->scale = w.scale;

            float tilingX = w.scale.x / 5.0f;
            float tilingY = w.scale.y / 5.0f;
            wall->AddComponent<MeshRenderer>("Cube", glm::vec3(1.0f));
            wall->GetComponent<MeshRenderer>()->SetTexture(wallTex, tilingX);
            walls.push_back(wall);
        }

        // 4. 建立障礙物 (使用新的 Helper 函式)
        // 中央高台 (寬6, 高2, 深6)
        CreateBox(glm::vec3(0, 1.0f, 0), glm::vec3(6, 2, 6));

        // 四個角落的掩體 (寬3, 高3, 深3)
        CreateBox(glm::vec3(15, 1.5f, 15), glm::vec3(3, 3, 3));
        CreateBox(glm::vec3(-15, 1.5f, -15), glm::vec3(3, 3, 3));
        CreateBox(glm::vec3(15, 1.5f, -15), glm::vec3(3, 3, 3));
        CreateBox(glm::vec3(-15, 1.5f, 15), glm::vec3(3, 3, 3));

        // 階梯模擬 (讓玩家能跳上中央高台)
        // 左側階梯
        CreateBox(glm::vec3(-5, 0.5f, 0), glm::vec3(4, 1.0f, 4)); // 第一階
        // 右側階梯
        CreateBox(glm::vec3(5, 0.5f, 0), glm::vec3(4, 1.0f, 4));

        // 炸彈生成點
        // 1. 中央高台正上方
        itemSpawnPoints.push_back(glm::vec3(0, 2.5f, 0));

        // 2. 四個角落掩體附近
        itemSpawnPoints.push_back(glm::vec3(12, 1.5f, 12));
        itemSpawnPoints.push_back(glm::vec3(-12, 1.5f, -12));
        itemSpawnPoints.push_back(glm::vec3(12, 1.5f, -12));
        itemSpawnPoints.push_back(glm::vec3(-12, 1.5f, 12));
    }

    void Render(Shader& shader) {
        // 設定地圖大小參數 (給墨水 Shader 用，確保投影正確)
        shader.SetFloat("mapSize", mapSize);

        // 1. 畫地板 (開啟墨水)
        shader.SetInt("useInk", 1);
        if (floor) {
            floor->Draw(shader);
        }

        // 2. 畫障礙物 (開啟墨水，這樣才能塗在箱子上)
        shader.SetInt("useInk", 1);
        for (auto o : obstacles) {
            o->Draw(shader);
        }

        // 3. 畫牆壁 (關閉墨水，保持乾淨邊界)
        shader.SetInt("useInk", 0);
        for (auto w : walls) {
            w->Draw(shader);
        }
    }

    // [修改] 高度查詢 (使用 AABB)
    float GetHeightAt(float x, float z) {
        float currentHeight = 0.0f; // 預設地板高度

        // 遍歷所有碰撞盒
        for (const auto& box : colliders) {
            // AABB 範圍檢查 (x, z)
            if (x >= box.min.x && x <= box.max.x &&
                z >= box.min.z && z <= box.max.z) {

                // 如果在範圍內，高度就是箱子頂部 (box.max.y)
                // 取最高的那個 (處理重疊)
                if (box.max.y > currentHeight) {
                    currentHeight = box.max.y;
                }
            }
        }
        return currentHeight;
    }

    void CleanUp() {
        if (floor) delete floor;
        for (auto w : walls) delete w;
        for (auto o : obstacles) delete o;
        walls.clear();
        obstacles.clear();
        colliders.clear();
    }

private:
    // 同時建立視覺物件與物理碰撞盒
    void CreateBox(glm::vec3 centerPos, glm::vec3 scale) {
        // 1. 視覺物件
        Entity* box = new Entity("Box");
        box->transform->position = centerPos;
        box->transform->scale = scale;
        box->AddComponent<MeshRenderer>("Cube", glm::vec3(0.6f, 0.6f, 0.6f)); // 灰色

        // 套用牆壁材質
        if (wallTex) {
            box->GetComponent<MeshRenderer>()->SetTexture(wallTex, 1.0f);
        }
        obstacles.push_back(box);

        // 2. 物理碰撞盒 (AABB)
        // 假設 Cube Mesh 的原始大小是 1x1x1 (範圍 -0.5 ~ 0.5)
        // 經過 scale 縮放後，範圍是 center +/- scale/2
        glm::vec3 halfSize = scale * 0.5f;
        AABB aabb;
        aabb.min = centerPos - halfSize;
        aabb.max = centerPos + halfSize;

        colliders.push_back(aabb);
    }
};