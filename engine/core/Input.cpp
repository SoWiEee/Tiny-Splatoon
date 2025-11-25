#include "Input.h"

GLFWwindow* Input::m_Window = nullptr;

void Input::Init(GLFWwindow* window) {
    m_Window = window;
}

bool Input::GetKey(int key) {
    return glfwGetKey(m_Window, key) == GLFW_PRESS;
}

bool Input::GetMouseButton(int button) {
    return glfwGetMouseButton(m_Window, button) == GLFW_PRESS;
}

glm::vec2 Input::GetMousePosition() {
    double x, y;
    glfwGetCursorPos(m_Window, &x, &y);
    return glm::vec2((float)x, (float)y);
}

float Input::GetMouseX() { return GetMousePosition().x; }
float Input::GetMouseY() { return GetMousePosition().y; }