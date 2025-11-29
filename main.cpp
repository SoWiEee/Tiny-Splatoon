#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>
#include <string>

#include "engine/core/Window.h"
#include "engine/core/Timer.h"
#include "engine/core/Input.h"
#include "engine/core/Logger.h"
#include "engine/rendering/Shader.h"
#include "engine/rendering/Mesh.h"
#include "components/Camera.h"
#include "components/HUD.h"
#include "components/Scoreboard.h"
#include "network/NetworkManager.h"
#include "gui/GUIManager.h"
#include "network/NetworkProtocol.h"
#include "engine/scene/SceneManager.h"
#include "engine/audio/AudioManager.h"
#include "scene/Lobby.h"
#include "scene/LoginScene.h"
#include "scene/GameScene.h"

enum class GameState {
    LOGIN_MENU,
    LOBBY,
    PLAYING
};

const unsigned int SCR_WIDTH = 1280;
const unsigned int SCR_HEIGHT = 720;

Camera* mainCamera = nullptr;

void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (GameScene::CurrentCamera) {
        GameScene::CurrentCamera->ProcessMouseMovement((float)xpos, (float)ypos);
    }
}

int main() {
    // Network, Window, GUI Init
    NetworkManager::Instance().Initialize();
    Window window(SCR_WIDTH, SCR_HEIGHT, "Tiny Splatoon");
    glfwSetCursorPosCallback(window.GetNativeWindow(), mouse_callback);
    GUIManager gui(window.GetNativeWindow());

    glEnable(GL_DEPTH_TEST);
    SceneManager::Instance().SwitchTo(std::make_unique<LoginScene>(&gui));
    Timer timer;
    AudioManager::Instance().Initialize();
    AudioManager::Instance().LoadSound("shoot", "assets/shoot.mp3");

    // Game Loop
    while (!window.ShouldClose()) {
        timer.Tick();
        float dt = timer.GetDeltaTime();

        if (Input::GetKey(GLFW_KEY_ESCAPE)) break;

        NetworkManager::Instance().Update();
        while (NetworkManager::Instance().HasPackets()) {
            SceneManager::Instance().HandlePacket(NetworkManager::Instance().PopPacket());
        }
        SceneManager::Instance().Update(dt);
        SceneManager::Instance().Render();
        gui.BeginFrame();
        SceneManager::Instance().DrawUI();
        gui.Render();

        window.SwapBuffers();
        window.PollEvents();
    }
    return 0;
}