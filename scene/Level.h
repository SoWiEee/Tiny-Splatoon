#pragma once
#include <vector>
#include <iostream>
#include "Entity.h"
#include "FloorMesh.h"
#include "../engine/rendering/Texture.h"

class Level {
public:
    FloorMesh* floor;
    std::vector<Entity*> walls;
    std::vector<Entity*> obstacles;
    std::shared_ptr<Texture> floorTex;
    std::shared_ptr<Texture> wallTex;

    float mapSize = 80.0f;

    // [新增] 重生點 (高空) 與 落地點 (基地)
    // Team 1 (紅): Z 軸負方向
    glm::vec3 spawnPointTeam1 = glm::vec3(0, 25.0f, -40.0f);
    glm::vec3 landingPointTeam1 = glm::vec3(0, 0.5f, -40.0f);

    // Team 2 (綠): Z 軸正方向
    glm::vec3 spawnPointTeam2 = glm::vec3(0, 25.0f, 40.0f);
    glm::vec3 landingPointTeam2 = glm::vec3(0, 0.5f, 40.0f);

    void Load() {
        // 1. 載入材質
        floorTex = std::make_shared<Texture>();
        // 建議找一張灰色的柏油路圖，比較容易看清楚墨水顏色
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

        // 4. 建立障礙物 (簡單的掩體)
        // 在地圖中間放幾個箱子
        CreateBox(glm::vec3(10, 1.5f, 10), glm::vec3(3, 3, 3));
        CreateBox(glm::vec3(-10, 1.5f, -10), glm::vec3(3, 3, 3));
        CreateBox(glm::vec3(10, 1.5f, -10), glm::vec3(3, 3, 3));
        CreateBox(glm::vec3(-10, 1.5f, 10), glm::vec3(3, 3, 3));

        // 中央高台
        CreateBox(glm::vec3(0, 1.0f, 0), glm::vec3(6, 2, 6));
    }

    // [新增] 渲染函式 (統一管理渲染，方便傳入 mapSize 給 Shader)
    void Render(Shader& shader) {
        // 設定地圖大小參數 (給墨水 Shader 用)
        shader.SetFloat("mapSize", mapSize);

        // 1. 畫地板
        if (floor) {
            // FloorMesh 內部會處理 Draw
            // 如果 FloorMesh::Draw 只是綁定 VAO 畫圖，這裡可能需要先綁定材質
            // 假設 FloorMesh 是一個 Entity 或有 Draw 方法
            floor->Draw(shader);
        }

        // 2. 畫牆壁 (牆壁通常不顯示墨水，可以關掉 useInk)
        shader.SetInt("useInk", 0);
        for (auto w : walls) {
            w->Draw(shader);
        }

        // 3. 畫障礙物 (障礙物要顯示墨水，開啟 useInk)
        shader.SetInt("useInk", 1);
        for (auto o : obstacles) {
            o->Draw(shader);
        }
    }

    // [新增] 簡單的高度查詢 (為了配合 Player 的物理)
    float GetHeightAt(float x, float z) {
        // 因為是平地版本，大部分回傳 0
        // 如果要讓 Player 站上箱子，可以在這裡簡單遍歷 obstacles

        for (auto box : obstacles) {
            glm::vec3 pos = box->transform->position;
            glm::vec3 scale = box->transform->scale;

            // AABB 檢查
            float halfW = scale.x / 2.0f;
            float halfD = scale.z / 2.0f;

            if (x >= pos.x - halfW && x <= pos.x + halfW &&
                z >= pos.z - halfD && z <= pos.z + halfD) {
                // 如果在箱子範圍內，回傳箱子頂部高度 (pos.y + halfHeight)
                // Cube 模型高度是 2? 還是 1? 假設 Cube 原點在中心，高度範圍 -1~1
                // 這裡假設我們縮放後的實際高度是 scale.y
                // 箱子頂部 = pos.y + (scale.y / 2.0f) * 1.0f (假設模型高度為1) 或 *2.0f (假設模型高度為2)
                // 根據你之前的 Entity Cube，通常是高度 2 (-1~1)
                // 所以頂部 = pos.y + scale.y; (如果 scale=1, pos=0, 頂部=1)
                return pos.y + scale.y;
            }
        }

        return 0.0f; // 地板高度
    }

    void CleanUp() {
        if (floor) delete floor;
        for (auto w : walls) delete w;
        for (auto o : obstacles) delete o;
        walls.clear();
        obstacles.clear();
    }

private:
    void CreateBox(glm::vec3 pos, glm::vec3 scale) {
        Entity* box = new Entity("Box");
        box->transform->position = pos;
        box->transform->scale = scale;
        box->AddComponent<MeshRenderer>("Cube", glm::vec3(0.6f, 0.6f, 0.6f)); // 灰色箱子

        // 讓箱子也用牆壁材質
        if (wallTex) box->GetComponent<MeshRenderer>()->SetTexture(wallTex, 1.0f);

        obstacles.push_back(box);
    }
};