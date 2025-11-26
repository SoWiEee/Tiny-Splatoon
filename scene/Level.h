#pragma once
#include <vector>
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

    void Load() {
        floorTex = std::make_shared<Texture>();
        floorTex->Load("assets/textures/floor.jpg"); // 找一張灰色的柏油路或磁磚圖

        wallTex = std::make_shared<Texture>();
        wallTex->Load("assets/textures/wall.jpg");   // 找一張磚牆圖

        floor = new FloorMesh(40.0f, 40.0f);
        floor->GetComponent<MeshRenderer>()->SetTexture(floorTex, 10.0f);

        struct WallConfig { glm::vec3 pos; glm::vec3 scale; };
        std::vector<WallConfig> wallConfigs = {
            {{ 0.0f, 2.5f, -20.5f}, {40.0f, 5.0f, 1.0f}}, // 北
            {{ 0.0f, 2.5f,  20.5f}, {40.0f, 5.0f, 1.0f}}, // 南
            {{-20.5f, 2.5f,  0.0f}, {1.0f, 5.0f, 40.0f}}, // 西
            {{ 20.5f, 2.5f,  0.0f}, {1.0f, 5.0f, 40.0f}}  // 東
        };

        for (const auto& w : wallConfigs) {
            Entity* wall = new Entity("Wall");
            wall->transform->position = w.pos;
            wall->transform->scale = w.scale;

            float tilingX = w.scale.x / 5.0f; // 每 5 米重複一次
            float tilingY = w.scale.y / 5.0f;
            wall->AddComponent<MeshRenderer>("Cube", glm::vec3(1.0f));
            wall->GetComponent<MeshRenderer>()->SetTexture(wallTex, tilingX);
            walls.push_back(wall);
        }

        Entity* box = new Entity("Box");
        box->transform->position = glm::vec3(5.0f, 1.0f, 5.0f);
        box->AddComponent<MeshRenderer>("Cube", glm::vec3(1.0f, 0.5f, 0.2f));
        obstacles.push_back(box);
    }
};