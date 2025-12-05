#pragma once
#include "../engine/scene/Scene.h"
#include "../engine/scene/SceneManager.h"
#include "../gui/GUIManager.h"
#include "../network/NetworkManager.h"
#include "LobbyScene.h"

class LoginScene : public Scene {
    GUIManager* gui;

public:
    LoginScene() {}
    LoginScene(GUIManager* guiManager) : gui(guiManager) {}
    virtual ~LoginScene() { OnExit(); }

    void OnEnter() override {
        glfwSetInputMode(glfwGetCurrentContext(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        gui->SetState(UIState::LOGIN);
    }

    void OnExit() override {
        GLFWwindow* currentWindow = glfwGetCurrentContext();
        if (currentWindow != nullptr) {
            glfwSetInputMode(currentWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
    }

    void Update(float dt) override {
        // 登入畫面沒有遊戲邏輯要更新
    }

    void Render() override {
        glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void DrawUI() override {
        if (!gui) return;

        bool startServer = false;
        bool connectClient = false;

        // 呼叫 GUIManager 繪製登入框
        gui->DrawLogin(startServer, connectClient);

        // --- 處理按鈕邏輯 ---

        // A. 啟動伺服器
        if (startServer) {
            if (NetworkManager::Instance().StartServer(7777)) {
                // Server 自己佔據第 1 個位置 (紅色)
                gui->lobbySlots[0].playerID = 0;
                gui->lobbySlots[0].teamID = 1;

                // 切換到大廳場景 (Server 模式)
                SceneManager::Instance().SwitchTo(std::make_unique<LobbyScene>(gui, true));
            }
        }

        // B. 連線客戶端
        if (connectClient) {
            if (NetworkManager::Instance().Connect(gui->ipBuffer, 7777)) {
                // 切換到大廳場景 (Client 模式)
                SceneManager::Instance().SwitchTo(std::make_unique<LobbyScene>(gui, false));
            }
        }
    }
};