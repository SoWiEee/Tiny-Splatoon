#pragma once
#include "../scene/Entity.h"
#include "../components/MeshRenderer.h"
#include <glm/glm.hpp>

class RemotePlayer : public Entity {
public:
    int playerID;
    glm::vec3 targetPos;
    float targetRot;

    GameObject* visualBody;

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
    }

    // [核心] 插值更新：每一幀呼叫，讓移動平滑
    void UpdateInterp(float dt) {
        // 1. 位置插值 (Lerp)
        // 10.0f 是插值速度，數值越大越緊跟(但抖動)，越小越平滑(但延遲)
        transform->position = glm::mix(transform->position, targetPos, dt * 10.0f);

        // 2. 角度插值 (簡單處理)
        float angleDiff = targetRot - transform->rotation.y;
        // 處理 360 度跨越問題 (這裡簡化略過，實務上需要處理 -180 到 180 的跳變)
        transform->rotation.y += angleDiff * dt * 10.0f;

        // 3. 同步視覺物件位置
        if (visualBody) {
            // 視覺高度修正 (假設 pivot 在腳底)
            visualBody->transform->position = transform->position + glm::vec3(0, 0.9f, 0);
            visualBody->transform->rotation = transform->rotation;
        }
    }

    // 當收到網路封包時呼叫此函式
    void SetTargetState(glm::vec3 pos, float rot) {
        targetPos = pos;
        targetRot = rot;
    }

    GameObject* GetVisualBody() { return visualBody; }
};