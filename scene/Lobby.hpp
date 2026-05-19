#pragma once
#include <imgui.h>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "../network/NetworkProtocol.hpp"
#include "../network/NetworkManager.hpp"

class Lobby {
public:
    // ���a�x�s�� 8 �Ӯ�l���A
    LobbySlotInfo slots[kLobbySlotCount];
    bool isGameStarted = false;

    Lobby() {
        // ��l�ơG�����S�H (-1)
        for (int i = 0; i < kLobbySlotCount; i++) {
            slots[i].playerID = -1;
            slots[i].teamID = 0;
        }
    }

    // ��s�j�U��� (������ Server �ʥ]�ɩI�s)
    void UpdateState(const PacketLobbyState& pkt) {
        for (int i = 0; i < kLobbySlotCount; i++) {
            slots[i] = pkt.slots[i];
        }
    }

    // ø�s UI (�b Main Loop �I�s)
    void DrawUI(int windowWidth, int windowHeight) {
        // �]�w ImGui �����������ù��A�B�S�����D�C (�ݰ_�ӹ��C����ͤ���)
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)windowWidth, (float)windowHeight));
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;

        if (ImGui::Begin("LobbyScreen", nullptr, window_flags)) {

            // 1. ���D
            // ����r�m���y�L�·Ф@�I�A�o��²��m��
            float fontSize = 3.0f;
            ImGui::SetWindowFontScale(fontSize);

            std::string title = "Tiny Splatoon";
            float textWidth = ImGui::CalcTextSize(title.c_str()).x;
            ImGui::SetCursorPosX((windowWidth - textWidth) * 0.5f);
            ImGui::SetCursorPosY(windowHeight * 0.2f);
            ImGui::Text("%s", title.c_str());

            // 2. ø�s 8 �Ӷ��
            ImGui::SetWindowFontScale(1.5f); // �Y�p�@�I�r�鵹�W�r��

            float startY = windowHeight * 0.4f;
            float circleRadius = 40.0f;
            float spacing = 110.0f;
            float startX = (windowWidth - (kLobbySlotCount * spacing)) * 0.5f + (spacing * 0.5f);

            ImDrawList* draw_list = ImGui::GetWindowDrawList();

            for (int i = 0; i < kLobbySlotCount; i++) {
                float x = startX + (i * spacing);
                float y = startY;

                // �M�w�C��
                ImU32 color = IM_COL32(100, 100, 100, 255); // �Ǧ� (��)
                if (slots[i].playerID != -1) {
                    if (slots[i].teamID == 1) color = IM_COL32(255, 50, 50, 255); // ��
                    if (slots[i].teamID == 2) color = IM_COL32(50, 255, 50, 255); // ��
                }

                // �e��
                draw_list->AddCircleFilled(ImVec2(x, y), circleRadius, color);

                // �e�W�r (Player 1~8)
                std::string name = "P" + std::to_string(i + 1);
                float nameW = ImGui::CalcTextSize(name.c_str()).x;
                ImGui::SetCursorPos(ImVec2(x - nameW * 0.5f, y + circleRadius + 10));
                ImGui::Text("%s", name.c_str());
            }

            // 3. Start ���s (�u�� Server �ݱo��)
            if (NetworkManager::Instance().IsServer()) {
                ImGui::SetWindowFontScale(2.0f);
                std::string btnText = "START GAME";
                float btnW = 200.0f;
                float btnH = 60.0f;

                ImGui::SetCursorPos(ImVec2((windowWidth - btnW) * 0.5f, windowHeight * 0.7f));

                if (ImGui::Button(btnText.c_str(), ImVec2(btnW, btnH))) {
                    // Server ���U�}�l
                    StartGame();
                }
            }
            else {
                // Client ��� "Waiting for host..."
                ImGui::SetWindowFontScale(1.5f);
                std::string waitText = "Waiting for host to start...";
                float waitW = ImGui::CalcTextSize(waitText.c_str()).x;
                ImGui::SetCursorPos(ImVec2((windowWidth - waitW) * 0.5f, windowHeight * 0.7f));
                ImGui::Text("%s", waitText.c_str());
            }
        }
        ImGui::End();
    }

private:
    void StartGame() {
        // �o�e�}�l�ʥ]���Ҧ��H
        PacketGameStart pkt;
        pkt.header.type = PacketType::S2C_GAME_START;
        NetworkManager::Instance().Broadcast(&pkt, sizeof(pkt), true); // Reliable

        // Server �ۤv�]�������A (�z�L flag)
        isGameStarted = true;
    }
};
