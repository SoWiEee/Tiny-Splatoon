#pragma once
#include "Projectile.h"
#include "../engine/audio/AudioManager.h"

class Bomb : public Projectile {
public:
    float fuseTimer = 2.0f; // 引信時間 (秒)
    float explosionRadius = 4.0f; // 爆炸半徑 (塗地大小)
    bool isExploded = false;

    Bomb(glm::vec3 pos, glm::vec3 vel, int team, glm::vec3 col, int owner)
        : Projectile(glm::vec3(0), col, team, 0.5f)
    {
        transform->position = pos;
        velocity = vel;
        gravity = 30.0f;
        this->ownerId = owner;
    }

    // 覆寫物理更新，讓它會反彈而不是穿透
    void UpdatePhysics(float dt) {
        if (isExploded) return;

        // 1. 重力與移動
        velocity.y -= gravity * dt;
        transform->position += velocity * dt;

        // 2. 簡易地面碰撞 (反彈邏輯)
        // 假設地板高度是 0
        if (transform->position.y < 0.2f) { // 考慮半徑
            transform->position.y = 0.2f; // 修正位置

            // 反彈：Y軸速度反轉，並衰減 (Damping)
            if (abs(velocity.y) > 2.0f) {
                velocity.y = -velocity.y * 0.6f; // 彈力係數 0.6
                velocity.x *= 0.8f; // 地面摩擦力
                velocity.z *= 0.8f;

                // 播放撞擊音效 (可選)
                // AudioManager::Instance().PlayOneShot("bounce", 0.3f);
            }
            else {
                // 速度太慢就停止彈跳，在地上滾
                velocity.y = 0;
                velocity.x *= 0.95f; // 滾動摩擦
                velocity.z *= 0.95f;
            }
        }

        // 3. 引信計時
        fuseTimer -= dt;
        if (fuseTimer <= 0.0f) {
            isExploded = true; // 標記為已爆炸，通知 GameWorld 處理後續
        }

        // 視覺旋轉 (隨便轉)
        transform->rotation.x += 200.0f * dt;
        transform->rotation.y += 200.0f * dt;
    }
};