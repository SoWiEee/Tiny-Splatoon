#pragma once

#include "../scene/Entity.h"
#include "../engine/core/Input.h"
#include "../components/MeshRenderer.h"
#include "../components/Health.h"
#include "Weapon.h"
#include "ShooterWeapon.h"
#include "ShotgunWeapon.h"
#include "BowWeapon.h"
#include "../splat/SplatMap.h"

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

    // reference
    Weapon* weapon = nullptr;
    SplatMap* splatMapRef;
    GameObject* cameraRef;
    GameObject* shadow;
    GameObject* visualBody;
    HUD* hudRef = nullptr;

    float mapLimit = 19.5f;
    float floorSize = 40.0f;

    Player(glm::vec3 startPos, int team, SplatMap* map, GameObject* cam, HUD* hud)
        : Entity("Player"), splatMapRef(map), cameraRef(cam), hudRef(hud)
    {
        this->teamID = team;
        shadow = new GameObject("ShadowBlob");
        shadow->AddComponent<MeshRenderer>("Plane", glm::vec3(0.0f, 0.0f, 0.0f));
        shadow->transform->position = transform->position + glm::vec3(0, 0.02f, 0);
        shadow->transform->scale = glm::vec3(1.2f, 1.0f, 1.2f);

        transform->position = startPos;
        weapon = new ShotgunWeapon(team, (team == 1) ? glm::vec3(1, 0, 0) : glm::vec3(0, 1, 0));
        AddComponent<Health>(team, startPos);
        visualBody = new GameObject("PlayerBody");
        visualBody->AddComponent<MeshRenderer>("Cube", weapon->inkColor);
    }

    ~Player() {
        if (weapon) delete weapon;
    }

    void EquipWeapon(Weapon* newWeapon) {
        if (weapon) delete weapon;
        weapon = newWeapon;
    }

    // ¥DÅÞ¿è§ó·s
    void UpdateLogic(float dt) {
        HandleInput(dt);
        ApplyPhysics(dt);
        UpdateVisuals(dt);
    }

    GameObject* GetVisualBody() { return visualBody; }

private:
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