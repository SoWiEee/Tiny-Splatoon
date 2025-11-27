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
#include "network/NetworkManager.h"

const unsigned int SCR_WIDTH = 1280;
const unsigned int SCR_HEIGHT = 720;

Camera* mainCamera = nullptr;

void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (mainCamera) mainCamera->ProcessMouseMovement((float)xpos, (float)ypos);
}

int main() {
    // =========================================================
    // 1. 網路初始化與模式選擇
    // =========================================================

    // 初始化 GameNetworkingSockets
    if (!NetworkManager::Instance().Initialize()) {
        std::cerr << "Failed to initialize NetworkManager!" << std::endl;
        return -1;
    }

    std::cout << "========================================" << std::endl;
    std::cout << " Tiny Splatoon Network Test " << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Select Mode: [S]erver or [C]lient? ";
    char mode;
    std::cin >> mode;

    bool isServerMode = (mode == 's' || mode == 'S');

    if (isServerMode) {
        if (NetworkManager::Instance().StartServer(7777)) {
            std::cout << ">> Server Mode Started on Port 7777" << std::endl;
        }
    }
    else {
        std::string ip;
        std::cout << "Enter Server IP (default 127.0.0.1): ";
        // 為了避免 cin 讀取換行符號問題，這裡簡單處理
        std::cin.ignore();
        std::getline(std::cin, ip);
        if (ip.empty()) ip = "127.0.0.1";

        if (NetworkManager::Instance().Connect(ip, 7777)) {
            std::cout << ">> Client Mode Started, connecting to " << ip << "..." << std::endl;
        }
    }

    // =========================================================
    // 2. 視窗與引擎初始化
    // =========================================================
    std::string winTitle = isServerMode ? "Tiny Splatoon [SERVER]" : "Tiny Splatoon [CLIENT]";
    Window window(SCR_WIDTH, SCR_HEIGHT, winTitle);
    Timer timer;

    glfwSetCursorPosCallback(window.GetNativeWindow(), mouse_callback);

    glEnable(GL_DEPTH_TEST);

    Shader shader("assets/shaders/default.vert", "assets/shaders/default.frag");

    // create camera
    GameObject* cameraObj = new GameObject("MainCamera");
    mainCamera = cameraObj->AddComponent<Camera>();

    // GameWorld init
    GameWorld game;
    // UI init
    GameObject* uiObj = new GameObject("UI");

    // HUD
    HUD* hud = uiObj->AddComponent<HUD>((float)SCR_WIDTH, (float)SCR_HEIGHT);
    game.Init(cameraObj, hud);

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

        NetworkManager::Instance().Update();
        // -----------------------------------------------------
        // [測試] 發送封包
        // -----------------------------------------------------
        // 按下 T 鍵發送測試封包
        // 為了避免按住一直發，你可以稍微改一下 Input 類別支援 GetKeyDown，或用簡單的 Timer 卡住
        static float sendTimer = 0.0f;
        sendTimer += dt;

        if (Input::GetKey(GLFW_KEY_T) && sendTimer > 0.5f) {
            sendTimer = 0.0f;

            PacketShoot pkt; // 借用射擊封包來測試
            pkt.header.type = PacketType::C2S_SHOOT;
            pkt.playerID = 999; // 測試 ID
            pkt.origin = glm::vec3(1, 2, 3); // 測試數據

            if (isServerMode) {
                NetworkManager::Instance().Broadcast(&pkt, sizeof(pkt), true);
                std::cout << "[Send] Server broadcasted a test packet!" << std::endl;
            }
            else {
                NetworkManager::Instance().SendToServer(&pkt, sizeof(pkt), true);
                std::cout << "[Send] Client sent a test packet!" << std::endl;
            }
        }
        // [測試] 接收封包
        while (NetworkManager::Instance().HasPackets()) {
            auto received = NetworkManager::Instance().PopPacket();

            std::cout << "[Recv] Got Packet Type: " << (int)received.type
                << " Size: " << received.data.size() << " bytes" << std::endl;

            if (received.type == PacketType::C2S_SHOOT) {
                std::cout << "   -> It's a Shoot Packet! (Test successful)" << std::endl;
            }
        }


        // TPS camera
        if (game.localPlayer) {
            glm::vec3 targetPos = game.localPlayer->transform->position;

            // camera parameter
            float camDist = 5.0f;
            float camHeight = 2.5f;

            glm::vec3 camDir = cameraObj->transform->GetForward();
            cameraObj->transform->position = targetPos - (camDir * camDist) + glm::vec3(0, camHeight, 0);
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