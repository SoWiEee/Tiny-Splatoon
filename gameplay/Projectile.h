#pragma once
#include "../scene/Entity.h"
#include "../components/MeshRenderer.h"
#include "../network/NetworkProtocol.h"
#include <glm/glm.hpp>
#include <cstdlib>
#include <cmath>

class Projectile : public Entity {
public:
    glm::vec3 velocity;
    float gravity = 30.0f;

    int ownerTeam;
    int ownerID;
    glm::vec3 inkColor;
    bool isDead = false;

    bool hasHitFloor = false;
    glm::vec3 hitPosition;

    ProjectileType pType;
    bool IsBomb() const { return pType == ProjectileType::BOMB; }

    float fuseTimer = 0.0f;     // 引信倒數
    bool hasExploded = false;   // 標記是否觸發爆炸
    bool warningPlayed = false;

    float lifeTime = 5.0f;

    Projectile(glm::vec3 startPos, glm::vec3 startVel, glm::vec3 color, int team, float scale, int owner, ProjectileType type = ProjectileType::BULLET)
        : Entity("Projectile"), velocity(startVel), inkColor(color), ownerTeam(team), ownerID(owner), pType(type)
    {
        transform->position = startPos;
        transform->scale = glm::vec3(scale);

        if (pType == ProjectileType::BOMB) {
            // --- 炸彈設定 ---
            gravity = 10.0f;
            fuseTimer = 2.0f;
            AddComponent<MeshRenderer>("Cube", glm::vec3(0.1f, 0.1f, 0.1f));

            // 比普通子彈大
            transform->scale = glm::vec3(1.0f, 1.0f, 1.0f);
        }
        else if (pType == ProjectileType::ROCKET) {
            gravity = 10.0f;   // 重力很小，飛比較直
            lifeTime = 4.0f;  // 飛久一點

            AddComponent<MeshRenderer>("Sphere", color);
            transform->scale = glm::vec3(1.0f, 1.0f, 1.0f);
        }
        else {
            // --- 普通子彈設定 ---
            gravity = 30.0f;
            lifeTime = 3.0f;
            AddComponent<MeshRenderer>("Sphere", color);
        }
    }

    void UpdatePhysics(float dt) {
        if (isDead) return;

        // 1. 壽命與引信管理
        if (pType == ProjectileType::BOMB) {
            fuseTimer -= dt;
            if (fuseTimer <= 0.0f) {
                isDead = true;
                hasExploded = true; // 時間到爆炸
                return;
            }
        }
        else {
            // BULLET 和 ROCKET 都是看 lifeTime
            lifeTime -= dt;
            if (lifeTime <= 0.0f) {
                isDead = true;
                return;
            }
        }

        // 2. 物理運動
        velocity.y -= gravity * dt;
        transform->position += velocity * dt;

        // 3. 視覺旋轉
        if (pType == ProjectileType::BOMB) {
            // 炸彈：亂轉
            transform->rotation.x += 720.0f * dt;
            transform->rotation.z += 360.0f * dt;
        }
        else if (pType == ProjectileType::ROCKET) {
            // [新增] 火箭：面朝飛行方向
            if (glm::length(velocity) > 0.1f) {
                transform->LookAt(transform->position + velocity);
            }
            // 也可以讓它自轉 (像鑽頭一樣)
            transform->rotation.z += 720.0f * dt;
        }
        else {
            // 子彈：拉伸變形
            UpdateVisualDeformation();
        }

        // 4. 地板碰撞偵測
        if (transform->position.y <= 0.2f) {
            if (velocity.y < 0) {
                if (pType == ProjectileType::BOMB) {
                    // 炸彈彈跳邏輯
                    velocity.y = -velocity.y * 1.0f;
                    velocity.x *= 0.6f;
                    velocity.z *= 0.6f;
                    transform->position.y = 0.2f;
                    if (std::abs(velocity.y) < 1.0f) velocity.y = 0.0f;

                    hasHitFloor = true;
                    hitPosition = transform->position;
                }
                else if (pType == ProjectileType::ROCKET) {
                    // 火箭撞地爆炸
                    hasHitFloor = true;
                    hitPosition = transform->position;

                    isDead = true;      // 銷毀物件
                    hasExploded = true; // 觸發爆炸 (塗地範圍大)
                }
                else {
                    // --- 普通子彈：撞地塗地 (保持原樣) ---
                    float timeOvershoot = 0.0f;
                    if (std::abs(velocity.y) > 0.001f) {
                        timeOvershoot = (transform->position.y - 0.0f) / velocity.y;
                    }
                    transform->position -= velocity * timeOvershoot;
                    transform->position.y = 0.0f;

                    hasHitFloor = true;
                    hitPosition = transform->position;
                    isDead = true;
                }
            }
        }

        if (transform->position.y < -10.0f) {
            isDead = true;
        }
    }

    void UpdateVisualDeformation() {
        if (pType != ProjectileType::BULLET) return; // 只有普通子彈用拉伸

        float speed = glm::length(velocity);
        if (speed > 0.1f) {
            transform->LookAt(transform->position + velocity);
        }

        float baseScale = transform->scale.x;
        float defaultScale = 0.3f;
        float stretchFactor = 1.0f + (speed * 0.05f);
        float squashFactor = 1.0f / sqrt(stretchFactor);

        transform->scale = glm::vec3(defaultScale * squashFactor, defaultScale * squashFactor, defaultScale * stretchFactor);
    }
};