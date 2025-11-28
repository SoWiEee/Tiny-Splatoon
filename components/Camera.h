#pragma once
#include "../engine/Component.h"
#include "../engine/GameObject.h"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <cstdlib>

class Camera : public Component {
public:
    glm::mat4 projection;
    float fov = 45.0f;
    float speed = 5.0f;
    float sensitivity = 0.1f;

    float lastX = 640, lastY = 360;
    bool firstMouse = true;

    float aspectRatio = 1280.0f / 720.0f;

    // 震動相關變數
    float shakeTimer = 0.0f;       // 震動剩餘時間
    float shakeMagnitude = 0.0f;   // 震動強度
    glm::vec3 shakeOffset = glm::vec3(0.0f); // 當前幀的震動偏移量

    void Start() override {
        projection = glm::perspective(glm::radians(fov), aspectRatio, 0.1f, 100.0f);
    }

    void Update(float dt) override {
        if (shakeTimer > 0.0f) {
            shakeTimer -= dt;

            // 產生隨機偏移 (-1.0 ~ 1.0 之間 * 幅度)
            // 這裡簡單用 rand()，如果想要更平滑可以用 Perlin Noise，但在射擊遊戲中隨機抖動很有打擊感
            float offsetX = ((rand() % 100) / 50.0f - 1.0f) * shakeMagnitude;
            float offsetY = ((rand() % 100) / 50.0f - 1.0f) * shakeMagnitude;
            float offsetZ = ((rand() % 100) / 50.0f - 1.0f) * shakeMagnitude;

            shakeOffset = glm::vec3(offsetX, offsetY, offsetZ);

            // (選用) 讓震動隨著時間慢慢變弱，感覺比較自然
            // shakeMagnitude = glm::max(0.0f, shakeMagnitude - (dt * shakeMagnitude * 5.0f)); 
        }
        else {
            shakeTimer = 0.0f;
            shakeOffset = glm::vec3(0.0f);
        }
    }

    // [新增] 外部呼叫此函式來觸發震動
    // duration: 持續幾秒 (例如 0.1)
    // magnitude: 震動多大 (例如 0.2)
    void TriggerShake(float duration, float magnitude) {
        shakeTimer = duration;
        shakeMagnitude = magnitude;
    }

    glm::mat4 GetViewMatrix() {
        // 算出 "震動後" 的位置
        // 注意：這裡不改變 gameObject->transform->position，只改變回傳給 Shader 的矩陣
        glm::vec3 finalPos = gameObject->transform->position + shakeOffset;

        return glm::lookAt(finalPos,
            finalPos + gameObject->transform->GetForward(),
            glm::vec3(0, 1, 0));
    }

    glm::mat4 GetProjectionMatrix() {
        projection = glm::perspective(glm::radians(fov), aspectRatio, 0.1f, 100.0f);
        return projection;
    }

    void ProcessMouseMovement(float xpos, float ypos) {
        if (firstMouse) { lastX = xpos; lastY = ypos; firstMouse = false; }

        float xoffset = xpos - lastX;
        float yoffset = lastY - ypos;
        lastX = xpos;
        lastY = ypos;

        xoffset *= sensitivity;
        yoffset *= sensitivity;

        glm::vec3& rot = gameObject->transform->rotation;
        rot.y += xoffset; // Yaw
        rot.x += yoffset; // Pitch

        if (rot.x > 89.0f) rot.x = 89.0f;
        if (rot.x < -89.0f) rot.x = -89.0f;
    }
};