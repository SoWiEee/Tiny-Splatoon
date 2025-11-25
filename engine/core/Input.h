#pragma once
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

class Input {
public:
    static void Init(GLFWwindow* window);

    // Áä½L
    static bool GetKey(int key);        // «ö¦í
    static bool GetKeyDown(int key);    // «ö¤UÀþ¶¡

    // ·Æ¹«
    static bool GetMouseButton(int button);
    static glm::vec2 GetMousePosition();
    static float GetMouseX();
    static float GetMouseY();

private:
    static GLFWwindow* m_Window;
};