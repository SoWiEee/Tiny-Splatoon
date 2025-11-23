#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>

#include "graphics/Shader.h"
#include "engine/GameObject.h"
#include "components/MeshRenderer.h"
#include "components/Camera.h"

// Settings
const unsigned int SCR_WIDTH = 1280;
const unsigned int SCR_HEIGHT = 720;

// Timing
float deltaTime = 0.0f;
float lastFrame = 0.0f;

// Mouse Callback
Camera* mainCamera = nullptr;
void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (mainCamera) mainCamera->ProcessMouseMovement((float)xpos, (float)ypos);
}

int main() {
    // 1. 初始化 GLFW
    glfwInit();
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(SCR_WIDTH, SCR_HEIGHT, "Splatoon OpenGL Project", NULL, NULL);
    if (window == NULL) {
        std::cout << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return -1;
    }
    glfwMakeContextCurrent(window);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    // 2. 初始化 GLAD
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to initialize GLAD" << std::endl;
        return -1;
    }

    glEnable(GL_DEPTH_TEST);

    // 3. 建立 Shader
    Shader shader("Assets/Shaders/default.vert", "Assets/Shaders/default.frag");

    // 4. 建立場景 (Scene Graph)
    std::vector<GameObject*> scene;

    // --- 玩家 (Camera) ---
    GameObject* playerObj = new GameObject("Player");
    playerObj->transform->position = glm::vec3(0.0f, 2.0f, 5.0f);
    mainCamera = playerObj->AddComponent<Camera>(); // 存起來給 Callback 用
    scene.push_back(playerObj);

    // --- 地板 (Floor) ---
    GameObject* floorObj = new GameObject("Floor");
    floorObj->transform->position = glm::vec3(0.0f, 0.0f, 0.0f);
    floorObj->transform->scale = glm::vec3(20.0f, 1.0f, 20.0f); // 20倍大
    floorObj->AddComponent<MeshRenderer>("Plane", glm::vec3(0.8f, 0.8f, 0.8f)); // 灰色
    scene.push_back(floorObj);

    // --- 障礙物 (Box) ---
    GameObject* boxObj = new GameObject("Box");
    boxObj->transform->position = glm::vec3(2.0f, 1.0f, -2.0f); // 浮空一點
    boxObj->AddComponent<MeshRenderer>("Cube", glm::vec3(1.0f, 0.5f, 0.2f)); // 橘色
    scene.push_back(boxObj);


    // 5. Render Loop
    while (!glfwWindowShouldClose(window)) {
        // Time Logic
        float currentFrame = (float)glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // Input
        if (glfwGetKey(window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
            glfwSetWindowShouldClose(window, true);
        mainCamera->ProcessKeyboard(window, deltaTime);

        // Update
        for (auto go : scene) go->Update(deltaTime);

        // Render
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        shader.use();
        shader.setMat4("view", mainCamera->GetViewMatrix());
        shader.setMat4("projection", mainCamera->projection);

        for (auto go : scene) go->Draw(shader);

        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    glfwTerminate();
    return 0;
}