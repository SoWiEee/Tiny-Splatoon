#pragma once
#include <glm/glm.hpp>
#include "../scene/FloorMesh.h"

class SplatPhysics {
public:
    struct HitResult {
        bool hit;
        glm::vec2 uv;
    };

    // 將世界座標轉換為地板 UV
    static HitResult WorldToUV(const glm::vec3& worldPos, const FloorMesh* floor) {
        if (!floor->IsPointOnFloor(worldPos)) {
            return { false, glm::vec2(0) };
        }

        float u = (worldPos.x + floor->width / 2.0f) / floor->width;
        float v = 1.0f - ((worldPos.z + floor->depth / 2.0f) / floor->depth);

        return { true, glm::vec2(u, v) };
    }
};