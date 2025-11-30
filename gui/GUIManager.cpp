#include "GUIManager.h"
#include <string>

GUIManager::GUIManager(GLFWwindow* window) : m_Window(window) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();
    io.FontGlobalScale = 1.2f;

    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 450");

    // 初始化大廳資料
    for (int i = 0; i < 8; i++) {
        lobbySlots[i].playerID = -1;
        lobbySlots[i].teamID = 0;
    }
}

GUIManager::~GUIManager() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void GUIManager::BeginFrame() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void GUIManager::Render() {
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void GUIManager::UpdateLobbyState(const PacketLobbyState& pkt) {
    for (int i = 0; i < 8; i++) {
        lobbySlots[i] = pkt.slots[i];
    }
}

// --- 繪製登入畫面 ---
void GUIManager::DrawLogin(bool& outStartServer, bool& outConnectClient) {
    if (currentState != UIState::LOGIN) return;

    // 取得視窗大小以置中
    int w, h;
    glfwGetWindowSize(m_Window, &w, &h);

    ImGui::SetNextWindowPos(ImVec2(w * 0.5f, h * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(400, 300));

    if (ImGui::Begin("Tiny Splatoon Login", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize)) {

        ImGui::Text("Welcome to Tiny-Splatoon!");
        ImGui::Separator();
        ImGui::Spacing();

        // Server 按鈕
        if (ImGui::Button("Host Server", ImVec2(380, 50))) {
            outStartServer = true;
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Client IP 輸入與按鈕
        ImGui::Text("Join Existing Game:");
        ImGui::InputText("Server IP", ipBuffer, IM_ARRAYSIZE(ipBuffer));

        if (ImGui::Button("Connect Client", ImVec2(380, 50))) {
            outConnectClient = true;
        }

        ImGui::End();
    }
}

// --- 繪製大廳畫面 ---
void GUIManager::DrawLobby(bool& outStartGame) {
    if (currentState != UIState::LOBBY) return;

    int w, h;
    glfwGetWindowSize(m_Window, &w, &h);

    // 全螢幕無邊框視窗
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImVec2((float)w, (float)h));
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBackground;

    if (ImGui::Begin("LobbyOverlay", nullptr, flags)) {

        // 1. 標題
        ImGui::SetWindowFontScale(3.0f);
        std::string title = "Tiny Splatoon Lobby";
        float textW = ImGui::CalcTextSize(title.c_str()).x;
        ImGui::SetCursorPos(ImVec2((w - textW) * 0.5f, h * 0.1f));
        ImGui::Text("%s", title.c_str());

        // 2. 繪製 8 個圓圈
        DrawLobbyCircles(w, h);
        // 武器選擇器
        ImGui::SetCursorPos(ImVec2(w * 0.1f, h * 0.6f));
        WeaponType currentWeapon = NetworkManager::Instance().GetMyWeaponType();
        // 如果有變更，這個函式會回傳 true，並更新 currentWeapon
        if (DrawWeaponSelector(currentWeapon)) {
            // 更新本地記錄
            NetworkManager::Instance().SetMyWeaponType(currentWeapon);

            // 發送封包通知 Server
            PacketLobbyChangeWeapon pkt;
            pkt.header.type = PacketType::C2S_LOBBY_CHANGE_WEAPON;
            pkt.playerID = NetworkManager::Instance().GetMyPlayerID();
            pkt.newWeapon = currentWeapon;
            NetworkManager::Instance().SendToServer(&pkt, sizeof(pkt), true);
        }

        // 3. 按鈕 (Server 顯示 Start, Client 顯示 Waiting)
        ImGui::SetWindowFontScale(2.0f);
        if (NetworkManager::Instance().IsServer()) {
            std::string btnText = "START GAME";
            float btnW = 200.0f;
            ImGui::SetCursorPos(ImVec2((w - btnW) * 0.5f, h * 0.8f));

            if (ImGui::Button(btnText.c_str(), ImVec2(btnW, 60))) {
                outStartGame = true;
            }
        }
        else {
            std::string waitText = "Waiting for Host...";
            float waitW = ImGui::CalcTextSize(waitText.c_str()).x;
            ImGui::SetCursorPos(ImVec2((w - waitW) * 0.5f, h * 0.8f));
            ImGui::Text("%s", waitText.c_str());
        }
    }
    ImGui::End();
}

// --- 繪製圓圈的輔助函式 ---
void GUIManager::DrawLobbyCircles(int w, int h) {
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    float startY = h * 0.5f;
    float radius = 40.0f;
    float spacing = 120.0f;
    // 算出起始 X，讓 8 個圓置中
    float totalW = (8 - 1) * spacing;
    float startX = (w - totalW) * 0.5f;

    for (int i = 0; i < 8; i++) {
        float x = startX + (i * spacing);
        float y = startY;

        // 決定顏色
        ImU32 color = IM_COL32(100, 100, 100, 255); // 灰色 (空)

        if (lobbySlots[i].playerID != -1) {
            if (lobbySlots[i].teamID == 1) color = IM_COL32(255, 50, 50, 255); // 紅
            if (lobbySlots[i].teamID == 2) color = IM_COL32(50, 255, 50, 255); // 綠
        }

        // 畫實心圓
        draw_list->AddCircleFilled(ImVec2(x, y), radius, color);
        // 畫邊框
        draw_list->AddCircle(ImVec2(x, y), radius, IM_COL32(255, 255, 255, 200), 0, 3.0f);

        // 畫名字 (P1, P2...)
        // 如果是自己，可以標註 (You)
        std::string name = "P" + std::to_string(i + 1);
        int myID = NetworkManager::Instance().GetMyPlayerID();
        if (lobbySlots[i].playerID != -1 && lobbySlots[i].playerID == myID) {
            name += "\n(You)";
        }

        ImGui::SetWindowFontScale(1.5f);
        float nameW = ImGui::CalcTextSize(name.c_str()).x;
        // 把 Cursor 移到圓圈下方
        ImGui::SetCursorPos(ImVec2(x - nameW * 0.5f - 10, y + radius + 15)); // 微調位置
        ImGui::Text("%s", name.c_str());
    }
}

bool GUIManager::DrawWeaponSelector(WeaponType& currentSelection) {
    bool changed = false;

    ImGui::SetWindowFontScale(1.5f);
    ImGui::Text("Select Weapon:");
    ImGui::SameLine();

    // 定義按鈕樣式
    auto DrawWeaponBtn = [&](const char* label, WeaponType type) {
        // 如果選中，改變按鈕顏色
        if (currentSelection == type) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.8f, 0.2f, 1.0f)); // 亮綠色
        }
        else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.3f, 0.3f, 0.3f, 1.0f)); // 灰色
        }

        if (ImGui::Button(label, ImVec2(120, 40))) {
            currentSelection = type;
            changed = true;
        }
        ImGui::PopStyleColor();
        };

    DrawWeaponBtn("Shooter", WeaponType::SHOOTER);
    ImGui::SameLine();
    DrawWeaponBtn("Shotgun", WeaponType::SHOTGUN);
    ImGui::SameLine();
    DrawWeaponBtn("Slosher", WeaponType::SLOSHER);

    // 顯示屬性面板
    ImGui::Spacing();
    ImGui::Indent(20.0f);
    ImGui::SetWindowFontScale(1.2f);

    std::string statsText = "";
    switch (currentSelection) {
    case WeaponType::SHOOTER:
        statsText = "Type: Automatic\nFire Rate: Fast\nInk Cost: Low\nRange: Medium";
        break;
    case WeaponType::SHOTGUN:
        statsText = "Type: Spread\nFire Rate: Slow\nInk Cost: High\nRange: Short";
        break;
    case WeaponType::SLOSHER:
        statsText = "Type: Projectile\nFire Rate: Slow\nInk Cost: Very High\nRange: Long";
        break;
    }
    ImGui::Text("%s", statsText.c_str());
    ImGui::Unindent(20.0f);

    return changed;
}