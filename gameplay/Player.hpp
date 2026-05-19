#pragma once

#include "../scene/Entity.hpp"
#include "../engine/core/Input.hpp"
#include "../engine/audio/AudioManager.hpp"
#include "../components/MeshRenderer.hpp"
#include "../components/Health.hpp"
#include "../components/Camera.hpp"
#include "../scene/Level.hpp"
#include "Weapon.hpp"
#include "ShooterWeapon.hpp"
#include "BrushWeapon.hpp"
#include "SlosherWeapon.hpp"
#include "../splat/SplatMap.hpp"
#include <memory>

enum class PlayerState {
    ALIVE,      // 甇?虜?
    DEAD,       // 甇颱滿 (蝑???)
    LAUNCHING,  // 頞?頝唾??脣銝?
    SHARKING    // 攳??????
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
    float const RESPAWN_TIME = 4.0f; // 甇颱滿敺?4 蝘???

    // --- ????---
    bool hasBomb = false;
    bool requestBombThrow = false;

    // --- ?怎悌憭扳? (Tri-zooka) ???---
    bool isRocketActive = false;    // ?臬甇?憭扳?璅∪?
    int rocketAmmo = 0;             // ?拚??澆?甈⊥ (3甈?
    float rocketCooldownTimer = 0.0f; // 撠???
    float rocketDurationTimer = 0.0f; // 憭扳?????
    bool requestRocketFire = false;   // [隢?] ?澆??怎悌 (蝯?NetworkManager ??

    // --- 攳???? ---
    int sharkDashCount = 0;         // ?拚?銵甈⊥ (蝮賢 3 甈?
    bool isSharkDashing = false;    // ?桀??臬???箔葉???胯???皞葉??
    float sharkStateTimer = 0.0f;   // ?梁閮???(銵? ?????)

    // ?閮剖?
    float sharkDashDuration = 1.0f;
    float sharkPauseDuration = 0.7f;
    float sharkSpeed = 13.0f;        // 銵?漲 (閬翰銝暺???
    float sharkInkTimer = 0.0f;      // ?游◢閮???
    glm::vec3 sharkDashDirection = glm::vec3(0, 0, 1);
    bool requestSharkSpray = false;
    bool requestSharkExplode = false;

    bool requestLaser = false;
    float currentCharge = 0.0f;       // ?嗅??賡?
    float const MAX_SPECIAL = 100.0f; // ?賡?銝?

    // 頞?頝唾??
    glm::vec3 jumpStartPos;
    glm::vec3 jumpTargetPos;
    float jumpTimer = 0.0f;
    float const JUMP_DURATION = 1.5f; // 頝唾?憌???

    // reference
    std::unique_ptr<Weapon> weapon;
    SplatMap* mapFloor = nullptr;
    SplatMap* mapObstacle = nullptr;
    GameObject* cameraRef;
    GameObject* shadow;
    GameObject* visualBody;
    HUD* hudRef = nullptr;
    Camera* camera = nullptr;
    GameObject* visualHuman = nullptr;
    GameObject* visualSquid = nullptr;

    float healRateSlow = 10.0f;      // 蝡??? (瞍恍)
    float healRateFast = 20.0f;      // 瞏偌?? (敹恍?
    float regenDelay = 2.0f;         // ?敺?蝑?2 蝘??賡?憪?銵
    float currentRegenDelay = 0.0f;  // 閮???
    float mapLimit = 39.5f;
    float floorSize = 80.0f;

    Player(glm::vec3 startPos, int team, SplatMap* floor, SplatMap* obstacle, GameObject* cam, HUD* hud, Level* mapLevel)
        : Entity("Player"), mapFloor(floor), mapObstacle(obstacle), cameraRef(cam), hudRef(hud), level(mapLevel)
    {
        this->teamID = team;
        transform->position = startPos;

        if (cam) camera = cam->GetComponent<Camera>();

        AddComponent<Health>(team, startPos);

        visualBody = nullptr;
        shadow = nullptr;

        StartSuperJump();
    }

    void EquipWeapon(std::unique_ptr<Weapon> newWeapon) {
        weapon = std::move(newWeapon);
    }

    // 銝駁?頛舀??
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

            // ?? Q ?萇?之??(?ㄐ蝷箇??拍車憭扳?嚗??臭誑?芸楛瘙箏?閬?芯?蝔?
            if (Input::GetKey(GLFW_KEY_Q) && IsSpecialReady()) {
                // 憒?雿?券?擳? StartSpecialShark();
                // 憒?雿?函蝞哨?
                ActivateRocketSpecial();

                // ???賡?
                currentCharge = 0.0f;
            }

            if (level && mapFloor && mapObstacle) {

                // 1. 閮? UV 摨扳?
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

                // --- ?瑕拿??銵?摩 ---
                auto healthComp = GetComponent<Health>();
                if (healthComp) {
                    if (!onEnemyInk && currentRegenDelay > 0.0f) {
                        currentRegenDelay -= dt;
                    }
                    else {
                        // ?寞??臬瞏偌瘙箏????漲
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
        // ?脫迫??甇颱滿
        if (state == PlayerState::DEAD || state == PlayerState::LAUNCHING) return;

        state = PlayerState::DEAD;
        respawnTimer = RESPAWN_TIME;

        // 甇颱滿??蝵桀之????
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
        jumpTargetPos = glm::vec3(0, 0.0f, 30.0f * zDir); // ?賢暺?

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

    // ?怎悌憭扳??摩
    void ActivateRocketSpecial() {
        if (isRocketActive) return;

        isRocketActive = true;
        rocketAmmo = 3;
        rocketDurationTimer = 6.0f;

        AudioManager::Instance().PlayOneShot("shark", 0.6f);    // ?剜???單?
        std::cout << ">>> ROCKET LAUNCHER EQUIPPED! (Ammo: 3) <<<" << std::endl;
    }

    void UpdateRocketState(float dt) {
        if (!isRocketActive) return;

        // 1. ?閮?
        rocketDurationTimer -= dt;
        if (rocketDurationTimer <= 0.0f) {
            DeactivateRocketSpecial();
            return;
        }

        // 2. 撠??瑕?
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
        visualBody = human;
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
        jumpStartPos = glm::vec3(0, 15.0f, 40.0f * zDir); // 擃征??暺?
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

        // 撠??摩 (?芸??斗?怎悌)

        // 1. ?炎?交?西??澆??怎悌 (銝瞏偌)
        if (isRocketActive && !isSwimming) {
            if (Input::GetMouseButton(0) && rocketCooldownTimer <= 0.0f && rocketAmmo > 0) {
                // 閮剖?隢???嚗?憭??潮???
                requestRocketFire = true;

                // ??敶?身摰??
                rocketAmmo--;
                rocketCooldownTimer = 0.6f;

                // ?剜?單?
                AudioManager::Instance().PlayOneShot("rocket_shot", 0.7f);

                // 憒?撠?鈭?蝯?憭扳?
                if (rocketAmmo <= 0) {
                    DeactivateRocketSpecial();
                }
            }
            return;
        }

        // ?桅郎?典???
        if (weapon) {
            bool hasInk = (hudRef && hudRef->currentInk >= weapon->inkCost);
            bool isFiring = Input::GetMouseButton(0) && !isSwimming && hasInk;

            glm::vec3 gunPos = transform->position + glm::vec3(0, 1.5f, 0) + right * 0.5f + front * 0.5f;

            if (weapon->Trigger(dt, gunPos, cameraRef->transform->GetForward(), isFiring)) {
                if (hudRef) hudRef->ConsumeInk(weapon->inkCost);
                camera->TriggerShake(0.1f, 0.05f);
                AudioManager::Instance().PlayOneShot("shoot", 0.2f);

                AddSpecialCharge(2.0f); // ?除
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
        // 憒?甇?撠蝞哨?銋??臬撠?嚗???
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
