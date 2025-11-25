#pragma once
#include "../engine/Component.h"
#include "../engine/GameObject.h"
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Camera : public Component {
public:
    glm::mat4 projection;
    float fov = 45.0f;
    float speed = 5.0f;
    float sensitivity = 0.1f;

    float lastX = 640, lastY = 360;
    bool firstMouse = true;

    float aspectRatio = 1280.0f / 720.0f;

    void Start() override {
        projection = glm::perspective(glm::radians(fov), aspectRatio, 0.1f, 100.0f);
    }

    void Update(float dt) override {
        // 這裡通常會需要傳入 Window 指標來讀取 Input
    }

    glm::mat4 GetViewMatrix() {
        return glm::lookAt(gameObject->transform->position,
            gameObject->transform->position + gameObject->transform->GetForward(),
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