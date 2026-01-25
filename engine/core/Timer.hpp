#pragma once
#include <GLFW/glfw3.h>

class Timer {
public:
    Timer() {
        lastFrame = glfwGetTime();
    }

    void Tick() {
        float currentFrame = (float)glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;
    }

    float GetDeltaTime() const { return deltaTime; }
    float GetTime() const { return (float)glfwGetTime(); }

private:
    float deltaTime = 0.0f;
    float lastFrame = 0.0f;
};