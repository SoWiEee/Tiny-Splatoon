#pragma once
#include "../engine/scene/Scene.hpp"
#include "../engine/scene/SceneManager.hpp"
#include "../gui/GUIManager.hpp"
#include "../network/NetworkManager.hpp"
#include "../network/NetworkProtocol.hpp"
#include "GameScene.hpp"

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
        AudioManager::Instance().PlayBGM("assets/audios/LifeWillChange.mp3", 0.2f);
    }

    void OnExit() override {
    }

    void Update(float dt) override {
        if (isServer) {
            lobbyUpdateTimer += dt;

            if (lobbyUpdateTimer > 0.5f) {
                PacketLobbyState pkt;

                // 1. ��l�ƪŸ��
                for (int i = 0; i < kLobbySlotCount; i++) {
                    pkt.slots[i].playerID = -1;
                    pkt.slots[i].teamID = 0;
                }

                // 2. ��J Server �ۤv (Slot 0)
                pkt.slots[0].playerID = 0;
                pkt.slots[0].teamID = 1;

                // 3. ��J�s�u�� Client
                auto& clientIDs = NetworkManager::Instance().connectedPlayerIDs;
                for (size_t i = 0; i < clientIDs.size() && i < (kLobbySlotCount - 1); i++) {
                    int pid = clientIDs[i];
                    pkt.slots[i + 1].playerID = pid;
                    pkt.slots[i + 1].teamID = (pid % 2 == 0) ? 1 : 2;

                    // �q Map Ū���Z��������J�ʥ]
                    if (NetworkManager::Instance().playerWeaponMap.count(pid)) {
                        pkt.slots[i + 1].weaponType = NetworkManager::Instance().playerWeaponMap[pid];
                    }
                    else {
                        pkt.slots[i + 1].weaponType = WeaponType::SHOOTER;
                    }
                }

                // 4. �s��
                NetworkManager::Instance().Broadcast(&pkt, sizeof(pkt), true);

                // 5. ���a UI �]�n��s
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
            // 1. �s���}�l�ʥ]
            PacketGameStart pkt;
            pkt.header.type = PacketType::S2C_GAME_START;
            NetworkManager::Instance().Broadcast(&pkt, sizeof(pkt), true);

            // 2. ������C������
            SceneManager::Instance().SwitchTo(std::make_unique<GameScene>(gui));
        }
    }

    // �B�z�����ʥ]
    void OnPacket(const ReceivedPacket& pkt) override {
        // Client: �����j�U��s
        if (pkt.type == PacketType::S2C_LOBBY_UPDATE) {
            if (const auto* lobbyState = TryGetPacket<PacketLobbyState>(pkt)) {
                gui->UpdateLobbyState(*lobbyState);
            }
        }
        // Client: �����}�l�C���T��
        else if (pkt.type == PacketType::S2C_GAME_START) {
            std::cout << "[Lobby] Game Started!" << std::endl;
            SceneManager::Instance().SwitchTo(std::make_unique<GameScene>(gui));
        }
        // Client: �����w��T�� (�]�w ID)
        else if (pkt.type == PacketType::S2C_JOIN_ACCEPT) {
            if (const auto* joinAccept = TryGetPacket<PacketJoinAccept>(pkt)) {
                NetworkManager::Instance().SetMyPlayerID(joinAccept->yourPlayerID);
                NetworkManager::Instance().SetMyTeamID(joinAccept->yourTeamID);
                std::cout << ">> Lobby Joined! ID: " << joinAccept->yourPlayerID << std::endl;
            }
        }
        // Server �B�z���Z���ШD
        if (isServer && pkt.type == PacketType::C2S_LOBBY_CHANGE_WEAPON) {
            if (const auto* weaponChange = TryGetPacket<PacketLobbyChangeWeapon>(pkt)) {
                NetworkManager::Instance().playerWeaponMap[weaponChange->playerID] = weaponChange->newWeapon;
                std::cout << "[Lobby] Player " << weaponChange->playerID << " changed weapon to " << (int)weaponChange->newWeapon << std::endl;
            }
        }
    }
};
