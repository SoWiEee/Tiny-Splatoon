#pragma once
#include <imgui.h>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include "../network/NetworkProtocol.h"
#include "../network/NetworkManager.h"

class Lobby {
public:
    // 本地儲存的 8 個格子狀態
    LobbySlotInfo slots[8];
    bool isGameStarted = false;

    Lobby() {
        // 初始化：全部沒人 (-1)
        for (int i = 0; i < 8; i++) {
            slots[i].playerID = -1;
            slots[i].teamID = 0;
        }
    }

    // 更新大廳資料 (當收到 Server 封包時呼叫)
    void UpdateState(const PacketLobbyState& pkt) {
        for (int i = 0; i < 8; i++) {
            slots[i] = pkt.slots[i];
        }
    }

    // 繪製 UI (在 Main Loop 呼叫)
    void DrawUI(int windowWidth, int windowHeight) {
        // 設定 ImGui 視窗佔滿全螢幕，且沒有標題列 (看起來像遊戲原生介面)
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2((float)windowWidth, (float)windowHeight));
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings;

        if (ImGui::Begin("LobbyScreen", nullptr, window_flags)) {

            // 1. 標題
            // 讓文字置中稍微麻煩一點，這裡簡單置中
            float fontSize = 3.0f;
            ImGui::SetWindowFontScale(fontSize);

            std::string title = "Tiny Splatoon";
            float textWidth = ImGui::CalcTextSize(title.c_str()).x;
            ImGui::SetCursorPosX((windowWidth - textWidth) * 0.5f);
            ImGui::SetCursorPosY(windowHeight * 0.2f);
            ImGui::Text("%s", title.c_str());

            // 2. 繪製 8 個圓圈
            ImGui::SetWindowFontScale(1.5f); // 縮小一點字體給名字用

            float startY = windowHeight * 0.4f;
            float circleRadius = 40.0f;
            float spacing = 110.0f;
            float startX = (windowWidth - (8 * spacing)) * 0.5f + (spacing * 0.5f);

            ImDrawList* draw_list = ImGui::GetWindowDrawList();

            for (int i = 0; i < 8; i++) {
                float x = startX + (i * spacing);
                float y = startY;

                // 決定顏色
                ImU32 color = IM_COL32(100, 100, 100, 255); // 灰色 (空)
                if (slots[i].playerID != -1) {
                    if (slots[i].teamID == 1) color = IM_COL32(255, 50, 50, 255); // 紅
                    if (slots[i].teamID == 2) color = IM_COL32(50, 255, 50, 255); // 綠
                }

                // 畫圓
                draw_list->AddCircleFilled(ImVec2(x, y), circleRadius, color);

                // 畫名字 (Player 1~8)
                std::string name = "P" + std::to_string(i + 1);
                float nameW = ImGui::CalcTextSize(name.c_str()).x;
                ImGui::SetCursorPos(ImVec2(x - nameW * 0.5f, y + circleRadius + 10));
                ImGui::Text("%s", name.c_str());
            }

            // 3. Start 按鈕 (只有 Server 看得到)
            if (NetworkManager::Instance().IsServer()) {
                ImGui::SetWindowFontScale(2.0f);
                std::string btnText = "START GAME";
                float btnW = 200.0f;
                float btnH = 60.0f;

                ImGui::SetCursorPos(ImVec2((windowWidth - btnW) * 0.5f, windowHeight * 0.7f));

                if (ImGui::Button(btnText.c_str(), ImVec2(btnW, btnH))) {
                    // Server 按下開始
                    StartGame();
                }
            }
            else {
                // Client 顯示 "Waiting for host..."
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
        // 發送開始封包給所有人
        PacketGameStart pkt;
        pkt.header.type = PacketType::S2C_GAME_START;
        NetworkManager::Instance().Broadcast(&pkt, sizeof(pkt), true); // Reliable

        // Server 自己也切換狀態 (透過 flag)
        isGameStarted = true;
    }
};