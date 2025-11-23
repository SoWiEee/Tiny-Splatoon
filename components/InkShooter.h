#pragma once
#include "../engine/Component.h"
#include "../engine/GameObject.h"
#include "Camera.h"
#include "HUD.h"
#include "graphics/InkMap.h"
#include <GLFW/glfw3.h>
#include <iostream>

class InkShooter : public Component {
public:
    Camera* camera;
    GameObject* debugCursor;
    HUD* hud;

    float floorSize = 20.0f;
    float shootRate = 0.1f;
    float lastShootTime = 0.0f;
    InkMap* inkMap;             // 畫布
    unsigned int brushTexture;  // 筆刷貼圖 ID
    // 發射請求佇列
    struct ShootRequest {
        glm::vec3 position;
        glm::vec3 direction;
    };
    std::vector<ShootRequest> pendingShots; // 待處理的射擊請求

    InkShooter(Camera* cam, GameObject* cursor, HUD* h, InkMap* map, unsigned int brushTex)
        : camera(cam), debugCursor(cursor), hud(h), inkMap(map), brushTexture(brushTex) {
    }

    void ProcessInput(GLFWwindow* window, float dt) {
        if (!camera || !debugCursor || !hud) return;

        // 狀態 1: 按下左鍵 且 墨水 > 0 -> 射擊
        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
            // 射擊頻率控制
            float currentTime = (float)glfwGetTime();
            if (currentTime - lastShootTime > shootRate && hud->currentInk > 0) {

                hud->ConsumeInk(0.2f); // 消耗墨水

                ShootRequest req;
                req.position = gameObject->transform->position; // 槍的位置(相機位置)
                req.direction = gameObject->transform->GetForward(); // 槍的朝向

                // 稍微把發射點往前移一點，不要跟相機重疊
                req.position += req.direction * 1.0f;

                pendingShots.push_back(req);

                lastShootTime = currentTime;
            }
        }
        else {
            hud->RefillInk();
        }
    }

private:
    void PerformRaycast(GLFWwindow* window) {
        glm::vec3 rayOrigin = camera->gameObject->transform->position;
        glm::vec3 rayDir = camera->gameObject->transform->GetForward();

        if (abs(rayDir.y) < 0.001f) return;

        float t = -rayOrigin.y / rayDir.y;

        if (t > 0) {
            glm::vec3 hitPoint = rayOrigin + rayDir * t;

            float halfSize = floorSize / 2.0f;
            if (hitPoint.x >= -halfSize && hitPoint.x <= halfSize &&
                hitPoint.z >= -halfSize && hitPoint.z <= halfSize)
            {
                debugCursor->transform->position = hitPoint;

                float u = (hitPoint.x + halfSize) / floorSize;
                float v = (hitPoint.z + halfSize) / floorSize;

                inkMap->Paint(glm::vec2(u, v), 0.08f, glm::vec3(1.0f, 0.0f, 0.0f), brushTexture);
            }
        }
    }
};