#pragma once
#include "Component.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

class Transform : public Component {
public:
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f); // Euler Angles (Degrees)
    glm::vec3 scale = glm::vec3(1.0f);
    Transform* parent = nullptr;

    // 設定父節點
    void SetParent(Transform* p) {
        parent = p;
    }

    // Model Matrix
    glm::mat4 GetModelMatrix() {
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, position);
        // 注意旋轉順序 Y -> X -> Z 通常比較穩定
        model = glm::rotate(model, glm::radians(rotation.y), glm::vec3(0, 1, 0));
        model = glm::rotate(model, glm::radians(rotation.x), glm::vec3(1, 0, 0));
        model = glm::rotate(model, glm::radians(rotation.z), glm::vec3(0, 0, 1));
        model = glm::scale(model, scale);

        if (parent != nullptr) {
            return parent->GetModelMatrix() * model;
        }

        return model;
    }

    // [修正] LookAt: 讓物體正面 (-Z) 朝向目標
    void LookAt(glm::vec3 target) {
        glm::vec3 direction = target - position;
        float len = glm::length(direction);
        if (len < 0.001f) return;
        direction /= len; // Normalize

        // [關鍵修正] 使用 atan2(x, -z) 來符合 OpenGL 座標系
        // 當 direction 為 (0,0,-1) 時，x=0, -z=1 -> atan2(0,1) = 0度 -> 正確！
        float yaw = glm::degrees(atan2(direction.x, -direction.z));

        // 計算 Pitch
        float pitch = glm::degrees(asin(direction.y));

        rotation.x = -pitch; // 反轉 Pitch 以符合 OpenGL
        rotation.y = yaw;
        rotation.z = 0.0f;
    }

    // [修正] GetForward: 0度 對應 -Z 軸
    glm::vec3 GetForward() {
        glm::vec3 front;
        float radY = glm::radians(rotation.y);
        float radX = glm::radians(rotation.x);

        // [關鍵數學修正]
        // sin(0) = 0, -cos(0) = -1 -> (0, 0, -1) 正前方！
        front.x = sin(radY) * cos(radX);
        front.y = sin(radX);
        front.z = -cos(radY) * cos(radX);

        return glm::normalize(front);
    }

    glm::vec3 GetRight() {
        return glm::normalize(glm::cross(GetForward(), glm::vec3(0, 1, 0)));
    }

    glm::vec3 GetUp() {
        return glm::normalize(glm::cross(GetRight(), GetForward()));
    }
};