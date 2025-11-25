#pragma once
#include <vector>
#include "Entity.h"
#include "FloorMesh.h"

class Level {
public:
    FloorMesh* floor;
    std::vector<Entity*> walls;
    std::vector<Entity*> obstacles;

    void Load() {
        floor = new FloorMesh(40.0f, 40.0f);

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
            wall->AddComponent<MeshRenderer>("Cube", glm::vec3(0.3f, 0.3f, 0.3f)); // 深灰色
            walls.push_back(wall);
        }

        Entity* box = new Entity("Box");
        box->transform->position = glm::vec3(5.0f, 1.0f, 5.0f);
        box->AddComponent<MeshRenderer>("Cube", glm::vec3(1.0f, 0.5f, 0.2f));
        obstacles.push_back(box);
    }
};