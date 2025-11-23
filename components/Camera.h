#pragma once
#include "../engine/Component.h"
#include "../engine/GameObject.h"
#include <GLFW/glfw3.h>

class Camera : public Component {
public:
    glm::mat4 projection;
    float fov = 45.0f;
    float speed = 5.0f;
    float sensitivity = 0.1f;

    float lastX = 640, lastY = 360;
    bool firstMouse = true;

    void Start() override {
        projection = glm::perspective(glm::radians(fov), 1280.0f / 720.0f, 0.1f, 100.0f);
    }

    void Update(float dt) override {
        // 這裡通常會需要傳入 Window 指標來讀取 Input，
        // 為求簡化，假設我們透過全域變數或 InputManager 取得輸入
        // 這裡僅示範 View Matrix 計算
    }

    glm::mat4 GetViewMatrix() {
        return glm::lookAt(gameObject->transform->position,
            gameObject->transform->position + gameObject->transform->GetForward(),
            glm::vec3(0, 1, 0));
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

    void ProcessKeyboard(GLFWwindow* window, float dt) {
        Transform* t = gameObject->transform;
        float velocity = speed * dt;
        if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)
            t->position += t->GetForward() * velocity;
        if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)
            t->position -= t->GetForward() * velocity;
        if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)
            t->position -= t->GetRight() * velocity;
        if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)
            t->position += t->GetRight() * velocity;
        // 空白鍵上升，Shift下降
        if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)
            t->position.y += velocity;
        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
            t->position.y -= velocity;
    }
};