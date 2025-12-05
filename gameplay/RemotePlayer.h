#pragma once
#include "../scene/Entity.h"
#include "../components/MeshRenderer.h"
#include <glm/glm.hpp>

class RemotePlayer : public Entity {
public:
    int playerID;
    glm::vec3 targetPos;
    float targetRot;
    bool isSwimming = false;
    float serverForceDeadTimer = 0.0f;

    GameObject* visualBody;
    GameObject* shadow = nullptr;

    RemotePlayer(int id, int team, glm::vec3 startPos)
        : Entity("RemotePlayer"), playerID(id)
    {
        // 初始化位置
        this->teamID = team;
        transform->position = startPos;
        targetPos = startPos;
        targetRot = 0.0f;
        AddComponent<Health>(team, startPos);

        visualBody = new GameObject("RemoteBody");

        // 1=red, 1=green
        glm::vec3 color = (team == 1) ? glm::vec3(1, 0, 0) :
            (team == 2) ? glm::vec3(0, 1, 0) : glm::vec3(0, 0, 1);

        visualBody->AddComponent<MeshRenderer>("Cube", color);
    }

    ~RemotePlayer() {
        if (visualBody) delete visualBody;
        if (shadow) delete shadow;
    }

    GameObject* GetVisualBody() { return visualBody; }

    // 接收狀態更新
    void SetTargetState(glm::vec3 pos, float rotY, bool swimming, bool dead) {
        targetPos = pos;
        targetRot = rotY;
        isSwimming = swimming;

        Health* hp = GetComponent<Health>();
        if (hp) {
            bool finalDeadState = dead;
            if (serverForceDeadTimer > 0.0f) {
                finalDeadState = true;
            }
            if (hp->isDead != finalDeadState) {
                hp->isDead = finalDeadState;

                if (!finalDeadState) {
                    hp->currentHP = hp->maxHP;
                }
                else {
                    hp->currentHP = 0.0f;
                }
            }
        }
    }

    // 插值更新
    void UpdateInterp(float dt) {
        if (serverForceDeadTimer > 0.0f) {
            serverForceDeadTimer -= dt;
        }

        transform->position = glm::mix(transform->position, targetPos, dt * 10.0f);
        float angleDiff = targetRot - transform->rotation.y;
        transform->rotation.y += angleDiff * dt * 10.0f;

        // 同步視覺物件位置
        if (visualBody) {
            if (isSwimming) {
                // 變扁 (魷魚狀態)
                visualBody->transform->scale = glm::vec3(0.6f, 0.1f, 0.6f);
                visualBody->transform->position = transform->position + glm::vec3(0, 0.05f, 0);
            }
            else {
                // 站立狀態
                visualBody->transform->scale = glm::vec3(0.5f, 1.8f, 0.5f);
                // 修正中心點高度
                visualBody->transform->position = transform->position + glm::vec3(0, 0.9f, 0);
            }
            // 同步旋轉
            visualBody->transform->rotation = transform->rotation;
        }
    }

    // 當收到網路封包時呼叫此函式
    void SetTargetState(glm::vec3 pos, float rot) {
        targetPos = pos;
        targetRot = rot;
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