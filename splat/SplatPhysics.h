#pragma once
#include <glm/glm.hpp>
#include "../scene/FloorMesh.h"

class SplatPhysics {
public:
    struct HitResult {
        bool hit;
        glm::vec2 uv;
    };

    static HitResult WorldToUV(const glm::vec3& worldPos, const glm::vec3& floorPos, float width, float depth) {
        // 1. 計算相對於地板中心的偏移
        float dx = worldPos.x - floorPos.x;
        float dz = worldPos.z - floorPos.z;

        // 2. 邊界檢查 (AABB)
        float halfW = width / 2.0f;
        float halfD = depth / 2.0f;

        if (dx < -halfW || dx > halfW || dz < -halfD || dz > halfD) {
            return { false, glm::vec2(0.0f) };
        }

        float u = (dx + halfW) / width;
        float v = 1.0f - ((dz + halfD) / depth);

        return { true, glm::vec2(u, v) };
    }
};