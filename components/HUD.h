#pragma once
#include "../engine/Component.h"
#include "../engine/rendering/Shader.h"
#include "../engine/rendering/Texture.h"
#include "../gui/GUIManager.h"
#include <imgui.h>
#include <vector>
#include <deque>
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

struct KillLog {
    std::string text;
    int killerTeam; // 1=red, 2=green
    float timer;    // 顯示時間
};

class HUD : public Component {
    unsigned int VAO, VBO;
    Shader* uiShader;
    float screenWidth, screenHeight;
    std::shared_ptr<Texture> damageTex;
    std::deque<KillLog> killLogs;

public:
    float currentInk = 1.0f; // 100% 墨水
    float hitMarkerTimer = 0.0f;

    HUD(float width, float height) : screenWidth(width), screenHeight(height) {
        uiShader = new Shader("../assets/shaders/ui.vert", "../assets/shaders/ui.frag");
        SetupQuad();
        damageTex = std::make_shared<Texture>();
        damageTex->Load("assets/textures/damage_vignette.png");
    }

    ~HUD() {
        delete uiShader;
        glDeleteVertexArrays(1, &VAO);
        glDeleteBuffers(1, &VBO);
    }

    void Update(float dt) {
        if (hitMarkerTimer > 0.0f) {
            hitMarkerTimer -= dt;
        }
        for (auto it = killLogs.begin(); it != killLogs.end(); ) {
            it->timer -= dt;
            if (it->timer <= 0) {
                it = killLogs.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    void Draw(Shader& sceneShader) override {

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        uiShader->Bind();

        // 2. 設定正交投影矩陣 (左, 右, 下, 上)
        glm::mat4 projection = glm::ortho(0.0f, screenWidth, 0.0f, screenHeight);
        uiShader->SetMat4("projection", projection);

        // 3. 設定 UI 位置與大小 (置中)
        glm::mat4 model = glm::mat4(1.0f);
        float crosshairSize = 100.0f; // 100x100 像素

        // 移到畫面中心
        model = glm::translate(model, glm::vec3((screenWidth - crosshairSize) / 2.0f, (screenHeight - crosshairSize) / 2.0f, 0.0f));
        // 縮放
        model = glm::scale(model, glm::vec3(crosshairSize, crosshairSize, 1.0f));

        uiShader->SetMat4("model", model);

        // 4. 更新墨水資訊
        uiShader->SetFloat("inkLevel", currentInk); // 之後可以把這個變數跟玩家邏輯綁定
        uiShader->SetVec3("inkColor", glm::vec3(1.0f, 0.5f, 0.2f)); // 橘色墨水

        // 5. 繪製
        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        // 6. 恢復狀態
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
    }

    void DrawOverlay(float hpPercent, float specialPercent) {

        // 1. 命中標記
        if (hitMarkerTimer > 0.0f) DrawHitMarkerImGui();

        // 2. 受傷濾鏡
        if (hpPercent < 1.0f) DrawDamageVignette(hpPercent);

        // 3. 擊殺列表
        DrawKillFeed();

        // 4. [新增] 大招計量表
        DrawSpecialGauge(specialPercent);
    }

    void ConsumeInk(float amount) {
        currentInk -= amount;
        if (currentInk < 0) currentInk = 0;
    }

    void RefillInk(float amount) {
        currentInk += amount;
        if (currentInk > 1.0f) currentInk = 1.0f;
    }

    // 加入一條擊殺訊息
    void AddKillLog(int killerID, int victimID, int kTeam, int vTeam) {
        KillLog log;
        log.killerTeam = kTeam;
        log.timer = 4.0f; // 顯示 4 秒

        // 簡單轉換 ID 為文字
        std::string kName = (killerID == 0) ? "Host" : (killerID == 100 ? "AI" : "P" + std::to_string(killerID));
        std::string vName = (victimID == 0) ? "Host" : (victimID == 100 ? "AI" : "P" + std::to_string(victimID));

        // 格式: "Killer -> Victim"
        log.text = kName + " > " + vName;

        killLogs.push_back(log);
        if (killLogs.size() > 5) killLogs.pop_front(); // 最多顯示 5 條
    }

    void ShowHitMarker() {
        hitMarkerTimer = 0.2f; // 顯示 0.2 秒
    }

private:
    void SetupQuad() {
        // 一個簡單的 2D Quad (0,0 到 1,1)
        float vertices[] = {
            // Pos      // Tex
            0.0f, 1.0f, 0.0f, 1.0f,
            1.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 0.0f,

            0.0f, 1.0f, 0.0f, 1.0f,
            1.0f, 1.0f, 1.0f, 1.0f,
            1.0f, 0.0f, 1.0f, 0.0f
        };

        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);

        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    }

    // 使用 ImGui 畫紅色 X
    void DrawHitMarkerImGui() {
        // 設定一個透明、無邊框、不可互動的視窗覆蓋全螢幕
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(screenWidth, screenHeight));
        ImGui::Begin("HitOverlay", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        ImVec2 center(screenWidth * 0.5f, screenHeight * 0.5f);
        float size = 15.0f; // 叉叉的大小

        // 畫紅色 X
        // 左上 -> 右下
        drawList->AddLine(
            ImVec2(center.x - size, center.y - size),
            ImVec2(center.x + size, center.y + size),
            IM_COL32(255, 50, 50, 255), // 紅色
            3.0f // 線條粗細
        );
        // 右上 -> 左下
        drawList->AddLine(
            ImVec2(center.x + size, center.y - size),
            ImVec2(center.x - size, center.y + size),
            IM_COL32(255, 50, 50, 255),
            3.0f
        );

        ImGui::End();
    }

    void DrawDamageVignette(float hpPercent) {
        // 設定全螢幕視窗
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(screenWidth, screenHeight));
        ImGui::Begin("DamageOverlay", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);

        // 計算透明度：血越少，Alpha 越高
        float alpha = (1.0f - hpPercent) * 0.9f;

        // 如果血量 > 90%，可能不想顯示，讓它全透明
        if (hpPercent > 0.9f) alpha = 0.0f;

        // 繪製圖片
        ImGui::GetWindowDrawList()->AddImage(
            (intptr_t)damageTex->ID,
            ImVec2(0, 0),
            ImVec2(screenWidth, screenHeight),
            ImVec2(0, 0), ImVec2(1, 1),
            IM_COL32(255, 255, 255, (int)(alpha * 255)) // 控制 Alpha
        );

        ImGui::End();
    }

    // 繪製擊殺列表
    void DrawKillFeed() {
        if (killLogs.empty()) return;

        // 設定視窗在右上角
        float pad = 10.0f;
        ImGui::SetNextWindowPos(ImVec2(screenWidth - 200 - pad, pad));
        ImGui::SetNextWindowSize(ImVec2(200, 0)); // 自動高度

        ImGui::Begin("KillFeed", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);

        for (const auto& log : killLogs) {
            // 根據隊伍設定顏色 (簡單紅綠)
            ImVec4 color = (log.killerTeam == 1) ? ImVec4(1, 0.2f, 0.2f, 1) : ImVec4(0.2f, 1, 0.2f, 1);

            // 隨時間淡出
            float alpha = 1.0f;
            if (log.timer < 1.0f) alpha = log.timer;
            color.w = alpha;

            ImGui::TextColored(color, "%s", log.text.c_str());
        }

        ImGui::End();
    }

    // 繪製右上角大招表
    void DrawSpecialGauge(float percent) {
        // 設定視窗屬性 (透明、無邊框)
        ImGui::SetNextWindowPos(ImVec2(screenWidth - 120, 20)); // 右上角，留點邊距
        ImGui::SetNextWindowSize(ImVec2(100, 100));

        ImGui::Begin("SpecialGauge", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);

        ImDrawList* drawList = ImGui::GetWindowDrawList();

        // 圓心與半徑
        ImVec2 center = ImGui::GetCursorScreenPos(); // 取得當前視窗內的游標位置
        center.x += 50.0f; // 往右移半個視窗寬度
        center.y += 50.0f; // 往下移半個視窗高度
        float radius = 35.0f;
        float thickness = 8.0f;

        // 1. 畫背景圓圈 (深灰色)
        drawList->AddCircle(center, radius, IM_COL32(50, 50, 50, 150), 0, thickness);

        // 2. 畫進度圓弧
        // 角度：從 -90度 (上方) 開始
        float startAngle = -3.14159f / 2.0f;
        float endAngle = startAngle + (percent * 3.14159f * 2.0f);

        // 決定顏色：集滿時變亮黃色/金色，未滿時用一般的亮色
        ImU32 barColor;
        if (percent >= 1.0f) {
            // 讓它閃爍 (Pulsing effect)
            float time = (float)ImGui::GetTime();
            float flash = (sin(time * 10.0f) + 1.0f) * 0.5f; // 0~1
            // 混合 黃色 與 白色
            int r = 255;
            int g = 215 + (int)(40 * flash);
            int b = 0 + (int)(255 * flash);
            barColor = IM_COL32(r, g, b, 255);
        }
        else {
            barColor = IM_COL32(0, 255, 255, 200); // 青色
        }

        // 使用 PathArcTo 畫圓弧
        // 注意：如果 percent 是 0，endAngle == startAngle，可能畫不出東西，加個保護
        if (percent > 0.01f) {
            drawList->PathArcTo(center, radius, startAngle, endAngle);
            drawList->PathStroke(barColor, 0, thickness);
        }

        // 3. 文字提示
        if (percent >= 1.0f) {
            // 計算文字大小以置中
            const char* text = "PRESS Q";
            ImVec2 textSize = ImGui::CalcTextSize(text);
            drawList->AddText(ImVec2(center.x - textSize.x / 2, center.y - textSize.y / 2), IM_COL32(255, 255, 255, 255), text);
        }

        ImGui::End();
    }
};