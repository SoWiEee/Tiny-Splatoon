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

    float floorSize = 20.0f;

    // 建構子：初始化速度、顏色與畫布
    InkProjectile(glm::vec3 startVel, glm::vec3 color, InkMap* map, unsigned int tex)
        : velocity(startVel), inkColor(color), inkMap(map), brushTex(tex) {
    }

    void Update(float dt) override {
        if (isDead) return;

        // 1. 物理運動計算
        // v = v0 + at (重力向下)
        velocity.y -= gravity * dt;

        // p = p0 + vt
        gameObject->transform->position += velocity * dt;

        // 2. 視覺效果：讓子彈在空中瘋狂旋轉
        gameObject->transform->rotation.x += 720.0f * dt; // 每秒轉兩圈
        gameObject->transform->rotation.z += 360.0f * dt;

        // 3. 碰撞偵測 (假設地板高度為 y=0)
        // 如果穿過地板，就觸發碰撞
        if (gameObject->transform->position.y <= 0.0f) {
            gameObject->transform->position.y = 0.0f;
            HitFloor();
        }

        // 4. 安全邊界檢查
        if (gameObject->transform->position.y < -10.0f) {
            isDead = true;
        }
    }

private:
    void HitFloor() {
        // 1. 取得擊中點的世界座標
        float x = gameObject->transform->position.x;
        float z = gameObject->transform->position.z;

        // 2. 檢查是否落在地板範圍內
        // 地板範圍是 [-10, 10] (因為 Size=20)
        float halfSize = floorSize / 2.0f;

        if (x >= -halfSize && x <= halfSize &&
            z >= -halfSize && z <= halfSize) {

            // 3. 座標轉換：世界座標 (World) -> 紋理座標 (UV)
            // map x from [-10, 10] to [0, 1]
            float u = (x + halfSize) / floorSize;
            float v = (z + halfSize) / floorSize;


            // 隨機大小：在 0.06 到 0.12 之間浮動
            float randomSize = 0.06f + static_cast<float>(rand()) / (static_cast<float>(RAND_MAX / 0.06f));

            // 隨機旋轉角度
            float randomAngle = static_cast<float>(rand()) / static_cast<float>(RAND_MAX) * 6.283185f;

            // 5. 呼叫 InkMap 進行繪製
            inkMap->Paint(glm::vec2(u, v), randomSize, inkColor, brushTex, randomAngle);
        }

        isDead = true;
    }
};