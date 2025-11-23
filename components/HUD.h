#pragma once
#include "../engine/Component.h"
#include "../graphics/Shader.h"
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

class HUD : public Component {
    unsigned int VAO, VBO;
    Shader* uiShader;
    float screenWidth, screenHeight;

public:
    float currentInk = 1.0f; // 100% 墨水

    HUD(float width, float height) : screenWidth(width), screenHeight(height) {
        uiShader = new Shader("assets/shaders/ui.vert", "assets/shaders/ui.frag");
        SetupQuad();
    }

    ~HUD() {
        delete uiShader;
        glDeleteVertexArrays(1, &VAO);
        glDeleteBuffers(1, &VBO);
    }

    void Draw(Shader& sceneShader) override {

        glDisable(GL_DEPTH_TEST);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        uiShader->use();

        // 2. 設定正交投影矩陣 (左, 右, 下, 上)
        glm::mat4 projection = glm::ortho(0.0f, screenWidth, 0.0f, screenHeight);
        uiShader->setMat4("projection", projection);

        // 3. 設定 UI 位置與大小 (置中)
        glm::mat4 model = glm::mat4(1.0f);
        float crosshairSize = 100.0f; // 100x100 像素

        // 移到畫面中心
        model = glm::translate(model, glm::vec3((screenWidth - crosshairSize) / 2.0f, (screenHeight - crosshairSize) / 2.0f, 0.0f));
        // 縮放
        model = glm::scale(model, glm::vec3(crosshairSize, crosshairSize, 1.0f));

        uiShader->setMat4("model", model);

        // 4. 更新墨水資訊
        uiShader->setFloat("inkLevel", currentInk); // 之後可以把這個變數跟玩家邏輯綁定
        uiShader->setVec3("inkColor", glm::vec3(1.0f, 0.5f, 0.2f)); // 橘色墨水

        // 5. 繪製
        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        // 6. 恢復狀態
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
    }

    // 測試用：減少墨水
    void ConsumeInk(float amount) {
        currentInk -= amount;
        if (currentInk < 0) currentInk = 0;
    }

    // 測試用：補充墨水
    void RefillInk() {
        currentInk += 0.01f;
        if (currentInk > 1.0f) currentInk = 1.0f;
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
};