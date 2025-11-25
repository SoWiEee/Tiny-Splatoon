#pragma once
#include "../engine/Component.h"
#include "../engine/GameObject.h"
#include "InkShooter.h"
#include <glm/glm.hpp>
#include <cstdlib>

class SimpleAI : public Component {
public:
    float moveSpeed = 4.0f;
    float changeDirTime = 2.0f;
    float timer = 0.0f;

    float shootTimer = 0.0f;
    float shootInterval = 0.3f; // AI 射快一點比較好玩

    glm::vec3 currentDir = glm::vec3(0, 0, 1);
    float mapLimit = 18.0f;

    InkShooter* shooter;

    void Setup(InkShooter* s) {
        shooter = s;
        RandomizeDir();
    }

    void Update(float dt) override {
        // --- 1. 移動邏輯 ---
        timer += dt;
        if (timer > changeDirTime) {
            RandomizeDir();
            timer = 0.0f;
            // 隨機決定下次換方向的時間 (0.5 ~ 2.5秒)
            changeDirTime = 0.5f + (float)(rand() % 20) / 10.0f;
        }

        // 移動
        gameObject->transform->position += currentDir * moveSpeed * dt;

        // 簡單的轉向 (面向移動方向)
        if (glm::length(currentDir) > 0.01f) {
            float angle = atan2(currentDir.x, currentDir.z);
            gameObject->transform->rotation.y = glm::degrees(angle);
        }

        // --- 2. 邊界檢查 ---
        glm::vec3& pos = gameObject->transform->position;
        bool hitWall = false;
        if (pos.x > mapLimit) { pos.x = mapLimit; hitWall = true; }
        if (pos.x < -mapLimit) { pos.x = -mapLimit; hitWall = true; }
        if (pos.z > mapLimit) { pos.z = mapLimit; hitWall = true; }
        if (pos.z < -mapLimit) { pos.z = -mapLimit; hitWall = true; }

        if (hitWall) {
            // 撞牆就往回彈一點，並立刻換方向
            pos -= currentDir * moveSpeed * dt;
            RandomizeDir();
        }

        // --- 3. 射擊邏輯 ---
        if (shooter) {
            shootTimer += dt;
            if (shootTimer > shootInterval) {
                // 槍口位置
                glm::vec3 gunPos = gameObject->transform->position + glm::vec3(0, 1.5f, 0) + currentDir * 0.8f;

                // 稍微增加射擊的隨機偏移 (Spread)，讓地板塗得更均勻
                float spreadX = ((rand() % 100) / 100.0f - 0.5f) * 0.5f; // -0.25 ~ 0.25
                glm::vec3 shootDir = glm::normalize(currentDir + glm::vec3(spreadX, -0.2f, 0.0f)); // 稍微往下射

                shooter->AIShoot(gunPos, shootDir);
                shootTimer = 0.0f;
            }
        }
    }

private:
    void RandomizeDir() {
        // 隨機產生 XZ 平面上的方向
        float x = (float)(rand() % 100) - 50;
        float z = (float)(rand() % 100) - 50;
        currentDir = glm::normalize(glm::vec3(x, 0, z));
    }
};