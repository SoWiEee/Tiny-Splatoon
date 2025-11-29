#pragma once
#include "Component.h"
#include <glm/gtc/matrix_transform.hpp>

class Transform : public Component {
public:
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotation = glm::vec3(0.0f); // Euler Angles
    glm::vec3 scale = glm::vec3(1.0f);

    // odel Matrix
    glm::mat4 GetModelMatrix() {
        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, position);
        model = glm::rotate(model, glm::radians(rotation.x), glm::vec3(1, 0, 0));
        model = glm::rotate(model, glm::radians(rotation.y), glm::vec3(0, 1, 0));
        model = glm::rotate(model, glm::radians(rotation.z), glm::vec3(0, 0, 1));
        model = glm::scale(model, scale);
        return model;
    }

    void LookAt(glm::vec3 target) {
        glm::vec3 direction = target - position;
        float len = glm::length(direction);

        // 防呆：如果重疊，不做任何事
        if (len < 0.001f) return;

        direction /= len; // Normalize

        // 計算 Yaw (Y軸旋轉)
        // atan2(x, z) 算出平面角度
        // 注意：根據你的模型預設朝向 (+Z 或 -Z)，這裡可能需要 +180.0f
        float yaw = glm::degrees(atan2(direction.x, direction.z));

        // 計算 Pitch (X軸旋轉)
        // asin(y) 算出仰角
        float pitch = glm::degrees(asin(direction.y));

        // 更新旋轉
        // 通常 OpenGL Pitch 向上是負角度，這裡設為 -pitch
        rotation.x = -pitch;
        rotation.y = yaw;
        rotation.z = 0.0f; // 暫不考慮 Roll
    }

    glm::vec3 GetForward() {
        glm::vec3 front;
        front.x = cos(glm::radians(rotation.y)) * cos(glm::radians(rotation.x));
        front.y = sin(glm::radians(rotation.x));
        front.z = sin(glm::radians(rotation.y)) * cos(glm::radians(rotation.x));
        return glm::normalize(front);
    }

    glm::vec3 GetRight() {
        return glm::normalize(glm::cross(GetForward(), glm::vec3(0, 1, 0)));
    }
};