#pragma once
#include "../engine/Component.h"
#include "../engine/rendering/Shader.h"
#include "../engine/rendering/Mesh.h"
#include "../splat/SplatMap.h"
#include "imgui.h"
#include <memory>
#include <vector>
#include <string>
#include <cstdio>
#include <cmath> 
#include <algorithm>

struct UIPlayerStatus {
    int id;
    int team;      // 1 or 2
    bool isDead;   // 是否死亡
    bool isSelf;   // 是否為自己 (可以畫個黃色外框特別標示)
    bool hasSpecial; // 是否大招集滿 (選用，讓圖示發光)
};

class Scoreboard : public Component {
    std::unique_ptr<Mesh> m_Mesh;
    std::unique_ptr<Shader> m_Shader;
    SplatMap* m_SplatMap;

    float m_ScreenW, m_ScreenH;
    float m_Timer = 0.0f;
    bool useExternalScore = false;
    glm::vec2 m_CurrentScores = glm::vec2(0.0f);

    bool m_ShowScoreBar = false;

public:
    Scoreboard(float w, float h, SplatMap* map) : m_ScreenW(w), m_ScreenH(h), m_SplatMap(map) {
        m_Shader = std::make_unique<Shader>("assets/shaders/ui.vert", "assets/shaders/scoreboard.frag");
        SetupMesh();
    }

    // 設定分數 (給網路連線用)
    void SetScores(float scoreA, float scoreB) {
        m_CurrentScores = glm::vec2(scoreA, scoreB);
        useExternalScore = true; // 開啟外部模式，停止自己計算
    }

    // 設定是否顯示分數條
    void SetShowScoreBar(bool show) {
        m_ShowScoreBar = show;
    }

    void Update(float dt) override {
        if (m_ShowScoreBar) {
            m_Timer += dt;
            if (!useExternalScore && m_Timer > 0.5f) {
                if (m_SplatMap) {
                    std::pair<float, float> result = m_SplatMap->CalculatePercentages();
                    m_CurrentScores = glm::vec2(result.first, result.second);
                }
                m_Timer = 0.0f;
            }
        }
    }

    // 原本的長條圖繪製 (OpenGL)
    void Draw(Shader& sceneShader) override {

        if (!m_ShowScoreBar) return;

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        m_Shader->Bind();

        glm::mat4 projection = glm::ortho(0.0f, m_ScreenW, 0.0f, m_ScreenH);
        m_Shader->SetMat4("projection", projection);

        // top center
        float barWidth = m_ScreenW * 0.6f;
        float barHeight = 30.0f;
        float xPos = (m_ScreenW - barWidth) / 2.0f;
        float yPos = m_ScreenH - 50.0f;

        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(xPos, yPos, 0.0f));
        model = glm::scale(model, glm::vec3(barWidth, barHeight, 1.0f));
        m_Shader->SetMat4("model", model);

        m_Shader->SetFloat("scoreA", m_CurrentScores.x);
        m_Shader->SetFloat("scoreB", m_CurrentScores.y);
        m_Shader->SetVec3("colorA", glm::vec3(1.0, 0.2, 0.2)); // 紅隊
        m_Shader->SetVec3("colorB", glm::vec3(0.2, 1.0, 0.2)); // 綠隊

        m_Mesh->Bind();
        if (m_Mesh->HasIndices())
            glDrawElements(GL_TRIANGLES, m_Mesh->GetCount(), GL_UNSIGNED_INT, 0);
        else
            glDrawArrays(GL_TRIANGLES, 0, m_Mesh->GetCount());

        m_Mesh->Unbind();

        glEnable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
    }

    // 繪製隊伍狀態圖示 (Squid Icons)
    void DrawPlayerIcons(const std::vector<UIPlayerStatus>& players) {
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(m_ScreenW, 100)); // 頂部區域

        ImGui::Begin("PlayerIcons", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);

        ImDrawList* dl = ImGui::GetWindowDrawList();

        // UI 配置參數
        float iconSize = 25.0f; // 圖示半徑
        float spacing = 60.0f;  // 圖示間距
        float topMargin = 40.0f;

        // 中心點 (計時器在中間)
        float centerX = m_ScreenW / 2.0f;
        float timerGap = 120.0f; // 中間留白給計時器

        // 分類隊伍
        std::vector<UIPlayerStatus> team1;
        std::vector<UIPlayerStatus> team2;
        for (const auto& p : players) {
            if (p.team == 1) team1.push_back(p);
            else team2.push_back(p);
        }

        // 繪製 Team 1 (紅色，在左邊，往左延伸)
        for (int i = 0; i < team1.size(); i++) {
            // 從中間往左排
            float x = centerX - (timerGap / 2) - (i * spacing) - 30.0f;
            float y = topMargin;
            DrawSingleIcon(dl, team1[i], ImVec2(x, y), iconSize, IM_COL32(255, 50, 50, 255));
        }

        // 繪製 Team 2 (綠色，在右邊，往右延伸)
        for (int i = 0; i < team2.size(); i++) {
            // 從中間往右排
            float x = centerX + (timerGap / 2) + (i * spacing) + 30.0f;
            float y = topMargin;
            DrawSingleIcon(dl, team2[i], ImVec2(x, y), iconSize, IM_COL32(50, 255, 50, 255));
        }

        ImGui::End();
    }

    // 繪製倒數計時器
    void DrawUITimer(float timeRemaining) {
        // 1. 格式化時間
        if (timeRemaining < 0) timeRemaining = 0;
        int minutes = (int)timeRemaining / 60;
        int seconds = (int)timeRemaining % 60;

        char timeStr[16];
        sprintf(timeStr, "%d:%02d", minutes, seconds);

        float imguiY = 60.0f;

        ImGui::SetNextWindowPos(ImVec2(m_ScreenW / 2 - 60, imguiY));
        ImGui::SetNextWindowSize(ImVec2(120, 80));

        ImGui::Begin("GameTimer", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground |
            ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing);

        // 3. 設定樣式
        ImGui::SetWindowFontScale(2.5f); // 字體放大

        ImVec4 textColor = ImVec4(1, 1, 1, 1); // 白色

        // 最後 30 秒閃爍紅色
        if (timeRemaining <= 30.0f && timeRemaining > 0.0f) {
            float flash = sin(ImGui::GetTime() * 15.0f) * 0.5f + 0.5f;
            textColor = ImVec4(1.0f, flash, flash, 1.0f);
        }

        // 4. 文字置中與陰影
        float textWidth = ImGui::CalcTextSize(timeStr).x;
        ImGui::SetCursorPosX((120 - textWidth) / 2); // 視窗寬 120

        // 黑色陰影 (Offset +2, +2)
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddText(
            ImVec2(pos.x + 2, pos.y + 2),
            IM_COL32(0, 0, 0, 200),
            timeStr
        );

        // 本體文字
        ImGui::TextColored(textColor, "%s", timeStr);

        ImGui::End();
    }

private:
    void DrawSingleIcon(ImDrawList* dl, const UIPlayerStatus& p, ImVec2 center, float radius, ImU32 teamColor) {
        // 1. 決定顏色
        ImU32 color = teamColor;

        if (p.isDead) {
            // 死亡變灰/暗
            color = IM_COL32(80, 80, 80, 200);
        }

        // 2. 畫圓 (代表魷魚)
        dl->AddCircleFilled(center, radius, color);

        // 3. 畫外框
        if (p.isSelf) {
            // 自己有黃色外框
            dl->AddCircle(center, radius + 2.0f, IM_COL32(255, 255, 0, 255), 0, 3.0f);
        }
        else {
            // 別人有黑色外框
            dl->AddCircle(center, radius, IM_COL32(0, 0, 0, 255), 0, 2.0f);
        }

        // 4. 死亡顯示 X
        if (p.isDead) {
            float s = radius * 0.6f;
            dl->AddLine(ImVec2(center.x - s, center.y - s), ImVec2(center.x + s, center.y + s), IM_COL32(200, 200, 200, 255), 3.0f);
            dl->AddLine(ImVec2(center.x + s, center.y - s), ImVec2(center.x - s, center.y + s), IM_COL32(200, 200, 200, 255), 3.0f);
        }

        // 5. 大招亮燈 (選用)
        if (p.hasSpecial && !p.isDead) {
            // 畫一個發光的圈
            float flash = (sin(ImGui::GetTime() * 10.0f) + 1.0f) * 0.5f;
            ImU32 glowCol = IM_COL32(255, 255, 255, (int)(150 * flash));
            dl->AddCircle(center, radius + 5.0f, glowCol, 0, 2.0f);
        }
    }

    void SetupMesh() {
        std::vector<Vertex> vertices = {
            // Pos              // UV       // Normal
            {{0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}, {0,0,1}},
            {{1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}, {0,0,1}},
            {{0.0f, 0.0f, 0.0f}, {0.0f, 0.0f}, {0,0,1}},

            {{0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}, {0,0,1}},
            {{1.0f, 1.0f, 0.0f}, {1.0f, 1.0f}, {0,0,1}},
            {{1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}, {0,0,1}}
        };

        m_Mesh = std::make_unique<Mesh>(vertices);
    }
};