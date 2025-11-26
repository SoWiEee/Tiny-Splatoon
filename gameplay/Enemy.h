#pragma once
#include "../scene/Entity.h"
#include "Weapon.h"
#include "ShooterWeapon.h"
#include "ShotgunWeapon.h"
#include "BowWeapon.h"
#include "../components/MeshRenderer.h"
#include "../components/Health.h"
#include <glm/glm.hpp>
#include <cstdlib>

class Enemy : public Entity {
public:
    // --- 屬性 ---
    int teamID;
    float moveSpeed = 3.0f;
    float mapLimit = 18.0f;

    // state
    float changeDirTime = 2.0f;
    float timer = 0.0f;
    glm::vec3 currentDir = glm::vec3(0, 0, 1);

    // reference
    Weapon* weapon = nullptr;;
    GameObject* visualBody;
    GameObject* shadow;

    Enemy(glm::vec3 startPos, int team) : Entity("Enemy"), teamID(team) {
        shadow = new GameObject("ShadowBlob");
        shadow->AddComponent<MeshRenderer>("Plane", glm::vec3(0.0f, 0.0f, 0.0f)); // 黑色
        // 稍微浮在地板上一點點，避免 Z-Fighting
        shadow->transform->position = transform->position + glm::vec3(0, 0.02f, 0);
        shadow->transform->scale = glm::vec3(1.2f, 1.0f, 1.2f); // 陰影比人稍微大一點
        transform->position = startPos;
        AddComponent<Health>(team, startPos);
        weapon = new ShooterWeapon(team, glm::vec3(0, 1, 0));

        visualBody = new GameObject("EnemyBody");
        visualBody->AddComponent<MeshRenderer>("Cube", weapon->inkColor);

        RandomizeDir();
    }

    ~Enemy() {
        if (weapon) delete weapon;
    }

    void UpdateLogic(float dt) {
        // walk
        timer += dt;
        if (timer > changeDirTime) {
            RandomizeDir();
            timer = 0.0f;
            changeDirTime = 1.0f + (float)(rand() % 20) / 10.0f;
        }

        // move
        transform->position += currentDir * moveSpeed * dt;

        // rotate
        if (glm::length(currentDir) > 0.01f) {
            float angle = atan2(currentDir.x, currentDir.z);
            transform->rotation.y = glm::degrees(angle);
        }

        CheckBounds(dt);

        if (visualBody) {
            visualBody->transform->position = transform->position + glm::vec3(0, 0.9f, 0);
            visualBody->transform->rotation = transform->rotation;
        }

        if (weapon) {
            glm::vec3 gunPos = transform->position + glm::vec3(0, 1.5f, 0) + currentDir * 0.8f;
            float spread = ((rand() % 100) / 100.0f - 0.5f) * 0.5f;
            glm::vec3 aimDir = glm::normalize(currentDir + glm::vec3(spread, -0.2f, 0.0f));

            weapon->Trigger(dt, gunPos, aimDir, true);
        }
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