#pragma once
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <string>
#include "Logger.h"
#include "Input.h"

class Window {
public:
    Window(int width, int height, const std::string& title);
    ~Window();

    bool ShouldClose();
    void SwapBuffers();
    void PollEvents();

    int GetWidth() const { return m_Width; }
    int GetHeight() const { return m_Height; }
    GLFWwindow* GetNativeWindow() const { return m_Window; }

private:
    GLFWwindow* m_Window;
    int m_Width, m_Height;
    std::string m_Title;
};