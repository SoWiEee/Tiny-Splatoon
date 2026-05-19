#pragma once
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <GLFW/glfw3.h>
#include "../network/NetworkManager.hpp"
#include "../network/NetworkProtocol.hpp"

// �w�q UI ��ܪ��A
enum class UIState {
    LOGIN,
    LOBBY,
    HUD,    // �C����������
    NONE    // ���� UI
};

class GUIManager {
public:
    // �j�U��l���
    LobbySlotInfo lobbySlots[kLobbySlotCount];

    // --- Login / Networking inputs ---
    // Join: allow hostname/IP, and optionally "host:port" (useful for playit.gg)
    char ipBuffer[128] = "127.0.0.1";

    // If ipBuffer doesn't include a port, this is used.
    int joinPort = 7777;

    // Host: local UDP port to listen on.
    int hostPort = 7777;

    // �غc�P�Ѻc
    GUIManager(GLFWwindow* window);
    ~GUIManager();

    // �֤߬y�{
    void BeginFrame(); // �}�lø�s
    void Render();     // �����ô�V

    // ø�s�U�ӭ���
    void DrawLogin(bool& outStartServer, bool& outConnectClient);
    void DrawLobby(bool& outStartGame); // outStartGame: Server ���U�}�l�^�� true

    // ��s��Ƥ���
    void UpdateLobbyState(const PacketLobbyState& pkt);
    void SetState(UIState newState) { currentState = newState; }
    UIState GetState() const { return currentState; }

    bool DrawWeaponSelector(WeaponType& currentSelection);

private:
    UIState currentState = UIState::LOGIN;
    GLFWwindow* m_Window;

    void DrawLobbyCircles(int width, int height);
};
