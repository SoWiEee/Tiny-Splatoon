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

        // 保存上一幀的位置 (選擇性，用速度回推比較簡單)

        // 1. 物理運動
        velocity.y -= gravity * dt;
        gameObject->transform->position += velocity * dt;

        // 2. 旋轉視覺
        gameObject->transform->rotation.x += 720.0f * dt;
        gameObject->transform->rotation.z += 360.0f * dt;

        // 3. [修正] 精確碰撞偵測
        if (gameObject->transform->position.y <= 0.0f) {
            // 我們現在在地底下 (y < 0)，需要回推到 y = 0 的時刻

            // 計算我們「多跑了」多少時間
            // 公式: time_overshoot = current_y / current_velocity_y
            // 因為 y 和 vel.y 都是負的，所以結果是正的時間
            float timeOvershoot = 0.0f;
            if (abs(velocity.y) > 0.001f) {
                timeOvershoot = gameObject->transform->position.y / velocity.y;
            }

            // 回推 X 和 Z 座標
            glm::vec3 correctedPos = gameObject->transform->position;
            correctedPos.x -= velocity.x * timeOvershoot;
            correctedPos.z -= velocity.z * timeOvershoot;
            correctedPos.y = 0.0f; // 強制設為地板高度

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
            float v = 1.0f - ((z + halfSize) / floorSize);


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