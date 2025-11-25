#pragma once
#include "../engine/Component.h"
#include "../graphics/InkMap.h"
#include "../engine/rendering/Shader.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class Scoreboard : public Component {
    unsigned int VAO, VBO;
    Shader* barShader;
    InkMap* inkMap;

    float screenW, screenH;
    float timer = 0.0f;
    glm::vec2 currentScores = glm::vec2(0.0f); // 緩存的分數

public:
    Scoreboard(float w, float h, InkMap* map) : screenW(w), screenH(h), inkMap(map) {
        barShader = new Shader("assets/shaders/ui.vert", "assets/shaders/scoreboard.frag");
        SetupQuad();
    }

    ~Scoreboard() {
        delete barShader;
        glDeleteVertexArrays(1, &VAO);
        glDeleteBuffers(1, &VBO);
    }

    void Update(float dt) override {
        // 優化：不需要每一幀都算分數 (太耗效能)
        // 每 0.5 秒算一次即可
        timer += dt;
        if (timer > 0.5f) {
            currentScores = inkMap->CalculateScore();
            timer = 0.0f;
        }
    }

    void Draw(Shader& sceneShader) override {
        // 1. 設定 UI 狀態
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        barShader->Bind();

        // 2. 設定投影 (Orthographic)
        glm::mat4 projection = glm::ortho(0.0f, screenW, 0.0f, screenH);
        barShader->SetMat4("projection", projection);

        // 3. 設定位置與大小 (畫在螢幕頂端中央)
        float barWidth = screenW * 0.6f; // 佔螢幕寬度 60%
        float barHeight = 30.0f;         // 高度 30 像素
        float xPos = (screenW - barWidth) / 2.0f;
        float yPos = screenH - 50.0f;    // 距離頂部 50 像素

        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(xPos, yPos, 0.0f));
        model = glm::scale(model, glm::vec3(barWidth, barHeight, 1.0f));
        barShader->SetMat4("model", model);

        // 4. 傳入分數與顏色
        barShader->SetFloat("scoreA", currentScores.x); // 紅隊
        barShader->SetFloat("scoreB", currentScores.y); // 綠隊
        barShader->SetVec3("colorA", glm::vec3(1.0, 0.2, 0.2)); // 紅色
        barShader->SetVec3("colorB", glm::vec3(0.2, 1.0, 0.2)); // 綠色

        // 5. 繪製
        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        // 6. 復原
        glEnable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
    }

private:
    void SetupQuad() {
        // 標準的 0~1 Quad
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
};