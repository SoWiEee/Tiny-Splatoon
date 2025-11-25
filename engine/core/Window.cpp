#include "Window.h"

// 滑鼠回呼函式 (全域)
void mouse_callback_proxy(GLFWwindow* window, double xpos, double ypos) {
    // 如果你有 Camera 需要這數據，可以在這裡轉發，或者讓 Camera 直接去問 Input::GetMousePosition()
}

Window::Window(int width, int height, const std::string& title)
    : m_Width(width), m_Height(height), m_Title(title)
{
    Logger::Log("Initializing Window...");

    if (!glfwInit()) {
        Logger::Error("Failed to initialize GLFW");
        return;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    m_Window = glfwCreateWindow(width, height, title.c_str(), NULL, NULL);
    if (m_Window == NULL) {
        Logger::Error("Failed to create GLFW window");
        glfwTerminate();
        return;
    }

    glfwMakeContextCurrent(m_Window);

    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        Logger::Error("Failed to initialize GLAD");
        return;
    }

    glViewport(0, 0, width, height);

    Input::Init(m_Window);

    // 設定滑鼠模式 (FPS 遊戲通常隱藏游標)
    glfwSetInputMode(m_Window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    Logger::Log("Window Initialized Successfully.");
}

Window::~Window() {
    glfwDestroyWindow(m_Window);
    glfwTerminate();
}

bool Window::ShouldClose() {
    return glfwWindowShouldClose(m_Window);
}

void Window::SwapBuffers() {
    glfwSwapBuffers(m_Window);
}

void Window::PollEvents() {
    glfwPollEvents();
}