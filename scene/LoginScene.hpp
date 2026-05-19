#pragma once
#include "../engine/scene/Scene.hpp"
#include "../engine/scene/SceneManager.hpp"
#include "../gui/GUIManager.hpp"
#include "../network/NetworkManager.hpp"
#include "LobbyScene.hpp"

class LoginScene : public Scene {
    GUIManager* gui;

public:
    explicit LoginScene(GUIManager* guiManager) : gui(guiManager) {}
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
        // �n�J�e���S���C���޿�n��s
    }

    void Render() override {
        glClearColor(0.2f, 0.2f, 0.2f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void DrawUI() override {
        if (!gui) return;

        bool startServer = false;
        bool connectClient = false;

        // �I�s GUIManager ø�s�n�J��
        gui->DrawLogin(startServer, connectClient);

        // --- �B�z���s�޿� ---

        // A. �Ұʦ��A��
        if (startServer) {
            if (NetworkManager::Instance().StartServer(gui->hostPort)) {
                // Server �ۤv���ڲ� 1 �Ӧ�m (����)
                gui->lobbySlots[0].playerID = 0;
                gui->lobbySlots[0].teamID = 1;

                // ������j�U���� (Server �Ҧ�)
                SceneManager::Instance().SwitchTo(std::make_unique<LobbyScene>(gui, true));
            }
        }

        // B. �s�u�Ȥ��
        if (connectClient) {
            std::string host;
            int port = gui->joinPort;
            if (!NetworkManager::ParseHostPort(gui->ipBuffer, host, port, gui->joinPort)) {
                return;
            }
            if (NetworkManager::Instance().Connect(host, port)) {
                // ������j�U���� (Client �Ҧ�)
                SceneManager::Instance().SwitchTo(std::make_unique<LobbyScene>(gui, false));
            }
        }
    }
};
