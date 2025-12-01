#pragma once

#include "../scene/Entity.h"
#include "../engine/core/Input.h"
#include "../engine/audio/AudioManager.h"
#include "../components/MeshRenderer.h"
#include "../components/Health.h"
#include "../components/Camera.h"
#include "../scene/Level.h"
#include "Weapon.h"
#include "ShooterWeapon.h"
#include "BrushWeapon.h"
#include "SlosherWeapon.h"
#include "../splat/SplatMap.h"

enum class PlayerState {
    ALIVE,
    DEAD,
    LAUNCHING,
    SPECIAL_ATTACK
};

class Player : public Entity {
public:
    // parameter
    float moveSpeed = 5.0f;
    float swimSpeed = 12.0f;
    float jumpHeight = 1.5f;
    float gravity = -20.0f;

    // state
    glm::vec3 velocity = glm::vec3(0.0f);
    bool isGrounded = false;
    bool isSwimming = false;
    PlayerState state = PlayerState::ALIVE;
    float respawnTimer = 0.0f;
    float const RESPAWN_TIME = 3.0f; // 死亡後 3 秒重生

    // 大招參數
    float specialCharge = 0.0f;       // 當前能量
    float const MAX_SPECIAL = 100.0f; // 能量上限
    bool requestSplashdown = false; // 告訴 GameWorld 我砸地了
    int specialPhase = 0; // 0=升空, 1=滯空, 2=下砸
    float specialTimer = 0.0f;
    glm::vec3 specialStartPos;

    // super jump
    glm::vec3 jumpStartPos;
    glm::vec3 jumpTargetPos;
    float jumpTimer = 0.0f;
    float const JUMP_DURATION = 1.5f;

    // reference
    Weapon* weapon = nullptr;
    SplatMap* splatMapRef;
    GameObject* cameraRef;
    GameObject* shadow;
    GameObject* visualBody;
    HUD* hudRef = nullptr;
    Camera* camera = nullptr;
    Level* levelRef = nullptr;

    float healRateSlow = 10.0f;      // 站立回血 (漫長)
    float healRateFast = 20.0f;      // 潛水回血 (快速)
    float regenDelay = 2.0f;         // 受傷後要等 2 秒才能開始回血
    float currentRegenDelay = 0.0f;  // 計時器
    float mapLimit = 39.5f;
    float floorSize = 80.0f;

    Player(glm::vec3 startPos, int team, SplatMap* map, GameObject* cam, HUD* hud, Level* level)
        : Entity("Player"), splatMapRef(map), cameraRef(cam), hudRef(hud), levelRef(level)
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
            // 施放大招
            if (Input::GetKey(GLFW_KEY_Q) && IsSpecialReady()) {
                StartSpecialAttack();
            }

            // 墨水環境互動 
            if (splatMapRef) {
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

        case PlayerState::SPECIAL_ATTACK:
            UpdateSpecialAttack(dt);
            break;
        }
        UpdateVisuals(dt);
    }

    void AddSpecialCharge(float amount) {
        // 只有活著的時候能集氣
        if (state != PlayerState::ALIVE) return;

        if (specialCharge < MAX_SPECIAL) {
            specialCharge += amount;
            if (specialCharge >= MAX_SPECIAL) {
                specialCharge = MAX_SPECIAL;
                AudioManager::Instance().PlayOneShot("special_ready", 0.8f);
            }
        }
    }

    void StartSpecialAttack() {
        if (state != PlayerState::ALIVE) return;

        state = PlayerState::SPECIAL_ATTACK;
        specialPhase = 0; // 升空
        specialStartPos = transform->position;
        specialTimer = 0.0f;

        // 瞬間補滿墨水
        if (hudRef) hudRef->RefillInk(100.0f);

        // 播放音效 (集氣聲)
        // AudioManager::Instance().PlayOneShot("special_start", 1.0f);
    }

    bool IsSpecialReady() const {
        return specialCharge >= MAX_SPECIAL;
    }

    // 消耗大招
    void ResetSpecialCharge() {
        specialCharge = 0.0f;
    }

    GameObject* GetVisualBody() { return visualBody; }

    void Die() {
        // 防止重複死亡
        if (state == PlayerState::DEAD || state == PlayerState::LAUNCHING) return;

        state = PlayerState::DEAD;
        respawnTimer = RESPAWN_TIME;

        if (visualBody) visualBody->transform->scale = glm::vec3(0.0f);

        AudioManager::Instance().PlayOneShot("die", 1.0f);

        if (cameraRef) {
            glm::vec3 spawnPos = GetSpawnPosition();
            cameraRef->transform->position = spawnPos;
            cameraRef->transform->LookAt(glm::vec3(0.0f, 0.0f, 0.0f));
        }
    }

    void StartSuperJump() {
        state = PlayerState::LAUNCHING;
        jumpTimer = 0.0f;

        if (visualBody) visualBody->active = true;

        // 設定起點與終點 (根據隊伍)
        // 這裡假設我們能拿到 Level 的資訊，或者寫死
        // 假設 Team 1 在 -40, Team 2 在 40
        float zDir = (teamID == 1) ? -1.0f : 1.0f;
        jumpTargetPos = glm::vec3(0, 0.0f, 30.0f * zDir); // 落地點

        // reset health, ink
        GetComponent<Health>()->Reset();
        if (hudRef) hudRef->RefillInk(100.0f);

        AudioManager::Instance().PlayOneShot("superjump", 1.0f);
    }

private:
    glm::vec3 GetSpawnPosition() {
        float zDir = (teamID == 1) ? -1.0f : 1.0f;
        jumpStartPos = glm::vec3(0, 15.0f, 40.0f * zDir); // 高空重生點
        return jumpStartPos;
    }
    void UpdateDeadState(float dt) {
        respawnTimer -= dt;

        if (cameraRef) {
            cameraRef->transform->position = GetSpawnPosition();
            cameraRef->transform->LookAt(glm::vec3(sin(glfwGetTime())*5.0f, 0, 0));
        }

        if (respawnTimer <= 0.0f) {
            StartSuperJump();
        }
    }

    // 超級跳躍物理 (拋物線)
    void UpdateSuperJump(float dt) {
        jumpTimer += dt;
        float t = jumpTimer / JUMP_DURATION; // 0.0 ~ 1.0

        if (t >= 1.0f) {
            transform->position = jumpTargetPos;
            state = PlayerState::ALIVE;
            // AudioManager::Instance().PlayOneShot("land", 0.8f);
            if (camera) camera->TriggerShake(0.2f, 0.1f);
            return;
        }

        // 拋物線移動
        glm::vec3 currentPos = glm::mix(jumpStartPos, jumpTargetPos, t);
        float heightOffset = 15.0f * sin(t * 3.14159f); // 弧線高度
        currentPos.y += heightOffset;

        transform->position = currentPos;
        transform->rotation.y += 720.0f * dt;
        transform->rotation.x = -90.0f * (1.0f - t);
    }

    // 大招動作邏輯
    void UpdateSpecialAttack(float dt) {
        // Phase 0: 快速升空 (Rise)
        if (specialPhase == 0) {
            // 強制向上移動
            float riseSpeed = 15.0f;
            transform->position.y += riseSpeed * dt;

            // 旋轉特效
            transform->rotation.y += 720.0f * dt;

            // 升空高度限制 (例如 6米)
            if (transform->position.y >= specialStartPos.y + 6.0f) {
                specialPhase = 1; // 進入滯空
                specialTimer = 0.0f;
            }
        }
        // Phase 1: 滯空 (Hover)
        else if (specialPhase == 1) {
            specialTimer += dt;

            // 微微上下浮動
            transform->position.y += sin(specialTimer * 10.0f) * 0.05f;

            // 滯空 0.6 秒後下砸
            if (specialTimer > 0.6f) {
                specialPhase = 2; // 進入下砸
                // 播放下砸音效預備
                AudioManager::Instance().PlayOneShot("special_fall", 1.0f);
            }
        }
        // Phase 2: 急速下砸 (Slam)
        else if (specialPhase == 2) {
            float slamSpeed = 40.0f; // 超快
            transform->position.y -= slamSpeed * dt;

            // 著地檢查
            float groundH = 0.0f;
            if (levelRef) groundH = levelRef->GetHeightAt(transform->position.x, transform->position.z);

            if (transform->position.y <= groundH) {
                // 1. 修正位置
                transform->position.y = groundH;

                // 2. [關鍵] 舉旗通知 GameWorld 造成傷害
                requestSplashdown = true;

                // 3. 扣除能量
                specialCharge = 0.0f;

                // 4. 恢復狀態
                state = PlayerState::ALIVE;
                velocity = glm::vec3(0); // 重置速度，避免慣性

                // 5. 震動
                if (camera) camera->TriggerShake(0.5f, 0.5f); // 強力震動
            }
        }
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