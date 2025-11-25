#pragma once
#include "../engine/Component.h"
#include "../engine/GameObject.h"
#include "../engine/core/Input.h"
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

    glm::vec3 inkColor = glm::vec3(1.0f, 0.0f, 0.0f);   // red

    struct ShootRequest {
        glm::vec3 position;
        glm::vec3 direction;
    };
    std::vector<ShootRequest> pendingShots;

    float shootRate = 0.1f;
    float lastShootTime = 0.0f;

    InkShooter(Camera* cam, HUD* h, InkMap* map, unsigned int brushTex)
        : camera(cam), hud(h), inkMap(map), brushTexture(brushTex) {
    }

    void SetColor(glm::vec3 color) {
        inkColor = color;
    }

    void ProcessInput(float dt) {
        if (!camera || !hud) return;

        if (Input::GetMouseButton(0)) {
            float currentTime = (float)glfwGetTime();
            if (currentTime - lastShootTime > shootRate && hud->currentInk > 0) {

                hud->ConsumeInk(0.2f);

                ShootRequest req;
                req.direction = camera->gameObject->transform->GetForward();

                glm::vec3 playerPos = gameObject->transform->position;
                glm::vec3 spawnOffset = glm::vec3(0.0f, 1.5f, 0.0f);
                req.position = playerPos + spawnOffset + (req.direction * 0.8f);

                pendingShots.push_back(req);
                lastShootTime = currentTime;
            }
        }
    }

    void AIShoot(glm::vec3 pos, glm::vec3 dir) {
        ShootRequest req;
        req.position = pos;
        req.direction = dir;
        pendingShots.push_back(req);
    }
};