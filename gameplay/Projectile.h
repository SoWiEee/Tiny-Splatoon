#pragma once
#include "../scene/Entity.h"
#include "../splat/SplatPhysics.h"

class Projectile : public Entity {
public:
    glm::vec3 velocity;
    float gravity = 15.0f;
    int ownerTeam;
    glm::vec3 inkColor;
    bool isDead = false;

    // 用來回傳撞擊資訊的 Callback 或旗標
    bool hasHitFloor = false;
    glm::vec3 hitPosition;

    Projectile(glm::vec3 startVel, glm::vec3 color, int team)
        : Entity("Projectile"), velocity(startVel), inkColor(color), ownerTeam(team) {
        // 加入 MeshRenderer (可視化)
        AddComponent<MeshRenderer>("Cube", color);
    }

    void UpdatePhysics(float dt) { // 不叫 Update，改由 GameWorld 統一呼叫
        if (isDead) return;

        // 物理運動 (移植 InkProjectile::Update)
        velocity.y -= gravity * dt;
        transform->position += velocity * dt;
        transform->rotation.x += 720.0f * dt;

        // 碰撞檢查 (只負責偵測，不負責畫圖)
        if (transform->position.y <= 0.0f) {
            // 校正位置
            float timeOvershoot = 0.0f;
            if (abs(velocity.y) > 0.001f) {
                timeOvershoot = transform->position.y / velocity.y;
            }
            transform->position -= velocity * timeOvershoot;
            transform->position.y = 0.0f;

            // 設定狀態，讓 GameWorld 知道要處理塗地了
            hasHitFloor = true;
            hitPosition = transform->position;
            isDead = true;
        }
    }
};