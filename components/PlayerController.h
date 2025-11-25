#pragma once
#include "../engine/Component.h"
#include "../engine/GameObject.h"
#include "../engine/core/Input.h"
#include "../splat/SplatMap.h"
#include "HUD.h"
#include <glm/glm.hpp>

class PlayerController : public Component {
public:
    // --- 砞﹚把计 (Settings) ---
    float MoveSpeed = 5.0f;
    float SwimSpeed = 10.0f;
    float JumpHeight = 2.0f;
    float Gravity = -25.0f;

    // State
    glm::vec3 Velocity = glm::vec3(0.0f);
    bool IsGrounded = false;
    bool IsSwimming = false;
    int TeamID = 1;

    // reference
    SplatMap* MapRef = nullptr;
    GameObject* VisualBody = nullptr;
    HUD* HudRef = nullptr;

    // --- 初春把计 ---
    float MapLimit = 19.5f;
    float FloorSize = 40.0f;

    void Setup(SplatMap* map, GameObject* body, int team, HUD* hud) {
        MapRef = map;
        VisualBody = body;
        TeamID = team;
        HudRef = hud;
    }

    void Update(float dt) override {
        ApplyPhysics(dt);
        ConstrainBounds();
    }

    void ProcessInput(float dt, glm::vec3 camForward, glm::vec3 camRight) {
        UpdateState();  // on ink
        HandleMovement(camForward, camRight);
        HandleActions();    // jump, swim
        UpdateVisuals(dt);
    }

private:
    void UpdateState() {
        float u = (gameObject->transform->position.x + FloorSize / 2.0f) / FloorSize;
        float v = 1.0f - ((gameObject->transform->position.z + FloorSize / 2.0f) / FloorSize);

        bool onMyInk = false;
        if (MapRef) {
            onMyInk = MapRef->IsColorInArea(u, v, TeamID, 1);
        }

        bool wantSwim = Input::GetKey(GLFW_KEY_LEFT_SHIFT);
        IsSwimming = (wantSwim && onMyInk);
    }

    void HandleMovement(glm::vec3 camFwd, glm::vec3 camRight) {
        float currentSpeed = IsSwimming ? SwimSpeed : MoveSpeed;
        glm::vec3 front = glm::normalize(glm::vec3(camFwd.x, 0.0f, camFwd.z));
        glm::vec3 right = glm::normalize(glm::vec3(camRight.x, 0.0f, camRight.z));

        glm::vec3 targetVel = glm::vec3(0.0f);

        if (Input::GetKey(GLFW_KEY_W)) targetVel += front;
        if (Input::GetKey(GLFW_KEY_S)) targetVel -= front;
        if (Input::GetKey(GLFW_KEY_A)) targetVel -= right;
        if (Input::GetKey(GLFW_KEY_D)) targetVel += right;

        if (glm::length(targetVel) > 0.1f) {
            targetVel = glm::normalize(targetVel) * currentSpeed;
        }

        Velocity.x = targetVel.x;
        Velocity.z = targetVel.z;
    }

    void HandleActions() {
        if (Input::GetKey(GLFW_KEY_SPACE) && IsGrounded && !IsSwimming) {
            Velocity.y = sqrt(2.0f * JumpHeight * abs(Gravity));
            IsGrounded = false;
        }
    }

    void UpdateVisuals(float dt) {
        if (HudRef) {
            float refillRate = IsSwimming ? 0.5f : 0.05f;
            HudRef->RefillInk(refillRate * dt);
        }
        if (VisualBody) {
            if (IsSwimming) {
                VisualBody->transform->scale = glm::vec3(0.6f, 0.1f, 0.6f);
                VisualBody->transform->position = gameObject->transform->position + glm::vec3(0, 0.1f, 0);
            }
            else {
                VisualBody->transform->scale = glm::vec3(0.5f, 1.8f, 0.5f);
                VisualBody->transform->position = gameObject->transform->position + glm::vec3(0, 0.9f, 0);
            }
            VisualBody->transform->rotation = gameObject->transform->rotation;
        }
    }

    void ApplyPhysics(float dt) {
        Velocity.y += Gravity * dt;
        gameObject->transform->position += Velocity * dt;

        if (gameObject->transform->position.y < 0.0f) {
            gameObject->transform->position.y = 0.0f;
            Velocity.y = 0;
            IsGrounded = true;
        }
        else {
            IsGrounded = false;
        }
    }

    void ConstrainBounds() {
        glm::vec3& pos = gameObject->transform->position;
        if (pos.x > MapLimit) pos.x = MapLimit;
        if (pos.x < -MapLimit) pos.x = -MapLimit;
        if (pos.z > MapLimit) pos.z = MapLimit;
        if (pos.z < -MapLimit) pos.z = -MapLimit;
    }
};