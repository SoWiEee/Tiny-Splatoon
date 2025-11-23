#pragma once
#include "../engine/Component.h"
#include "../engine/GameObject.h"
#include "../graphics/InkMap.h"
#include <glm/glm.hpp>
#include <cstdlib>

class InkProjectile : public Component {
public:
    glm::vec3 velocity;         // 飛行速度
    float gravity = 9.8f;       // 重力加速度
    float radius = 0.2f;        // 子彈半徑 (用於碰撞檢測)

    InkMap* inkMap;             // 畫布的參考
    unsigned int brushTex;      // 筆刷貼圖
    bool isDead = false;        // 標記是否該被刪除

    InkProjectile(glm::vec3 startVel, InkMap* map, unsigned int tex)
        : velocity(startVel), inkMap(map), brushTex(tex) {
    }

    void Update(float dt) override {
        if (isDead) return;

        // 1. 物理運動
        // v = v0 + at
        velocity.y -= gravity * dt;
        // p = p0 + vt
        gameObject->transform->position += velocity * dt;

        // 2. 旋轉子彈 (讓它飛的時候看起來在轉)
        gameObject->transform->rotation.x += 360.0f * dt;

        // 3. 地板碰撞檢測
        // 簡單起見，我們假設地板就在 y = 0
        if (gameObject->transform->position.y <= 0.0f) {
            HitFloor();
        }

        if (gameObject->transform->position.y < -10.0f) {
            isDead = true;
        }
    }

private:
    void HitFloor() {
        // 1. 計算 UV
        // 假設地板大小是 20x20，中心在 (0,0)
        float floorSize = 20.0f;
        float x = gameObject->transform->position.x;
        float z = gameObject->transform->position.z;

        // 檢查是否在地板範圍內
        if (x >= -floorSize / 2 && x <= floorSize / 2 &&
            z >= -floorSize / 2 && z <= floorSize / 2) {

            float u = (x + floorSize / 2) / floorSize;
            float v = (z + floorSize / 2) / floorSize;

            // 2. 畫墨水
            // 隨機調整筆刷大小 (0.05 ~ 0.08)，讓墨漬大小不一
            float randomSize = 0.05f + static_cast<float>(rand()) / (static_cast<float>(RAND_MAX / 0.03f));

            // TODO: 我們需要在 InkMap::Paint 加入「旋轉」參數，這裡先傳預設
            // 這裡用紅色，之後可以改成參數傳入
            inkMap->Paint(glm::vec2(u, v), randomSize, glm::vec3(1.0f, 0.0f, 0.0f), brushTex);
        }

        isDead = true;
    }
};