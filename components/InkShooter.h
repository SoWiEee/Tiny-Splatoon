// Source/Components/InkShooter.h
#pragma once
#include "../Engine/Component.h"
#include "../Engine/GameObject.h"
#include "Camera.h"
#include <GLFW/glfw3.h>
#include <iostream>

class InkShooter : public Component {
public:
    Camera* camera;
    GameObject* debugCursor;

    float floorSize = 20.0f;

    InkShooter(Camera* cam, GameObject* cursor) : camera(cam), debugCursor(cursor) {}

    void Update(float dt) override {
        // 這裡只處理邏輯，輸入處理我們拉出來寫，比較乾淨
    }

    // 處理射擊與瞄準邏輯
    void ProcessInput(GLFWwindow* window) {
        if (!camera || !debugCursor) return;

        // 1. 取得射線資訊
        glm::vec3 rayOrigin = camera->gameObject->transform->position;
        glm::vec3 rayDir = camera->gameObject->transform->GetForward();

        // 2. 計算射線與平面 (y=0) 的交點
        // 平面公式: P dot N = d (我們假設平面在 y=0, Normal=(0,1,0))
        // 射線公式: R(t) = Origin + t * Dir
        // 解 t:
        // (Origin.y + t * Dir.y) = 0
        // t = -Origin.y / Dir.y

        // 避免除以 0 (如果幾乎水平看出去)
        if (abs(rayDir.y) < 0.001f) return;

        float t = -rayOrigin.y / rayDir.y;

        // t > 0 代表交點在攝影機前方
        if (t > 0) {
            // 3. 算出世界座標擊中點
            glm::vec3 hitPoint = rayOrigin + rayDir * t;

            // 4. 邊界檢查 (是不是還在地板範圍內?)
            // 地板是從 -10 到 +10 (因為 Scale=20, 中心在0)
            float halfSize = floorSize / 2.0f;
            if (hitPoint.x >= -halfSize && hitPoint.x <= halfSize &&
                hitPoint.z >= -halfSize && hitPoint.z <= halfSize)
            {
                // 擊中地板了！

                // A. 更新 Debug Cursor 位置，讓我們看得到
                debugCursor->transform->position = hitPoint;

                // B. 算出 UV 座標 (之後畫圖用)
                // world X: -10 ~ 10  ->  UV u: 0 ~ 1
                float u = (hitPoint.x + halfSize) / floorSize;
                float v = (hitPoint.z + halfSize) / floorSize;

                // 按下左鍵射擊 (模擬)
                if (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS) {
                    std::cout << "Splat! Hit at: " << hitPoint.x << ", " << hitPoint.z
                        << " | UV: " << u << ", " << v << std::endl;

                    // TODO (Phase 3): 在這裡呼叫 FBO 繪圖函式
                }
            }
        }
    }
};