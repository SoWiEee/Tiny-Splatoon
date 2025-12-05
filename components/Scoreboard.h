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

class Scoreboard : public Component {
    std::unique_ptr<Mesh> m_Mesh;
    std::unique_ptr<Shader> m_Shader;
    SplatMap* m_SplatMap;

    float m_ScreenW, m_ScreenH;
    float m_Timer = 0.0f;
    bool useExternalScore = false;
    glm::vec2 m_CurrentScores = glm::vec2(0.0f);

public:
    Scoreboard(float w, float h, SplatMap* map) : m_ScreenW(w), m_ScreenH(h), m_SplatMap(map) {
        // 修改 Shader 路徑以符合你的專案結構 (如果路徑沒變就不需要改)
        m_Shader = std::make_unique<Shader>("assets/shaders/ui.vert", "assets/shaders/scoreboard.frag");
        SetupMesh();
    }

    // 設定分數 (給網路連線用)
    void SetScores(float scoreA, float scoreB) {
        m_CurrentScores = glm::vec2(scoreA, scoreB);
        useExternalScore = true; // 開啟外部模式，停止自己計算
    }

    void Update(float dt) override {
        m_Timer += dt;
        if (!useExternalScore && m_Timer > 0.5f) {
            if (m_SplatMap) {
                m_CurrentScores = m_SplatMap->CalculateScore();
            }
            m_Timer = 0.0f;
        }
    }

    // 原本的長條圖繪製 (OpenGL)
    void Draw(Shader& sceneShader) override {
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

        // OpenGL 座標 (0,0) 在左下角，所以 Top 是 ScreenH
        // 這裡設定在上方往下 50 pixel 的位置
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

    // [新增] 繪製倒數計時器 (ImGui)
    // 請在 GameScene::DrawUI() 裡面呼叫這個函式
    void DrawUITimer(float timeRemaining) {
        // 1. 格式化時間
        if (timeRemaining < 0) timeRemaining = 0;
        int minutes = (int)timeRemaining / 60;
        int seconds = (int)timeRemaining % 60;

        char timeStr[16];
        sprintf(timeStr, "%d:%02d", minutes, seconds);

        // 2. 計算位置
        // ImGui 座標 (0,0) 在左上角
        // 你的計分條在 OpenGL 是 m_ScreenH - 50 (也就是離頂部 50px)
        // 計分條高 30px，所以大約佔據頂部 20~50px 的位置
        // 我們把計時器放在計分條正下方 (ImGui Y 座標約 60~70)
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