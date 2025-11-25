#pragma once
#include "../engine/Component.h"
#include "../engine/GameObject.h"
#include "../graphics/InkMap.h"
#include "../engine/core/Input.h"
#include "HUD.h"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

class PlayerController : public Component {
public:
    // parameters
    float moveSpeed = 5.0f;
    float runSpeed = 5.0f; 
    float swimSpeed = 10.0f;
    float jumpHeight = 2.0f;
    float gravity = -9.8f * 2.5f;

    // refill
    float refillSpeedSwim = 0.5f;
    float refillSpeedStand = 0.1f;

    // state
    glm::vec3 velocity = glm::vec3(0.0f);
    bool isGrounded = false;
    bool isSwimming = false;

    // external
    InkMap* inkMap;
    GameObject* visualBody;
    HUD* hud;
    int myTeamID = 1;   // red team

    float playerHeight = 2.0f;
    float floorSize = 40.0f;
    float mapLimit = 19.5f;

    void Setup(InkMap* map, GameObject* body, int team, HUD* h) {
        inkMap = map;
        visualBody = body;
        myTeamID = team;
        hud = h;
    }

    void Update(float dt) override {
        velocity.y += gravity * dt;
        gameObject->transform->position += velocity * dt;

        // ground check
        if (gameObject->transform->position.y < playerHeight) {
            gameObject->transform->position.y = playerHeight;
            velocity.y = 0;
            isGrounded = true;
        }
        else {
            isGrounded = false;
        }

        // boundary check
        glm::vec3& pos = gameObject->transform->position;
        if (pos.x > mapLimit) pos.x = mapLimit;
        if (pos.x < -mapLimit) pos.x = -mapLimit;
        if (pos.z > mapLimit) pos.z = mapLimit;
        if (pos.z < -mapLimit) pos.z = -mapLimit;
    }

    void ProcessInput(float dt, glm::vec3 cameraForward, glm::vec3 cameraRight) {
        // uv
        float u = (gameObject->transform->position.x + floorSize / 2.0f) / floorSize;
        float v = 1.0f - ((gameObject->transform->position.z + floorSize / 2.0f) / floorSize);

        bool onMyInk = false;
        if (inkMap) {
            onMyInk = inkMap->IsColorInArea(u, v, myTeamID, 1);
        }

        // swim check
        bool wantSwim = Input::GetKey(GLFW_KEY_LEFT_SHIFT);

        if (wantSwim && onMyInk) {
            isSwimming = true;
        }
        else {
            isSwimming = false;
        }

        // ink refill
        if (hud) {
            if (isSwimming) hud->RefillInk(refillSpeedSwim * dt);
            else hud->RefillInk(refillSpeedStand * dt);
        }

        // 視覺變換
        if (visualBody) {
            if (isSwimming) {
                visualBody->transform->scale = glm::vec3(0.5f, 0.1f, 0.5f);
                visualBody->transform->position = gameObject->transform->position - glm::vec3(0, 0.8f, 0);
            }
            else {
                visualBody->transform->scale = glm::vec3(0.5f, 1.8f, 0.5f);
                visualBody->transform->position = gameObject->transform->position;
            }
        }

        float currentSpeed = isSwimming ? swimSpeed : runSpeed;

        // 將相機方向投影到水平面
        glm::vec3 front = glm::normalize(glm::vec3(cameraForward.x, 0.0f, cameraForward.z));
        glm::vec3 right = glm::normalize(glm::vec3(cameraRight.x, 0.0f, cameraRight.z));

        glm::vec3 targetVelocity = glm::vec3(0.0f);

        if (Input::GetKey(GLFW_KEY_W)) targetVelocity += front;
        if (Input::GetKey(GLFW_KEY_S)) targetVelocity -= front;
        if (Input::GetKey(GLFW_KEY_A)) targetVelocity -= right;
        if (Input::GetKey(GLFW_KEY_D)) targetVelocity += right;

        if (glm::length(targetVelocity) > 0.1f) {
            targetVelocity = glm::normalize(targetVelocity) * currentSpeed;
        }
        velocity.x = targetVelocity.x;
        velocity.z = targetVelocity.z;

        // jump
        if (Input::GetKey(GLFW_KEY_SPACE) && isGrounded && !isSwimming) {
            velocity.y = sqrt(2.0f * jumpHeight * abs(gravity));
            isGrounded = false;
        }
    }
};