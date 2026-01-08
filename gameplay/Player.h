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
    ALIVE,      // 正常遊玩
    DEAD,       // 死亡 (等待重生)
    LAUNCHING,  // 超級跳躍進場中
    SHARKING    // 鯊魚坐騎狀態
};

class Player : public Entity {
public:
    // parameter
    float moveSpeed = 3.0f;
    float swimSpeed = 9.0f;
    float jumpHeight = 2.0f;
    float gravity = -30.0f;
    Level* level = nullptr;

    // state
    glm::vec3 velocity = glm::vec3(0.0f);
    bool isGrounded = false;
    bool isSwimming = false;
    PlayerState state = PlayerState::ALIVE;
    float respawnTimer = 0.0f;
    float const RESPAWN_TIME = 3.0f; // 死亡後 3 秒重生

    // --- 道具狀態 ---
    bool hasBomb = false;
    bool requestBombThrow = false;

    // --- [新增] 火箭大招 (Tri-zooka) 狀態 ---
    bool isRocketActive = false;    // 是否正在大招模式
    int rocketAmmo = 0;             // 剩餘發射次數 (3次)
    float rocketCooldownTimer = 0.0f; // 射擊間隔
    float rocketDurationTimer = 0.0f; // 大招持續時間
    bool requestRocketFire = false;   // [請求] 發射火箭 (給 NetworkManager 看)

    // --- 鯊魚坐騎參數 ---
    int sharkDashCount = 0;         // 剩餘衝刺次數 (總共 3 次)
    bool isSharkDashing = false;    // 目前是在「衝刺中」還是「停頓瞄準中」
    float sharkStateTimer = 0.0f;   // 共用計時器 (衝刺倒數 或 停頓倒數)

    // 參數設定
    float sharkDashDuration = 1.0f;
    float sharkPauseDuration = 0.7f;
    float sharkSpeed = 13.0f;        // 衝刺速度 (要快一點才爽)
    float sharkInkTimer = 0.0f;      // 噴墨計時器
    glm::vec3 sharkDashDirection = glm::vec3(0, 0, 1);
    bool requestSharkSpray = false;
    bool requestSharkExplode = false;

    bool requestLaser = false;
    float currentCharge = 0.0f;       // 當前能量
    float const MAX_SPECIAL = 100.0f; // 能量上限

    // 超級跳躍參數
    glm::vec3 jumpStartPos;
    glm::vec3 jumpTargetPos;
    float jumpTimer = 0.0f;
    float const JUMP_DURATION = 1.5f; // 跳躍飛行時間

    // reference
    Weapon* weapon = nullptr;
    SplatMap* mapFloor = nullptr;
    SplatMap* mapObstacle = nullptr;
    GameObject* cameraRef;
    GameObject* shadow;
    GameObject* visualBody;
    HUD* hudRef = nullptr;
    Camera* camera = nullptr;
    GameObject* visualHuman = nullptr;
    GameObject* visualSquid = nullptr;

    float healRateSlow = 10.0f;      // 站立回血 (漫長)
    float healRateFast = 20.0f;      // 潛水回血 (快速)
    float regenDelay = 2.0f;         // 受傷後要等 2 秒才能開始回血
    float currentRegenDelay = 0.0f;  // 計時器
    float mapLimit = 39.5f;
    float floorSize = 80.0f;

    Player(glm::vec3 startPos, int team, SplatMap* floor, SplatMap* obstacle, GameObject* cam, HUD* hud, Level* mapLevel)
        : Entity("Player"), mapFloor(floor), mapObstacle(obstacle), cameraRef(cam), hudRef(hud), level(mapLevel)
    {
        this->teamID = team;
        transform->position = startPos;

        if (cam) camera = cam->GetComponent<Camera>();
        weapon = nullptr;

        AddComponent<Health>(team, startPos);

        visualBody = nullptr;
        shadow = nullptr;

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
        auto hpComp = GetComponent<Health>();
        if (state == PlayerState::ALIVE && hpComp && hpComp->isDead) {
            Die();
            return;
        }

        switch (state) {
        case PlayerState::ALIVE:
            HandleInput(dt);
            UpdateRocketState(dt);

            // 按下 Q 鍵發動大招 (這裡示範兩種大招，你可以自己決定要用哪一種)
            if (Input::GetKey(GLFW_KEY_Q) && IsSpecialReady()) {
                // 如果你想用鯊魚： StartSpecialShark();
                // 如果你想用火箭：
                ActivateRocketSpecial();

                // 扣除能量
                currentCharge = 0.0f;
            }

            if (level && mapFloor && mapObstacle) {

                // 1. 計算 UV 座標
                float u = (transform->position.x / floorSize) + 0.5f;
                float v = (transform->position.z / floorSize) + 0.5f;

                float currentHeight = level->GetHeightAt(transform->position.x, transform->position.z);
                SplatMap* targetMap = mapFloor;
                if (currentHeight > 0.5f) {
                    targetMap = mapObstacle;
                }

                int enemyTeam = (teamID == 1) ? 2 : 1;
                bool onEnemyInk = targetMap->IsColorInArea(u, v, enemyTeam, 1);
                bool onMyInk = targetMap->IsColorInArea(u, v, teamID, 1);

                // --- 傷害與回血邏輯 ---
                auto healthComp = GetComponent<Health>();
                if (healthComp) {
                    if (!onEnemyInk && currentRegenDelay > 0.0f) {
                        currentRegenDelay -= dt;
                    }
                    else {
                        // 根據是否潛水決定回血速度
                        if (isSwimming) healthComp->Heal(healRateFast * dt);
                        else healthComp->Heal(healRateSlow * dt);
                    }
                }
            }
            ApplyPhysics(dt);
            break;

        case PlayerState::SHARKING:
            UpdateShark(dt);
            break;

        case PlayerState::DEAD:
            UpdateDeadState(dt);
            break;

        case PlayerState::LAUNCHING:
            UpdateSuperJump(dt);
            break;
        }
        UpdateInk(dt);
        UpdateVisualState();
    }

    GameObject* GetVisualBody() { return visualBody; }

    void AddSpecialCharge(float amount) {
        if (state != PlayerState::ALIVE) return;

        if (currentCharge < MAX_SPECIAL) {
            currentCharge += amount;
            if (currentCharge >= MAX_SPECIAL) {
                currentCharge = MAX_SPECIAL;
                AudioManager::Instance().PlayOneShot("special_ready");
            }
        }
    }

    bool IsSpecialReady() const {
        return currentCharge >= MAX_SPECIAL;
    }
    void ResetSpecialCharge() {
        currentCharge = 0.0f;
    }

    void Die() {
        // 防止重複死亡
        if (state == PlayerState::DEAD || state == PlayerState::LAUNCHING) return;

        state = PlayerState::DEAD;
        respawnTimer = RESPAWN_TIME;

        // 死亡時重置大招狀態
        isRocketActive = false;
        rocketAmmo = 0;

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

        float zDir = (teamID == 1) ? -1.0f : 1.0f;
        jumpTargetPos = glm::vec3(0, 0.0f, 30.0f * zDir); // 落地點

        GetComponent<Health>()->Reset();
        if (hudRef) hudRef->RefillInk(100.0f);
        AudioManager::Instance().PlayOneShot("superjump", 1.0f);
    }

    void PickupBomb() {
        if (!hasBomb) {
            hasBomb = true;
            std::cout << "[Player] Picked up a Bomb! Press R to throw." << std::endl;
        }
    }

    // 火箭大招邏輯
    void ActivateRocketSpecial() {
        if (isRocketActive) return;

        isRocketActive = true;
        rocketAmmo = 3;
        rocketDurationTimer = 6.0f;

        AudioManager::Instance().PlayOneShot("shark", 0.6f);    // 播放啟動音效
        std::cout << ">>> ROCKET LAUNCHER EQUIPPED! (Ammo: 3) <<<" << std::endl;
    }

    void UpdateRocketState(float dt) {
        if (!isRocketActive) return;

        // 1. 倒數計時
        rocketDurationTimer -= dt;
        if (rocketDurationTimer <= 0.0f) {
            DeactivateRocketSpecial();
            return;
        }

        // 2. 射擊冷卻倒數
        if (rocketCooldownTimer > 0.0f) {
            rocketCooldownTimer -= dt;
        }
    }

    void DeactivateRocketSpecial() {
        isRocketActive = false;
        rocketAmmo = 0;
        std::cout << "Special Ended." << std::endl;
    }

    // ==========================================

    void StartSpecialShark() {
        if (state != PlayerState::ALIVE) return;

        state = PlayerState::SHARKING;
        sharkDashCount = 3;
        StartNextDash();
        AudioManager::Instance().PlayOneShot("shark", 1.0f);
    }

    void StartNextDash() {
        isSharkDashing = true;
        sharkStateTimer = sharkDashDuration;
        sharkInkTimer = 0.0f;

        glm::vec3 camFwd = glm::vec3(0, 0, 1);
        if (cameraRef) {
            camFwd = cameraRef->transform->GetForward();
        }
        camFwd.y = 0.0f;
        if (glm::length(camFwd) > 0.01f) {
            camFwd = glm::normalize(camFwd);
        }
        sharkDashDirection = camFwd;

        velocity.x = sharkDashDirection.x * sharkSpeed;
        velocity.z = sharkDashDirection.z * sharkSpeed;
        velocity.y = 2.0f;

        transform->LookAt(transform->position + sharkDashDirection);
        AudioManager::Instance().PlayOneShot("shark", 0.6f);
    }

    void UpdateShark(float dt) {
        sharkStateTimer -= dt;

        if (glm::length(velocity) > 0.1f) {
            RotateTowards(velocity, 20.0f, dt);
        }

        if (isSharkDashing) {
            velocity.x = sharkDashDirection.x * sharkSpeed;
            velocity.z = sharkDashDirection.z * sharkSpeed;
            velocity.y += gravity * dt;

            sharkInkTimer += dt;
            if (sharkInkTimer > 0.08f) {
                sharkInkTimer = 0.0f;
                requestSharkSpray = true;
            }

            if (sharkStateTimer <= 0.0f) {
                sharkDashCount--;
                velocity = glm::vec3(0);

                if (sharkDashCount > 0) {
                    isSharkDashing = false;
                    sharkStateTimer = sharkPauseDuration;
                }
                else {
                    EndShark();
                }
            }
        }
        else {
            velocity = glm::vec3(0);
            velocity.y += gravity * dt;

            if (cameraRef) {
                glm::vec3 camFwd = cameraRef->transform->GetForward();
                camFwd.y = 0;
                RotateTowards(camFwd, 20.0f, dt);
            }
            if (sharkStateTimer <= 0.0f) {
                StartNextDash();
            }
        }

        ApplyPhysics(dt);
    }

    void EndShark() {
        state = PlayerState::ALIVE;
        velocity = glm::vec3(0);
        currentCharge = 0.0f;
    }

    void RotateTowards(glm::vec3 dir, float turnSpeed, float dt) {
        dir.y = 0;
        if (glm::length(dir) < 0.01f) return;
        dir = glm::normalize(dir);

        float targetAngle = glm::degrees(atan2(dir.x, dir.z));
        float currentAngle = transform->rotation.y;

        float diff = targetAngle - currentAngle;
        while (diff < -180.0f) diff += 360.0f;
        while (diff > 180.0f) diff -= 360.0f;

        transform->rotation.y += diff * turnSpeed * dt;
    }

    void SetupVisuals(GameObject* human, GameObject* squid) {
        visualHuman = human;
        visualSquid = squid;
        UpdateVisualState();
    }

    void UpdateVisualState() {
        if (!visualHuman || !visualSquid) return;

        bool isSquidForm = isSwimming || state == PlayerState::SHARKING;

        if (isSquidForm) {
            visualHuman->active = false;
            visualSquid->active = true;
        }
        else {
            visualHuman->active = true;
            visualSquid->active = false;
        }
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
            cameraRef->transform->LookAt(glm::vec3(sin(glfwGetTime()) * 5.0f, 0, 0));
        }

        if (respawnTimer <= 0.0f) {
            StartSuperJump();
        }
    }

    void UpdateSuperJump(float dt) {
        jumpTimer += dt;
        float t = jumpTimer / JUMP_DURATION;

        if (t >= 1.0f) {
            transform->position = jumpTargetPos;
            state = PlayerState::ALIVE;
            if (camera) camera->TriggerShake(0.2f, 0.1f);
            return;
        }

        glm::vec3 currentPos = glm::mix(jumpStartPos, jumpTargetPos, t);
        float heightOffset = 15.0f * sin(t * 3.14159f);
        currentPos.y += heightOffset;

        transform->position = currentPos;
        transform->rotation.y += 720.0f * dt;
        transform->rotation.x = -90.0f * (1.0f - t);
    }

    void HandleInput(float dt) {
        if (!cameraRef) return;

        bool onMyInk = false;
        if (mapFloor && mapObstacle && level) {
            float h = level->GetHeightAt(transform->position.x, transform->position.z);
            SplatMap* target = (h > 0.5f) ? mapObstacle : mapFloor;

            auto uvRes = SplatPhysics::WorldToUV(transform->position, glm::vec3(0), floorSize, floorSize);
            if (uvRes.hit) {
                onMyInk = target->IsColorInArea(uvRes.uv.x, uvRes.uv.y, teamID, 1);
            }
        }

        bool wantSwim = Input::GetKey(GLFW_KEY_LEFT_SHIFT);
        bool nextIsSwimming = (wantSwim && onMyInk);

        if (nextIsSwimming != isSwimming) {
            AudioManager::Instance().PlayOneShot("swim", 0.3f);
        }

        isSwimming = nextIsSwimming;

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

        if (glm::length(targetVel) > 0.0f) {
            targetVel = glm::normalize(targetVel) * currentSpeed;
        }

        if (isSwimming) {
            if (glm::length(targetVel) > 0.0f) {
                RotateTowards(targetVel, 10.0f, dt);
            }
        }
        else {
            RotateTowards(camFwd, 15.0f, dt);
        }

        float friction = 10.0f;
        velocity.x = glm::mix(velocity.x, targetVel.x, friction * dt);
        velocity.z = glm::mix(velocity.z, targetVel.z, friction * dt);

        if (Input::GetKey(GLFW_KEY_SPACE) && isGrounded && !isSwimming) {
            velocity.y = sqrt(2.0f * jumpHeight * abs(gravity));
            isGrounded = false;
        }

        // 射擊邏輯 (優先判斷火箭)

        // 1. 先檢查是否要發射火箭 (不可潛水)
        if (isRocketActive && !isSwimming) {
            if (Input::GetMouseButton(0) && rocketCooldownTimer <= 0.0f && rocketAmmo > 0) {
                // 設定請求旗標，讓外部抓去發送封包
                requestRocketFire = true;

                // 扣除彈藥與設定冷卻
                rocketAmmo--;
                rocketCooldownTimer = 0.6f;

                // 播放音效
                AudioManager::Instance().PlayOneShot("rocket_shot", 0.7f);

                // 如果射完了，結束大招
                if (rocketAmmo <= 0) {
                    DeactivateRocketSpecial();
                }
            }
            return;
        }

        // 普通武器射擊
        if (weapon) {
            bool hasInk = (hudRef && hudRef->currentInk >= weapon->inkCost);
            bool isFiring = Input::GetMouseButton(0) && !isSwimming && hasInk;

            glm::vec3 gunPos = transform->position + glm::vec3(0, 1.5f, 0) + right * 0.5f + front * 0.5f;

            if (weapon->Trigger(dt, gunPos, cameraRef->transform->GetForward(), isFiring)) {
                if (hudRef) hudRef->ConsumeInk(weapon->inkCost);
                camera->TriggerShake(0.1f, 0.05f);
                AudioManager::Instance().PlayOneShot("shoot", 0.3f);

                AddSpecialCharge(5.0f); // 集氣
            }
        }

        if (hasBomb && Input::GetKey(GLFW_KEY_R)) {
            hasBomb = false;
            requestBombThrow = true;
            std::cout << "[Player] Throwing Bomb!" << std::endl;
        }
    }

    void ApplyPhysics(float dt) {
        glm::vec3 nextPos = transform->position;
        nextPos.x += velocity.x * dt;
        nextPos.z += velocity.z * dt;
        float currentH = 0.0f;
        float nextH = 0.0f;

        if (level) {
            currentH = level->GetHeightAt(transform->position.x, transform->position.z);
            nextH = level->GetHeightAt(nextPos.x, nextPos.z);
        }

        float stepHeight = 0.5f;
        if (nextH > currentH + stepHeight) {
            velocity.x = 0;
            velocity.z = 0;
        }
        else {
            transform->position.x = nextPos.x;
            transform->position.z = nextPos.z;
        }

        velocity.y += gravity * dt;
        transform->position += velocity * dt;

        float groundHeight = 0.0f;
        if (level) {
            groundHeight = level->GetHeightAt(transform->position.x, transform->position.z);
        }
        if (transform->position.y < groundHeight) {
            transform->position.y = groundHeight;
            velocity.y = 0;
            isGrounded = true;
        }
        else {
            if (transform->position.y - groundHeight < 0.1f && velocity.y <= 0) {
                transform->position.y = groundHeight;
                isGrounded = true;
            }
            else {
                isGrounded = false;
            }
        }

        if (transform->position.x > mapLimit) transform->position.x = mapLimit;
        if (transform->position.x < -mapLimit) transform->position.x = -mapLimit;
        if (transform->position.z > mapLimit) transform->position.z = mapLimit;
        if (transform->position.z < -mapLimit) transform->position.z = -mapLimit;
    }

    void UpdateInk(float dt) {
        if (!hudRef) return;

        bool isShooting = Input::GetMouseButton(0) && !isSwimming;
        // 如果正在射火箭，也算是在射擊，不回充
        if (isRocketActive && Input::GetMouseButton(0)) isShooting = true;

        if (!isShooting) {
            float refillRate = isSwimming ? 0.5f : 0.1f;
            hudRef->RefillInk(refillRate * dt);
        }
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