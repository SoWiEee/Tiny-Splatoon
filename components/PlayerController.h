#pragma once
#include "../engine/Component.h"
#include "../engine/GameObject.h"
#include "../graphics/InkMap.h"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

class PlayerController : public Component {
public:
    float moveSpeed = 5.0f;
    float jumpHeight = 2.0f;
    float gravity = -9.8f * 3;
    float runSpeed = 5.0f;
    float swimSpeed = 10.0f; 
    InkMap* inkMap;        // 用來查顏色
    GameObject* visualBody;
    int myTeamID = 1;      // 紅隊
    float floorSize = 40.0f;
    bool isSwimming = false;
    glm::vec3 velocity = glm::vec3(0.0f); // 當前的 3D 速度
    bool isGrounded = false;
    float playerHeight = 2.0f;
    float mapLimit = 19.5f;

    void Setup(InkMap* map, GameObject* body, int team) {
        inkMap = map;
        visualBody = body;
        myTeamID = team;
    }

    void Update(float dt) override {
        velocity.y += gravity * dt;
        gameObject->transform->position += velocity * dt;

        // Ground Check
        if (gameObject->transform->position.y < playerHeight) {
            gameObject->transform->position.y = playerHeight; // 修正位置
            velocity.y = 0; // 落地後垂直速度歸零
            isGrounded = true;
        }
        else {
            isGrounded = false;
        }

        // 4. 牆壁限制 (Boundary Check)
        glm::vec3& pos = gameObject->transform->position;
        if (pos.x > mapLimit) pos.x = mapLimit;
        if (pos.x < -mapLimit) pos.x = -mapLimit;
        if (pos.z > mapLimit) pos.z = mapLimit;
        if (pos.z < -mapLimit) pos.z = -mapLimit;
    }

    void ProcessInput(GLFWwindow* window, float dt, glm::vec3 cameraForward, glm::vec3 cameraRight) {
        // 1. 計算 UV
        float u = (gameObject->transform->position.x + floorSize / 2.0f) / floorSize;
        float v = 1.0f - ((gameObject->transform->position.z + floorSize / 2.0f) / floorSize);

        // 2. [修改] 使用寬容檢查
        // 不再只讀取一個點，而是問 Map: "我腳下這附近有沒有我的顏色?"
        // radius 傳 1 或 2，代表檢查周圍 3x3 或 5x5 的格子
        bool onMyInk = false;
        if (inkMap) {
            onMyInk = inkMap->IsColorInArea(u, v, myTeamID, 1);
        }

        // 3. 潛水判定
        bool wantSwim = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS);

        // 只要 onMyInk 為真，就不會彈起來
        if (wantSwim && onMyInk) {
            isSwimming = true;
        }
        else {
            isSwimming = false;
        }

        // 4. 根據狀態改變行為
        float currentSpeed = isSwimming ? swimSpeed : runSpeed;

        // 視覺開關：潛水時隱藏身體 (或者壓扁)
        if (visualBody) {
            if (isSwimming) {
                // 潛水：壓扁在地上
                visualBody->transform->scale = glm::vec3(0.5f, 0.1f, 0.5f);
                visualBody->transform->position = gameObject->transform->position - glm::vec3(0, 0.8f, 0); // 貼地
            }
            else {
                // 站立：恢復原狀
                visualBody->transform->scale = glm::vec3(0.5f, 1.8f, 0.5f); // 假設人是長條形
                visualBody->transform->position = gameObject->transform->position;
            }
        }

        // 潛水時通常不能跳躍，這裡先保留跳躍
        glm::vec3 front = glm::normalize(glm::vec3(cameraForward.x, 0.0f, cameraForward.z));
        glm::vec3 right = glm::normalize(glm::vec3(cameraRight.x, 0.0f, cameraRight.z));
        glm::vec3 targetVelocity = glm::vec3(0.0f);

        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) targetVelocity += front;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) targetVelocity -= front;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) targetVelocity -= right;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) targetVelocity += right;

        if (glm::length(targetVelocity) > 0.1f) {
            targetVelocity = glm::normalize(targetVelocity) * currentSpeed;
        }
        velocity.x = targetVelocity.x;
        velocity.z = targetVelocity.z;

        // 跳躍
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS && isGrounded && !isSwimming) {
            velocity.y = sqrt(2.0f * jumpHeight * abs(gravity));
            isGrounded = false;
        }
    }
};