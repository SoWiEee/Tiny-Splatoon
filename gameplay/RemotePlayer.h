#pragma once
#include "../scene/Entity.h"
#include <glm/glm.hpp>

class RemotePlayer : public Entity {
public:
    int playerID;

    glm::vec3 targetPos;
    float targetRot;
    bool isSwimming = false;
    bool isSharking = false;
    bool isDead = false;

    float serverForceDeadTimer = 0.0f;

    GameObject* visualHuman = nullptr;
    GameObject* visualSquid = nullptr;

    RemotePlayer(int id, int team, glm::vec3 startPos)
        : Entity("RemotePlayer"), playerID(id)
    {
        this->teamID = team;
        transform->position = startPos;
        targetPos = startPos;
        targetRot = 0.0f;

        AddComponent<Health>(team, startPos);

        // 這裡不需要建立 visualBody，讓 GameWorld::CreateRemotePlayer 負責
    }

    ~RemotePlayer() {
        // visualHuman 和 visualSquid 生命週期由 GameWorld 管理，不需要在這裡 delete
    }

    void SetupVisuals(GameObject* human, GameObject* squid) {
        visualHuman = human;
        visualSquid = squid;
        UpdateVisualState(); // 初始化狀態
    }

    // 根據狀態切換模型 (人/魷魚)
    void UpdateVisualState() {
        if (!visualHuman || !visualSquid) return;

        bool showSquid = isSwimming || isSharking;

        // 如果死了，全部隱藏
        if (isDead) {
            visualHuman->active = false;
            visualSquid->active = false;
            return;
        }

        if (showSquid) {
            visualHuman->active = false;
            visualSquid->active = true;

            // 如果在騎鯊魚，可以微調魷魚的位置 (浮起來一點)
            // if (isSharking) visualSquid->transform->position.y += 0.5f; 
        }
        else {
            visualHuman->active = true;
            visualSquid->active = false;
        }
    }

    // 接收狀態更新
    void SetTargetState(glm::vec3 pos, float rotY, bool swimming, bool dead, bool sharking) {
        targetPos = pos;
        targetRot = rotY;
        isSwimming = swimming;
        isDead = dead;
        isSharking = sharking;

        // 處理血量狀態同步
        Health* hp = GetComponent<Health>();
        if (hp) {
            bool finalDeadState = dead;
            if (serverForceDeadTimer > 0.0f) {
                finalDeadState = true;
            }
            if (hp->isDead != finalDeadState) {
                hp->isDead = finalDeadState;
                hp->currentHP = finalDeadState ? 0.0f : hp->maxHP;
            }
        }

        // 更新視覺顯示
        UpdateVisualState();
    }

    // 插值更新
    void UpdateInterp(float dt) {
        if (serverForceDeadTimer > 0.0f) {
            serverForceDeadTimer -= dt;
        }

        // 位置插值
        transform->position = glm::mix(transform->position, targetPos, dt * 10.0f);

        // 旋轉插值 (處理角度迴圈)
        float currentY = transform->rotation.y;
        float diff = targetRot - currentY;
        while (diff < -180.0f) diff += 360.0f;
        while (diff > 180.0f) diff -= 360.0f;
        transform->rotation.y += diff * dt * 10.0f;

        UpdateVisualState();
    }

    void ForceDeadByServer(float duration = 2.0f) {
        serverForceDeadTimer = duration;
        Health* hp = GetComponent<Health>();
        if (hp) {
            hp->isDead = true;
            hp->currentHP = 0.0f;
        }
    }
};