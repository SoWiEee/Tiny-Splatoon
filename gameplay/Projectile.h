#pragma once
#include "../scene/Entity.h"
#include "../components/MeshRenderer.h"
#include <glm/glm.hpp>

class Projectile : public Entity {
public:
    // --- 物理屬性 ---
    glm::vec3 velocity;
    float gravity = 15.0f;

    int ownerTeam;
    glm::vec3 inkColor;
    bool isDead = false;

    // --- 撞擊事件資料 (傳給 GameWorld 用) ---
    bool hasHitFloor = false;
    glm::vec3 hitPosition;

    Projectile(glm::vec3 startVel, glm::vec3 color, int team)
        : Entity("Projectile"), velocity(startVel), inkColor(color), ownerTeam(team)
    {
        // 加入視覺組件 (子彈是一顆小方塊)
        transform->scale = glm::vec3(0.15f);
        AddComponent<MeshRenderer>("Cube", color);
    }

    void UpdatePhysics(float dt) {
        if (isDead) return;

        velocity.y -= gravity * dt;
        transform->position += velocity * dt;
        transform->rotation.x += 720.0f * dt;
        transform->rotation.z += 360.0f * dt;

        if (transform->position.y <= 0.0f) {
            float timeOvershoot = 0.0f;
            if (abs(velocity.y) > 0.001f) {
                timeOvershoot = transform->position.y / velocity.y;
            }

            transform->position -= velocity * timeOvershoot;
            transform->position.y = 0.0f;

            hasHitFloor = true;
            hitPosition = transform->position;

            isDead = true;
        }

        if (transform->position.y < -10.0f) {
            isDead = true;
        }
    }
};