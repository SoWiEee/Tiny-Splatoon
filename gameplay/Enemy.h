#pragma once
#include "../scene/Entity.h"
#include "Weapon.h"
#include "../components/MeshRenderer.h"
#include <glm/glm.hpp>
#include <cstdlib>

class Enemy : public Entity {
public:
    // --- 屬性 ---
    int teamID;
    float moveSpeed = 3.0f;
    float mapLimit = 18.0f;

    // --- AI 狀態 ---
    float changeDirTime = 2.0f;
    float timer = 0.0f;
    glm::vec3 currentDir = glm::vec3(0, 0, 1);

    // reference
    Weapon weapon;
    GameObject* visualBody;

    Enemy(glm::vec3 startPos, int team)
        : Entity("Enemy"), teamID(team),
        weapon(team, glm::vec3(0, 1, 0)) // 預設綠色
    {
        transform->position = startPos;

        visualBody = new GameObject("EnemyBody");
        visualBody->AddComponent<MeshRenderer>("Cube", weapon.inkColor);

        RandomizeDir();
    }

    void UpdateLogic(float dt) {
        // 1. AI 移動邏輯 (隨機亂走)
        timer += dt;
        if (timer > changeDirTime) {
            RandomizeDir();
            timer = 0.0f;
            changeDirTime = 1.0f + (float)(rand() % 20) / 10.0f;
        }

        // 移動
        transform->position += currentDir * moveSpeed * dt;

        // 轉向
        if (glm::length(currentDir) > 0.01f) {
            // 簡單轉向 (看向移動方向)
            float angle = atan2(currentDir.x, currentDir.z);
            transform->rotation.y = glm::degrees(angle);
        }

        // 邊界檢查
        CheckBounds(dt);

        // 更新視覺位置
        if (visualBody) {
            visualBody->transform->position = transform->position + glm::vec3(0, 0.9f, 0);
            visualBody->transform->rotation = transform->rotation;
        }

        // 2. AI 射擊邏輯 (自動開火)
        // 槍口位置
        glm::vec3 gunPos = transform->position + glm::vec3(0, 1.5f, 0) + currentDir * 0.8f;

        // 稍微隨機的射擊方向
        float spread = ((rand() % 100) / 100.0f - 0.5f) * 0.5f;
        glm::vec3 aimDir = glm::normalize(currentDir + glm::vec3(spread, -0.2f, 0.0f));

        // 自動扣扳機 (true)
        weapon.Trigger(dt, gunPos, aimDir, true);
    }

    GameObject* GetVisualBody() { return visualBody; }

private:
    void RandomizeDir() {
        float x = (float)(rand() % 100) - 50;
        float z = (float)(rand() % 100) - 50;
        currentDir = glm::normalize(glm::vec3(x, 0, z));
    }

    void CheckBounds(float dt) {
        glm::vec3& pos = transform->position;
        bool hit = false;
        if (pos.x > mapLimit) { pos.x = mapLimit; hit = true; }
        if (pos.x < -mapLimit) { pos.x = -mapLimit; hit = true; }
        if (pos.z > mapLimit) { pos.z = mapLimit; hit = true; }
        if (pos.z < -mapLimit) { pos.z = -mapLimit; hit = true; }

        if (hit) {
            pos -= currentDir * moveSpeed * dt; // 退後
            RandomizeDir(); // 換方向
        }
    }
};