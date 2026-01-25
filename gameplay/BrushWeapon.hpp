#pragma once
#include "Weapon.hpp"
#include <glm/gtx/rotate_vector.hpp> 

class BrushWeapon : public Weapon {
private:
    bool swingRight = true; // 當前揮動方向

public:
    // 射速極快，單發耗墨中等
    BrushWeapon(int team, glm::vec3 color)
        : Weapon(team, color, 0.2f, 0.15f) {
    }

protected:
    void FireLogic(glm::vec3 pos, glm::vec3 dir) override {

        swingRight = !swingRight;   // 切換揮動方向
        float swingBias = swingRight ? -15.0f : 15.0f;  // 揮動的中心偏差
        int blobCount = 12;     // 一次灑出多顆墨水

        // 定義扇形的總寬度
        float fanWidth = 60.0f;
        float startAngle = -fanWidth / 2.0f; // -30度
        float stepAngle = fanWidth / (blobCount - 1); // 每顆間隔

        // 定義旋轉軸 (預設是 Y 軸，即水平左右擴散)
        glm::vec3 upAxis(0, 1, 0);

        for (int i = 0; i < blobCount; i++) {
            SpawnInfo info;
            info.pos = pos;
            info.color = inkColor;
            info.team = teamID;

            // --- 1. 計算角度 ---
            // 基礎扇形角度 + 揮動偏差 + 一點點隨機擾動
            float currentAngle = startAngle + (stepAngle * i);
            float finalAngle = currentAngle + swingBias + RandomFloat(-5.0f, 5.0f);

            // --- 2. 旋轉向量 ---
            // 將瞄準方向 dir 繞著 Y 軸旋轉 finalAngle 度
            glm::vec3 spreadDir = glm::rotate(dir, glm::radians(finalAngle), upAxis);

            // 筆刷會稍微往上噴一點點，增加拋物線感
            spreadDir.y += 0.1f;
            info.dir = glm::normalize(spreadDir);

            // --- 3. 速度與距離 ---
            // 筆刷射程短，所以速度慢 (15.0)
            // 加上隨機速度，讓墨水落地點前後不一 (12 ~ 18)
            info.speed = RandomFloat(12.0f, 18.0f);

            // --- 4. 大小 ---
            // 筆刷的墨水通常比較大顆且形狀不規則
            info.scale = 0.4f * RandomFloat(0.8f, 1.2f);

            pendingSpawns.push_back(info);
        }
    }
};