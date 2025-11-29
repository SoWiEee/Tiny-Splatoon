#pragma once
#include "../engine/scene/Scene.h"
#include "../engine/scene/SceneManager.h"
#include "../gui/GUIManager.h"
#include "../network/NetworkManager.h"
#include "../network/NetworkProtocol.h"
#include "GameScene.h"

class LobbyScene : public Scene {
    GUIManager* gui;
    bool isServer;
    float lobbyUpdateTimer = 0.0f;

public:
    LobbyScene(GUIManager* guiManager, bool serverMode)
        : gui(guiManager), isServer(serverMode) {
    }

    void OnEnter() override {
        glfwSetInputMode(glfwGetCurrentContext(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        gui->SetState(UIState::LOBBY);
        AudioManager::Instance().PlayBGM("assets/LifeWillChange.mp3", 0.3f);
    }

    void OnExit() override {
    }

    void Update(float dt) override {
        if (isServer) {
            lobbyUpdateTimer += dt;

            if (lobbyUpdateTimer > 0.5f) {
                PacketLobbyState pkt;

                // 1. 初始化空資料
                for (int i = 0; i < 8; i++) {
                    pkt.slots[i].playerID = -1;
                    pkt.slots[i].teamID = 0;
                }

                // 2. 填入 Server 自己 (Slot 0)
                pkt.slots[0].playerID = 0;
                pkt.slots[0].teamID = 1;

                // 3. 填入連線的 Client
                auto& clientIDs = NetworkManager::Instance().connectedPlayerIDs;
                for (size_t i = 0; i < clientIDs.size() && i < 7; i++) {
                    int pid = clientIDs[i];
                    pkt.slots[i + 1].playerID = pid;
                    pkt.slots[i + 1].teamID = (pid % 2 == 0) ? 1 : 2;

                    // 從 Map 讀取武器類型填入封包
                    if (NetworkManager::Instance().playerWeaponMap.count(pid)) {
                        pkt.slots[i + 1].weaponType = NetworkManager::Instance().playerWeaponMap[pid];
                    }
                    else {
                        pkt.slots[i + 1].weaponType = WeaponType::SHOOTER;
                    }
                }

                // 4. 廣播
                NetworkManager::Instance().Broadcast(&pkt, sizeof(pkt), true);

                // 5. 本地 UI 也要更新
                gui->UpdateLobbyState(pkt);

                lobbyUpdateTimer = 0.0f;
            }
        }
    }

    void Render() override {
        glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void DrawUI() override {
        if (!gui) return;

        bool startGame = false;
        gui->DrawLobby(startGame);

        if (startGame && isServer) {
            // 1. 廣播開始封包
            PacketGameStart pkt;
            pkt.header.type = PacketType::S2C_GAME_START;
            NetworkManager::Instance().Broadcast(&pkt, sizeof(pkt), true);

            // 2. 切換到遊戲場景
            SceneManager::Instance().SwitchTo(std::make_unique<GameScene>());
        }
    }

    // 處理網路封包
    void OnPacket(const ReceivedPacket& pkt) override {
        // Client: 接收大廳更新
        if (pkt.type == PacketType::S2C_LOBBY_UPDATE) {
            gui->UpdateLobbyState(*(PacketLobbyState*)pkt.data.data());
        }
        // Client: 接收開始遊戲訊號
        else if (pkt.type == PacketType::S2C_GAME_START) {
            std::cout << "[Lobby] Game Started!" << std::endl;
            SceneManager::Instance().SwitchTo(std::make_unique<GameScene>());
        }
        // Client: 接收歡迎訊息 (設定 ID)
        else if (pkt.type == PacketType::S2C_JOIN_ACCEPT) {
            auto* p = (PacketJoinAccept*)pkt.data.data();
            NetworkManager::Instance().SetMyPlayerID(p->yourPlayerID);
            NetworkManager::Instance().SetMyTeamID(p->yourTeamID);
            std::cout << ">> Lobby Joined! ID: " << p->yourPlayerID << std::endl;
        }
        // Server 處理換武器請求
        if (isServer && pkt.type == PacketType::C2S_LOBBY_CHANGE_WEAPON) {
            auto* p = (PacketLobbyChangeWeapon*)pkt.data.data();
            NetworkManager::Instance().playerWeaponMap[p->playerID] = p->newWeapon;
            std::cout << "[Lobby] Player " << p->playerID << " changed weapon to " << (int)p->newWeapon << std::endl;
        }
    }
};