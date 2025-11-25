#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <iostream>
#include <vector>

#include "engine/core/Window.h"
#include "engine/core/Timer.h"
#include "engine/core/Input.h"
#include "engine/core/Logger.h"
#include "engine/rendering/Shader.h"
#include "engine/rendering/Mesh.h"
#include "components/Camera.h"
#include "components/HUD.h"
#include "components/Scoreboard.h"
#include "gameplay/GameWorld.h"

const unsigned int SCR_WIDTH = 1280;
const unsigned int SCR_HEIGHT = 720;

Camera* mainCamera = nullptr;

void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (mainCamera) mainCamera->ProcessMouseMovement((float)xpos, (float)ypos);
}

int main() {
    Window window(SCR_WIDTH, SCR_HEIGHT, "Splatoon Simulator");
    Timer timer;

    glfwSetCursorPosCallback(window.GetNativeWindow(), mouse_callback);

    glEnable(GL_DEPTH_TEST);

    Shader shader("assets/shaders/default.vert", "assets/shaders/default.frag");

    // create camera
    GameObject* cameraObj = new GameObject("MainCamera");
    mainCamera = cameraObj->AddComponent<Camera>();

    // GameWorld init
    GameWorld game;
    game.Init(cameraObj);

    // UI init
    GameObject* uiObj = new GameObject("UI");

    // HUD
    HUD* hud = uiObj->AddComponent<HUD>((float)SCR_WIDTH, (float)SCR_HEIGHT);

    // Scoreboard
    Scoreboard* scoreboard = uiObj->AddComponent<Scoreboard>((float)SCR_WIDTH, (float)SCR_HEIGHT, game.splatMap);

    // 把 HUD 傳給 Player 讓他控制回充顯示 (如果 Player 支援的話)
    // 假設你在 Player.h 裡有 SetHUD 之類的函式，或者直接存取
    if (game.localPlayer) {
        // game.localPlayer->hud = hud; // 視你的 Player 實作而定
    }

    // Game Loop
    while (!window.ShouldClose()) {
        timer.Tick();
        float dt = timer.GetDeltaTime();

        if (Input::GetKey(GLFW_KEY_ESCAPE)) break;

        // TPS camera
        if (game.localPlayer) {
            glm::vec3 targetPos = game.localPlayer->transform->position;

            // camera parameter
            float camDist = 5.0f;
            float camHeight = 2.5f;

            glm::vec3 camDir = cameraObj->transform->GetForward();
            cameraObj->transform->position = targetPos - (camDir * camDist) + glm::vec3(0, camHeight, 0);

            // [可選] 平滑移動 (Lerp) 讓相機不要太生硬
            // cameraObj->transform->position = glm::mix(cameraObj->transform->position, desiredPos, dt * 10.0f);
        }

        // Update Player, Physics, Projectiles, SplatMap
        game.Update(dt);

        // 更新 UI 邏輯 (計分板計時器等)
        scoreboard->Update(dt);
        hud->Update(dt);

        glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);

        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        shader.Bind();
        shader.SetMat4("view", mainCamera->GetViewMatrix());
        shader.SetMat4("projection", mainCamera->GetProjectionMatrix());
        shader.SetVec3("viewPos", cameraObj->transform->position);

        game.Render(shader);

        hud->Draw(shader);
        scoreboard->Draw(shader);

        window.SwapBuffers();
        window.PollEvents();
    }

    game.CleanUp();

    return 0;
}