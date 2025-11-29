#pragma once
#include "../scene/Entity.h"
#include "../components/MeshRenderer.h"
#include <glm/glm.hpp>
#include <cstdlib>

class Projectile : public Entity {
public:
    // --- 物理屬性 ---
    glm::vec3 velocity;
    float gravity = 15.0f;

    int ownerTeam;
    glm::vec3 inkColor;
    bool isDead = false;

    bool hasHitFloor = false;
    glm::vec3 hitPosition;

    Projectile(glm::vec3 startVel, glm::vec3 color, int team, float scale)
        : Entity("Projectile"), velocity(startVel), inkColor(color), ownerTeam(team)
    {
        transform->scale = glm::vec3(scale);
        AddComponent<MeshRenderer>("Sphere", color);
    }

    void UpdatePhysics(float dt) {
        if (isDead) return;

        velocity.y -= gravity * dt;
        transform->position += velocity * dt;
        transform->rotation.x += 720.0f * dt;
        transform->rotation.z += 360.0f * dt;

        UpdateVisualDeformation();

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

    void UpdateVisualDeformation() {
        // 1. 計算速度
        float speed = glm::length(velocity);

        // 2. 旋轉：讓子彈永遠「面朝」飛行方向
        if (speed > 0.1f) {
            glm::vec3 direction = glm::normalize(velocity);
            // 或是更簡單的：我們直接改 transform 的 Model Matrix 建構方式
            // 但如果你的 Transform 類別只存 vec3 rotation，我們用 LookAt 算出 Euler Angles
            // 為了簡化，我們先只做「拉長」，旋轉交給下一幀的 Shader 或 Render 處理

            // 這裡假設 GameObject 有 LookAt 方法，或者我們手動設定
            transform->LookAt(transform->position + velocity);
        }

        // 3. 縮放：速度越快，Z 軸(前進方向)越長，XY 軸(寬度)越窄
        // 基礎大小
        float baseScale = 0.3f;

        // 拉伸係數 (可調整)
        float stretchFactor = 1.0f + (speed * 0.1f);
        // 擠壓係數 (體積保持)
        float squashFactor = 1.0f / sqrt(stretchFactor);

        transform->scale = glm::vec3(baseScale * squashFactor, baseScale * squashFactor, baseScale * stretchFactor);
    }
};