#pragma once
#include "../engine/Component.h"
#include "../engine/GameObject.h"
#include "../graphics/InkMap.h"
#include <glm/glm.hpp>
#include <cstdlib> 
#include <cmath> 

class InkProjectile : public Component {
public:
    glm::vec3 velocity;
    float gravity = 9.8f;

    InkMap* inkMap;             // 畫布 (FBO) 的指標
    unsigned int brushTex;      // 筆刷貼圖 ID
    glm::vec3 inkColor;         // 墨水顏色 (紅隊或綠隊)
    bool isDead = false;        // 標記是否該被刪除

    float floorSize = 40.0f;

    InkProjectile(glm::vec3 startVel, glm::vec3 color, InkMap* map, unsigned int tex)
        : velocity(startVel), inkColor(color), inkMap(map), brushTex(tex) {
    }

    void Update(float dt) override {
        if (isDead) return;

        velocity.y -= gravity * dt;
        gameObject->transform->position += velocity * dt;
        gameObject->transform->rotation.x += 720.0f * dt;
        gameObject->transform->rotation.z += 360.0f * dt;

        // 碰撞偵測
        if (gameObject->transform->position.y <= 0.0f) {
            float timeOvershoot = 0.0f;
            if (abs(velocity.y) > 0.001f) {
                timeOvershoot = gameObject->transform->position.y / velocity.y;
            }

            glm::vec3 correctedPos = gameObject->transform->position;
            correctedPos.x -= velocity.x * timeOvershoot;
            correctedPos.z -= velocity.z * timeOvershoot;
            correctedPos.y = 0.0f;

            // 更新物件位置到正確的撞擊點 (這樣視覺上也會剛好停在地板)
            gameObject->transform->position = correctedPos;

            HitFloor();
        }

        if (gameObject->transform->position.y < -10.0f) {
            isDead = true;
        }
    }

private:
    void HitFloor() {
        float x = gameObject->transform->position.x;
        float z = gameObject->transform->position.z;

        float halfSize = floorSize / 2.0f;

        if (x >= -halfSize && x <= halfSize &&
            z >= -halfSize && z <= halfSize) {

            // 世界座標 -> 紋理座標
            float u = (x + halfSize) / floorSize;
            float v = 1.0f - ((z + halfSize) / floorSize);

            float randomSize = 0.06f + static_cast<float>(rand()) / (static_cast<float>(RAND_MAX / 0.06f));
            float randomAngle = static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 6.283185f;

            // GPU 畫圖
            inkMap->Paint(glm::vec2(u, v), randomSize, inkColor, brushTex, randomAngle);
            // CPU 記錄
            int teamID = (inkColor.x > 0.5f) ? 1 : 2;
            inkMap->UpdateMapData(u, v, teamID);
        }
        isDead = true;
    }
};