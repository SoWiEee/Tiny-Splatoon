#pragma once
#include "../scene/Entity.h"
#include "../components/MeshRenderer.h"

class RemotePlayer : public Entity {
public:
    int playerID;
    int teamID;

    // 用於插值的目標位置
    glm::vec3 targetPos;
    float targetRot;

    GameObject* visualBody;

    RemotePlayer(int id, int team, glm::vec3 startPos)
        : Entity("RemotePlayer"), playerID(id), teamID(team)
    {
        transform->position = startPos;
        targetPos = startPos;

        visualBody = new GameObject("RemoteBody");
        glm::vec3 color = (team == 1) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0);
        visualBody->AddComponent<MeshRenderer>("Cube", color);
    }

    void UpdateInterp(float dt) {
        // 簡單的線性插值 (Lerp) 平滑移動
        // 實際網路遊戲會用更複雜的 Entity Interpolation
        transform->position = glm::mix(transform->position, targetPos, dt * 10.0f);

        // 旋轉插值... (略)

        // 同步視覺
        if (visualBody) {
            visualBody->transform->position = transform->position + glm::vec3(0, 0.9f, 0);
            visualBody->transform->rotation = transform->rotation;
        }
    }

    void SetTargetState(glm::vec3 pos, float rot) {
        targetPos = pos;
        targetRot = rot;
    }
};