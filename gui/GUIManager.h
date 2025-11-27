#pragma once
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <GLFW/glfw3.h>
#include "../network/NetworkManager.h"
#include "../network/NetworkProtocol.h"

// 定義 UI 顯示狀態
enum class UIState {
    LOGIN,
    LOBBY,
    HUD,    // 遊戲中的介面
    NONE    // 關閉 UI
};

class GUIManager {
public:
    // 大廳格子資料
    LobbySlotInfo lobbySlots[8];

    // 輸入緩衝區
    char ipBuffer[128] = "127.0.0.1";

    // 建構與解構
    GUIManager(GLFWwindow* window);
    ~GUIManager();

    // 核心流程
    void BeginFrame(); // 開始繪製
    void Render();     // 結束並渲染

    // 繪製各個頁面
    void DrawLogin(bool& outStartServer, bool& outConnectClient);
    void DrawLobby(bool& outStartGame); // outStartGame: Server 按下開始回傳 true

    // 更新資料介面
    void UpdateLobbyState(const PacketLobbyState& pkt);
    void SetState(UIState newState) { currentState = newState; }
    UIState GetState() const { return currentState; }

private:
    UIState currentState = UIState::LOGIN;
    GLFWwindow* m_Window;

    void DrawLobbyCircles(int width, int height);
};