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

    // 射擊請求結構
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
                req.position = gameObject->transform->position;
                req.direction = gameObject->transform->GetForward();
                req.position += req.direction * 1.0f; // 稍微往前一點生成

                pendingShots.push_back(req);
                lastShootTime = currentTime;
            }
        }
        else {
            hud->RefillInk();
        }
    }
};