#pragma once

#include "../scene/Entity.h"
#include "../engine/core/Input.h"
#include "../components/MeshRenderer.h"
#include "Weapon.h"
#include "../splat/SplatMap.h"

class Player : public Entity {
public:
    // --- 屬性 ---
    int teamID;
    float moveSpeed = 5.0f;
    float swimSpeed = 10.0f;
    float jumpHeight = 2.0f;
    float gravity = -15.0f; // 重力感強一點

    // --- 狀態 ---
    glm::vec3 velocity = glm::vec3(0.0f);
    bool isGrounded = false;
    bool isSwimming = false;

    // --- 外部參照 ---
    Weapon weapon;            // 玩家持有的武器
    SplatMap* splatMapRef;    // 用來查詢墨水
    GameObject* cameraRef;    // 用來決定移動方向 (TPS)
    GameObject* visualBody;   // 身體模型 (用於潛水變形)

    // 場地邊界 (之後可以從 GameWorld 或 Level 取得)
    float mapLimit = 19.5f;
    float floorSize = 40.0f;

    // 建構子
    Player(glm::vec3 startPos, int team, SplatMap* map, GameObject* cam)
        : Entity("Player"), teamID(team), splatMapRef(map), cameraRef(cam),
        weapon(team, (team == 1) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0)) // 預設武器顏色
    {
        transform->position = startPos;

        // 建立視覺身體 (作為子物件的概念，雖然我們目前沒有 Hierarchy，手動同步即可)
        visualBody = new GameObject("PlayerBody");
        visualBody->AddComponent<MeshRenderer>("Cube", weapon.inkColor); // 身體顏色跟墨水一樣

        // 玩家本體只是一個邏輯點，不需要 MeshRenderer
    }

    // 這裡我們不 override GameObject::Update，而是提供 UpdateLogic 讓 GameWorld 呼叫
    // 這樣可以確保執行順序 (先 Input -> 再 Physics)
    void UpdateLogic(float dt) {

        // 1. 處理狀態與輸入
        HandleInput(dt);

        // 2. 物理模擬
        ApplyPhysics(dt);

        // 3. 更新視覺模型 (跟隨邏輯點)
        UpdateVisuals();
    }

    // 將視覺物件加入場景清單 (因為 visualBody 是獨立的 GameObject)
    GameObject* GetVisualBody() { return visualBody; }

private:
    void HandleInput(float dt) {
        if (!cameraRef) return;

        // --- A. 檢查墨水狀態 ---
        bool onMyInk = false;
        if (splatMapRef) {
            // 計算 UV
            float u = (transform->position.x + floorSize / 2.0f) / floorSize;
            float v = 1.0f - ((transform->position.z + floorSize / 2.0f) / floorSize);
            // 查詢顏色 (半徑 1 代表寬容判定)
            onMyInk = splatMapRef->IsColorInArea(u, v, teamID, 1);
        }

        // --- B. 潛水判定 ---
        bool wantSwim = Input::GetKey(GLFW_KEY_LEFT_SHIFT);
        isSwimming = (wantSwim && onMyInk);

        // --- C. 移動控制 (WASD) ---
        float currentSpeed = isSwimming ? swimSpeed : moveSpeed;

        // 取得相機的水平方向
        glm::vec3 camFwd = cameraRef->transform->GetForward();
        glm::vec3 camRight = cameraRef->transform->GetRight();

        glm::vec3 front = glm::normalize(glm::vec3(camFwd.x, 0.0f, camFwd.z));
        glm::vec3 right = glm::normalize(glm::vec3(camRight.x, 0.0f, camRight.z));

        glm::vec3 targetVel = glm::vec3(0.0f);
        if (Input::GetKey(GLFW_KEY_W)) targetVel += front;
        if (Input::GetKey(GLFW_KEY_S)) targetVel -= front;
        if (Input::GetKey(GLFW_KEY_A)) targetVel -= right;
        if (Input::GetKey(GLFW_KEY_D)) targetVel += right;

        if (glm::length(targetVel) > 0.1f) {
            targetVel = glm::normalize(targetVel) * currentSpeed;
            // 同步轉向：讓玩家面向移動方向 (如果不是潛水狀態)或是跟隨相機
            // 這裡採用跟隨相機 (TPS 射擊模式)
            transform->rotation.y = cameraRef->transform->rotation.y;
        }

        // 應用水平速度 (保留 Y 軸垂直速度)
        velocity.x = targetVel.x;
        velocity.z = targetVel.z;

        // --- D. 跳躍 ---
        if (Input::GetKey(GLFW_KEY_SPACE) && isGrounded && !isSwimming) {
            velocity.y = sqrt(2.0f * jumpHeight * abs(gravity));
            isGrounded = false;
        }

        // --- E. 射擊 ---
        // 只有站立時可以射擊
        bool isFiring = Input::GetMouseButton(0) && !isSwimming;

        // 計算槍口位置 (稍微在右側一點)
        glm::vec3 gunPos = transform->position + glm::vec3(0, 1.5f, 0) + right * 0.5f + front * 0.5f;

        // 觸發武器 (Weapon 會把生成的子彈加到 pendingSpawns)
        weapon.Trigger(dt, gunPos, cameraRef->transform->GetForward(), isFiring);
    }

    void ApplyPhysics(float dt) {
        velocity.y += gravity * dt;
        transform->position += velocity * dt;

        // 地板碰撞 (簡化版：假設地板在 y=0)
        // 之後可以改用 Level->floor->GetHeightAt(x, z)
        float floorHeight = isSwimming ? 0.5f : 2.0f; // 潛水時貼地，站立時有高度(眼睛)

        // 這裡要注意：如果 visualBody 的 origin 在中心，那 transform.y 應該是高度的一半
        // 假設 transform.position 代表"腳底"，則:
        if (transform->position.y < 0.0f) {
            transform->position.y = 0.0f;
            velocity.y = 0;
            isGrounded = true;
        }
        else {
            isGrounded = false;
        }

        // 邊界限制
        if (transform->position.x > mapLimit) transform->position.x = mapLimit;
        if (transform->position.x < -mapLimit) transform->position.x = -mapLimit;
        if (transform->position.z > mapLimit) transform->position.z = mapLimit;
        if (transform->position.z < -mapLimit) transform->position.z = -mapLimit;
    }

    void UpdateVisuals() {
        if (!visualBody) return;

        if (isSwimming) {
            visualBody->transform->scale = glm::vec3(0.5f, 0.2f, 0.5f);
            visualBody->transform->position = transform->position + glm::vec3(0, 0.1f, 0);
        }
        else {
            visualBody->transform->scale = glm::vec3(0.5f, 1.8f, 0.5f);
            visualBody->transform->position = transform->position + glm::vec3(0, 0.9f, 0);
        }

        visualBody->transform->rotation = transform->rotation;
    }
};