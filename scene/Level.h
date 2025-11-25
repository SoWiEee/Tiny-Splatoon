#pragma once
#include <vector>
#include "Entity.h"
#include "FloorMesh.h"

class Level {
public:
    FloorMesh* floor; // 主要地板
    std::vector<Entity*> walls;
    std::vector<Entity*> obstacles;

    void Load() {
        // 建立地板
        floor = new FloorMesh(40.0f, 40.0f);

        // 建立牆壁 (移植 main.cpp 的 walls vector)
        // ... (建立 Wall Entity 並加入 walls) ...
    }

    // 之後可擴充：GetCollisionObjects()
};