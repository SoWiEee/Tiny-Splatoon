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
#include "gameplay/GameWorld.h"
#include "network/NetworkManager.h"
#include "gui/GUIManager.h"
#include "scene/Lobby.h"
#include "network/NetworkProtocol.h"

enum class GameState {
    LOGIN_MENU,
    LOBBY,
    PLAYING
};

const unsigned int SCR_WIDTH = 1280;
const unsigned int SCR_HEIGHT = 720;

Camera* mainCamera = nullptr;

void mouse_callback(GLFWwindow* window, double xpos, double ypos) {
    if (mainCamera) mainCamera->ProcessMouseMovement((float)xpos, (float)ypos);
}

int main() {
    // 1. Network Init
    if (!NetworkManager::Instance().Initialize()) {
        std::cerr << "Failed to initialize NetworkManager!" << std::endl;
        return -1;
    }

    // 2. Window Init
    Window window(SCR_WIDTH, SCR_HEIGHT, "Tiny Splatoon");
    glfwSetCursorPosCallback(window.GetNativeWindow(), mouse_callback);

    // 3. GUI Init
    GUIManager gui(window.GetNativeWindow());

    glEnable(GL_DEPTH_TEST);
    Shader shader("assets/shaders/default.vert", "assets/shaders/default.frag");

    // 4. GameObject Init
    GameObject* cameraObj = new GameObject("MainCamera");
    mainCamera = cameraObj->AddComponent<Camera>();

    GameWorld game;
    GameState currentState = GameState::LOGIN_MENU;
    Timer timer;

    // [修正 1] UI 物件先宣告指標，但暫時不初始化內容
    GameObject* uiObj = new GameObject("UI");
    HUD* hud = nullptr;
    Scoreboard* scoreboard = nullptr;

    // Game Loop
    while (!window.ShouldClose()) {
        timer.Tick();
        float dt = timer.GetDeltaTime();

        if (Input::GetKey(GLFW_KEY_ESCAPE)) break;

        NetworkManager::Instance().Update();

        // ---------------------------------------------
        // 封包處理
        // ---------------------------------------------
        while (NetworkManager::Instance().HasPackets()) {
            auto received = NetworkManager::Instance().PopPacket();

            if (currentState == GameState::LOBBY) {
                if (received.type == PacketType::S2C_LOBBY_UPDATE) {
                    gui.UpdateLobbyState(*(PacketLobbyState*)received.data.data());
                }
                else if (received.type == PacketType::S2C_GAME_START) {
                    // Client 收到開始訊號
                    std::cout << "Game Started by Host!" << std::endl;
                    currentState = GameState::PLAYING;
                    gui.SetState(UIState::HUD);

                    // 遊戲開始時才初始化 HUD 和 Scoreboard
                    if (!hud) hud = uiObj->AddComponent<HUD>((float)SCR_WIDTH, (float)SCR_HEIGHT);

                    // 先 Init Game (這樣 splatMap 才會被 new 出來)
                    // 注意：這裡先傳 hud 和 nullptr 給 scoreboard
                    game.Init(cameraObj, hud, nullptr);

                    // 現在 game.splatMap 有東西了，再建立 Scoreboard
                    if (!scoreboard) scoreboard = uiObj->AddComponent<Scoreboard>((float)SCR_WIDTH, (float)SCR_HEIGHT, game.splatMap);

                    game.scoreboardRef = scoreboard;
                }
                else if (received.type == PacketType::S2C_JOIN_ACCEPT) {
                    auto* pkt = (PacketJoinAccept*)received.data.data();

                    // 1. 設定 Client 自己的 ID (這樣 GUI 才能畫出 "You")
                    NetworkManager::Instance().SetMyPlayerID(pkt->yourPlayerID);
                    NetworkManager::Instance().SetMyTeamID(pkt->yourTeamID);

                    std::cout << ">> Lobby Joined! My ID: " << pkt->yourPlayerID << std::endl;
                }
            }
            else if (currentState == GameState::PLAYING) {
                game.HandlePacket(received);
            }
        }

        // ---------------------------------------------
        // GUI 邏輯
        // ---------------------------------------------
        gui.BeginFrame();

        if (currentState == GameState::LOGIN_MENU) {
            glfwSetInputMode(window.GetNativeWindow(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            bool startServer = false;
            bool connectClient = false;
            gui.DrawLogin(startServer, connectClient);

            if (startServer) {
                if (NetworkManager::Instance().StartServer(7777)) {
                    currentState = GameState::LOBBY;
                    gui.SetState(UIState::LOBBY);
                    gui.lobbySlots[0].playerID = 0;
                    gui.lobbySlots[0].teamID = 1;
                }
            }
            if (connectClient) {
                if (NetworkManager::Instance().Connect(gui.ipBuffer, 7777)) {
                    currentState = GameState::LOBBY;
                    gui.SetState(UIState::LOBBY);
                }
            }
        }
        else if (currentState == GameState::LOBBY) {
            glfwSetInputMode(window.GetNativeWindow(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);

            bool startGame = false;
            gui.DrawLobby(startGame);

            if (NetworkManager::Instance().IsServer()) {
                static float lobbyUpdateTimer = 0.0f;
                lobbyUpdateTimer += dt;

                if (lobbyUpdateTimer > 0.5f) {
                    PacketLobbyState pkt;
                    // 初始化
                    for (int i = 0; i < 8; i++) {
                        pkt.slots[i].playerID = -1;
                        pkt.slots[i].teamID = 0;
                    }
                    // 填入 Server
                    pkt.slots[0].playerID = 0;
                    pkt.slots[0].teamID = 1;

                    // 填入 Clients
                    auto& clientIDs = NetworkManager::Instance().connectedPlayerIDs;
                    for (size_t i = 0; i < clientIDs.size() && i < 7; i++) {
                        int pid = clientIDs[i];
                        pkt.slots[i + 1].playerID = pid;
                        pkt.slots[i + 1].teamID = (pid % 2 == 0) ? 1 : 2;
                    }

                    NetworkManager::Instance().Broadcast(&pkt, sizeof(pkt), true);
                    gui.UpdateLobbyState(pkt);
                    lobbyUpdateTimer = 0.0f;
                }

                // 按下開始按鈕的邏輯
                if (startGame) {
                    PacketGameStart pkt;
                    pkt.header.type = PacketType::S2C_GAME_START;
                    NetworkManager::Instance().Broadcast(&pkt, sizeof(pkt), true);

                    // Server 切換狀態
                    currentState = GameState::PLAYING;
                    gui.SetState(UIState::HUD);

                    // [修正 1] Server 端也要依序初始化
                    if (!hud) hud = uiObj->AddComponent<HUD>((float)SCR_WIDTH, (float)SCR_HEIGHT);
                    game.Init(cameraObj, hud, nullptr);
                    if (!scoreboard) scoreboard = uiObj->AddComponent<Scoreboard>((float)SCR_WIDTH, (float)SCR_HEIGHT, game.splatMap);
                    game.scoreboardRef = scoreboard;
                }
            }
        }
        else if (currentState == GameState::PLAYING) {
            // FPS 模式
            glfwSetInputMode(window.GetNativeWindow(), GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            if (game.localPlayer) {
                glm::vec3 targetPos = game.localPlayer->transform->position;

                // camera parameter
                float camDist = 5.0f;
                float camHeight = 2.5f;

                glm::vec3 camDir = cameraObj->transform->GetForward();
                cameraObj->transform->position = targetPos - (camDir * camDist) + glm::vec3(0, camHeight, 0);
            }

            game.Update(dt);
            if (scoreboard) scoreboard->Update(dt);
            if (hud) hud->Update(dt);
            if (mainCamera) mainCamera->Update(dt);

            glViewport(0, 0, SCR_WIDTH, SCR_HEIGHT);
            glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            shader.Bind();
            shader.SetMat4("view", mainCamera->GetViewMatrix());
            shader.SetMat4("projection", mainCamera->GetProjectionMatrix());
            shader.SetVec3("viewPos", cameraObj->transform->position);

            game.Render(shader);
            if (hud) hud->Draw(shader);
            if (scoreboard) scoreboard->Draw(shader);
        }

        gui.Render();
        window.SwapBuffers();
        window.PollEvents();
    }

    game.CleanUp();
    // 釋放記憶體
    delete cameraObj;
    delete uiObj;

    return 0;
}