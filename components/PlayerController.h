#pragma once
#include "../engine/Component.h"
#include "../engine/GameObject.h"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

class PlayerController : public Component {
public:
    float moveSpeed = 5.0f;
    float jumpHeight = 2.0f;
    float gravity = -9.8f * 3;

    // --- 狀態變數 ---
    glm::vec3 velocity = glm::vec3(0.0f); // 當前的 3D 速度
    bool isGrounded = false;

    // 玩家身高 (眼睛的高度)
    float playerHeight = 2.0f;

    // 場地邊界
    float mapLimit = 19.5f;

    void Update(float dt) override {
        // 這裡只處理物理運動，輸入處理放在 ProcessInput

        // 1. 套用重力 (v = v0 + at)
        velocity.y += gravity * dt;

        // 2. 更新位置 (p = p0 + vt)
        gameObject->transform->position += velocity * dt;

        // 3. 地板碰撞 (Ground Check)
        // 假設地板高度是 0，玩家中心點在地板上時，y = playerHeight
        // 但這裡我們簡化：假設 transform.position 就是腳底板的位置 (需要調整 Camera 位置)
        // 或者假設 transform.position 是眼睛位置 (y=2.0)

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

    // 將輸入處理獨立出來
    void ProcessInput(GLFWwindow* window, float dt, glm::vec3 cameraForward, glm::vec3 cameraRight) {
        // --- 1. 水平移動 (XZ 平面) ---
        // 我們不直接改 position，而是設定水平速度

        // 將相機的前方向量投影到水平面 (去除 Y 軸影響)
        glm::vec3 front = glm::normalize(glm::vec3(cameraForward.x, 0.0f, cameraForward.z));
        glm::vec3 right = glm::normalize(glm::vec3(cameraRight.x, 0.0f, cameraRight.z));

        glm::vec3 targetVelocity = glm::vec3(0.0f);

        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            targetVelocity += front;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            targetVelocity -= front;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            targetVelocity -= right;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            targetVelocity += right;

        // 正規化速度並乘上移動速度
        if (glm::length(targetVelocity) > 0.1f) {
            targetVelocity = glm::normalize(targetVelocity) * moveSpeed;
        }

        // 應用到當前速度 (保留原本的 Y 軸速度，那是重力管的)
        velocity.x = targetVelocity.x;
        velocity.z = targetVelocity.z;

        // --- 2. 跳躍 ---
        // 只有在地板上才能跳
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS && isGrounded) {
            // v^2 = 2gh -> v = sqrt(2gh)
            velocity.y = sqrt(2.0f * jumpHeight * abs(gravity));
            isGrounded = false;
        }
    }
};