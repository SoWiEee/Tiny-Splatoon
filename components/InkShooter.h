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
    HUD* hud;
    InkMap* inkMap;
    unsigned int brushTexture;

    struct ShootRequest {
        glm::vec3 position;
        glm::vec3 direction;
    };
    std::vector<ShootRequest> pendingShots;

    float shootRate = 0.1f;
    float lastShootTime = 0.0f;

    // [修改] 建構子移除 cursor 參數
    InkShooter(Camera* cam, HUD* h, InkMap* map, unsigned int brushTex)
        : camera(cam), hud(h), inkMap(map), brushTexture(brushTex) {
    }

    void ProcessInput(GLFWwindow* window, float dt) {
        if (!camera || !hud) return;

        if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
            float currentTime = (float)glfwGetTime();
            if (currentTime - lastShootTime > shootRate && hud->currentInk > 0) {

                hud->ConsumeInk(0.2f);

                ShootRequest req;

                // [修正 1] 射擊方向：直接拿相機的前方向量 (包含上下視角)
                req.direction = camera->gameObject->transform->GetForward();

                // [修正 2] 發射位置：
                glm::vec3 playerPos = gameObject->transform->position;
                glm::vec3 spawnOffset = glm::vec3(0.0f, 1.5f, 0.0f); // 假設槍在 1.5m 高

                req.position = playerPos + spawnOffset + (req.direction * 0.8f);

                pendingShots.push_back(req);
                lastShootTime = currentTime;
            }
        }
    }
};