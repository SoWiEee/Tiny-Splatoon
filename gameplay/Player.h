#pragma once

#include "../scene/Entity.h"
#include "../engine/core/Input.h"
#include "../engine/audio/AudioManager.h"
#include "../components/MeshRenderer.h"
#include "../components/Health.h"
#include "../components/Camera.h"
#include "Weapon.h"
#include "ShooterWeapon.h"
#include "BrushWeapon.h"
#include "SlosherWeapon.h"
#include "../splat/SplatMap.h"

enum class PlayerState {
    ALIVE,      // 正常遊玩
    DEAD,       // 死亡 (等待重生)
    LAUNCHING   // 超級跳躍進場中
};

class Player : public Entity {
public:
    // parameter
    float moveSpeed = 5.0f;
    float swimSpeed = 12.0f;
    float jumpHeight = 2.0f;
    float gravity = -20.0f;

    // state
    glm::vec3 velocity = glm::vec3(0.0f);
    bool isGrounded = false;
    bool isSwimming = false;
    PlayerState state = PlayerState::ALIVE;
    float respawnTimer = 0.0f;
    float const RESPAWN_TIME = 3.0f; // 死亡後 3 秒重生

    // 超級跳躍參數
    glm::vec3 jumpStartPos;
    glm::vec3 jumpTargetPos;
    float jumpTimer = 0.0f;
    float const JUMP_DURATION = 1.5f; // 跳躍飛行時間

    // reference
    Weapon* weapon = nullptr;
    SplatMap* splatMapRef;
    GameObject* cameraRef;
    GameObject* shadow;
    GameObject* visualBody;
    HUD* hudRef = nullptr;
    Camera* camera = nullptr;

    float healRateSlow = 10.0f;      // 站立回血 (漫長)
    float healRateFast = 20.0f;      // 潛水回血 (快速)
    float regenDelay = 2.0f;         // 受傷後要等 2 秒才能開始回血
    float currentRegenDelay = 0.0f;  // 計時器
    float mapLimit = 39.5f;
    float floorSize = 80.0f;

    Player(glm::vec3 startPos, int team, SplatMap* map, GameObject* cam, HUD* hud)
        : Entity("Player"), splatMapRef(map), cameraRef(cam), hudRef(hud)
    {
        this->teamID = team;
        shadow = new GameObject("ShadowBlob");
        shadow->AddComponent<MeshRenderer>("Plane", glm::vec3(0.0f, 0.0f, 0.0f));
        shadow->transform->position = transform->position + glm::vec3(0, 0.02f, 0);
        shadow->transform->scale = glm::vec3(1.2f, 1.0f, 1.2f);

        transform->position = startPos;
        camera = cam->GetComponent<Camera>();
        weapon = new BrushWeapon(team, (team == 1) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0));
        AddComponent<Health>(team, startPos);
        visualBody = new GameObject("PlayerBody");
        visualBody->AddComponent<MeshRenderer>("Cube", weapon->inkColor);
        StartSuperJump();
    }

    ~Player() {
        if (weapon) delete weapon;
    }

    void EquipWeapon(Weapon* newWeapon) {
        if (weapon) delete weapon;
        weapon = newWeapon;
    }

    // 主邏輯更新
    void UpdateLogic(float dt) {
        switch (state) {
        case PlayerState::ALIVE:
            HandleInput(dt);

            // 墨水環境互動 
            if (splatMapRef) {
                // 計算 UV 座標 (假設 floorSize 是地圖總寬度，中心在 0,0)
                // 注意：這裡的 UV 計算需跟 Shader/SplatPhysics 一致
                float u = (transform->position.x / floorSize) + 0.5f;
                float v = (transform->position.z / floorSize) + 0.5f;

                // 檢查是否踩在敵方墨水上
                // 假設：我方 teamID=1, 敵方=2; 我方=2, 敵方=1
                int enemyTeam = (teamID == 1) ? 2 : 1;
                bool onEnemyInk = splatMapRef->IsColorInArea(u, v, enemyTeam, 1);

                auto healthComp = GetComponent<Health>();
                if (healthComp) {
                    if (!onEnemyInk) {
                        // B. 安全地帶：倒數回血
                        if (currentRegenDelay > 0.0f) {
                            currentRegenDelay -= dt;
                        }
                        else {
                            // 時間到，開始回血
                            if (isSwimming) {
                                // 潛水：快速回血
                                healthComp->Heal(healRateFast * dt);
                            }
                            else {
                                // 站立：慢速回血
                                healthComp->Heal(healRateSlow * dt);
                            }
                        }
                    }
                }
            }
            ApplyPhysics(dt);
            break;

        case PlayerState::DEAD:
            UpdateDeadState(dt);
            break;

        case PlayerState::LAUNCHING:
            UpdateSuperJump(dt);
            break;
        }
        UpdateVisuals(dt);
    }

    GameObject* GetVisualBody() { return visualBody; }

    void Die() {
        // 防止重複死亡
        if (state == PlayerState::DEAD || state == PlayerState::LAUNCHING) return;

        state = PlayerState::DEAD;
        respawnTimer = RESPAWN_TIME; // 設定 3 秒倒數

        // 隱藏模型
        if (visualBody) visualBody->transform->scale = glm::vec3(0.0f);

        // 播放音效
        // AudioManager::Instance().PlayOneShot("die", 1.0f);
    }

    // 開始超級跳躍
    void StartSuperJump() {
        state = PlayerState::LAUNCHING;
        jumpTimer = 0.0f;

        // 顯示模型
        if (visualBody) visualBody->active = true;

        // 設定起點與終點 (根據隊伍)
        // 這裡假設我們能拿到 Level 的資訊，或者寫死
        // 假設 Team 1 在 -40, Team 2 在 40
        float zDir = (teamID == 1) ? -1.0f : 1.0f;

        jumpStartPos = glm::vec3(0, 15.0f, 40.0f * zDir); // 高空重生點
        jumpTargetPos = glm::vec3(0, 0.0f, 30.0f * zDir); // 落地點

        // 重置血量與墨水
        GetComponent<Health>()->Reset();
        if (hudRef) hudRef->RefillInk(100.0f);

        // 播放跳躍音效
        AudioManager::Instance().PlayOneShot("superjump", 1.0f);
    }

private:
    void UpdateDeadState(float dt) {
        respawnTimer -= dt;

        // 可以在這裡讓 Camera 慢慢轉向重生點，或是看著屍體

        if (respawnTimer <= 0.0f) {
            StartSuperJump();
        }
    }

    // 超級跳躍物理 (拋物線)
    void UpdateSuperJump(float dt) {
        jumpTimer += dt;
        float t = jumpTimer / JUMP_DURATION; // 0.0 ~ 1.0

        if (t >= 1.0f) {
            // 落地
            transform->position = jumpTargetPos;
            state = PlayerState::ALIVE;

            // 落地音效 & 震動
            AudioManager::Instance().PlayOneShot("land", 0.8f);
            if (camera) camera->TriggerShake(0.2f, 0.1f);

            return;
        }

        // --- 拋物線公式 ---
        // Lerp 插值水平位置
        glm::vec3 currentPos = glm::mix(jumpStartPos, jumpTargetPos, t);

        // 加上垂直高度曲線 (Sine wave 或 Parabola)
        // 讓他在中間 (t=0.5) 飛最高
        float heightOffset = 15.0f * sin(t * 3.14159f);
        currentPos.y += heightOffset;

        // 讓起點Y和終點Y平滑過渡
        // 這裡直接用 mix 已經處理了 Y 的線性變化，加上 heightOffset 形成弧線

        transform->position = currentPos;

        // 讓角色旋轉 (帥氣進場)
        transform->rotation.y += 720.0f * dt; // 旋轉兩圈
        transform->rotation.x = -90.0f * (1.0f - t); // 從朝下變成朝前
    }

    void HandleInput(float dt) {
        if (!cameraRef) return;

        bool onMyInk = false;
        if (splatMapRef) {
            float u = (transform->position.x + floorSize / 2.0f) / floorSize;
            float v = 1.0f - ((transform->position.z + floorSize / 2.0f) / floorSize);
            onMyInk = splatMapRef->IsColorInArea(u, v, teamID, 1);
        }

        bool wantSwim = Input::GetKey(GLFW_KEY_LEFT_SHIFT);
        isSwimming = (wantSwim && onMyInk);

        float currentSpeed = isSwimming ? swimSpeed : moveSpeed;

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
            transform->rotation.y = cameraRef->transform->rotation.y;
        }

        velocity.x = targetVel.x;
        velocity.z = targetVel.z;

        if (Input::GetKey(GLFW_KEY_SPACE) && isGrounded && !isSwimming) {
            velocity.y = sqrt(2.0f * jumpHeight * abs(gravity));
            isGrounded = false;
        }

        bool hasInk = (hudRef && hudRef->currentInk > 0.0f);
        bool isFiring = Input::GetMouseButton(0) && !isSwimming && hasInk;

        glm::vec3 gunPos = transform->position + glm::vec3(0, 1.5f, 0) + right * 0.5f + front * 0.5f;

        if (weapon) {
            bool hasInk = (hudRef && hudRef->currentInk >= weapon->inkCost);
            bool isFiring = Input::GetMouseButton(0) && !isSwimming && hasInk;

            glm::vec3 gunPos = transform->position + glm::vec3(0, 1.5f, 0) + right * 0.5f + front * 0.5f;

            if (weapon->Trigger(dt, gunPos, cameraRef->transform->GetForward(), isFiring)) {
                if (hudRef) hudRef->ConsumeInk(weapon->inkCost);
                camera->TriggerShake(0.1f, 0.05f);
                AudioManager::Instance().PlayOneShot("shoot", 0.5f);
            }
        }
    }

    void ApplyPhysics(float dt) {
        velocity.y += gravity * dt;
        transform->position += velocity * dt;

        if (transform->position.y < 0.0f) {
            transform->position.y = 0.0f;
            velocity.y = 0;
            isGrounded = true;
        }
        else {
            isGrounded = false;
        }

        if (transform->position.x > mapLimit) transform->position.x = mapLimit;
        if (transform->position.x < -mapLimit) transform->position.x = -mapLimit;
        if (transform->position.z > mapLimit) transform->position.z = mapLimit;
        if (transform->position.z < -mapLimit) transform->position.z = -mapLimit;
    }

    void UpdateVisuals(float dt) {
        if (!visualBody) return;

        if (hudRef) {
            bool isTryingToShoot = Input::GetMouseButton(0) && !isSwimming;
            if (!isTryingToShoot) {
                float refillRate = isSwimming ? 0.5f : 0.1f;
                hudRef->RefillInk(refillRate * dt);
            }
        }

        if (isSwimming) {
            visualBody->transform->scale = glm::vec3(0.6f, 0.1f, 0.6f);
            visualBody->transform->position = transform->position + glm::vec3(0, 0.05f, 0);
        }
        else {
            visualBody->transform->scale = glm::vec3(0.5f, 1.8f, 0.5f);
            visualBody->transform->position = transform->position + glm::vec3(0, 0.9f, 0);
        }
        visualBody->transform->rotation = transform->rotation;
    }
};