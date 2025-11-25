#pragma once
#include "Entity.h"
#include "../components/MeshRenderer.h"

class FloorMesh : public Entity {
public:
    float width;
    float depth;

    FloorMesh(float w, float d) : Entity("Floor"), width(w), depth(d) {
        // 設定 Transform
        transform->position = glm::vec3(0, 0, 0);
        transform->scale = glm::vec3(width, 1.0f, depth);

        // 掛載渲染組件 (原本 main.cpp 的邏輯)
        AddComponent<MeshRenderer>("Plane", glm::vec3(0.8f));
    }

    // 提供給物理系統查詢邊界用
    bool IsPointOnFloor(const glm::vec3& point) const {
        float halfW = width / 2.0f;
        float halfD = depth / 2.0f;
        return (point.x >= -halfW && point.x <= halfW &&
            point.z >= -halfD && point.z <= halfD);
    }
};