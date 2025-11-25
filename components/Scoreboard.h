#pragma once
#include "../engine/Component.h"
#include "../engine/rendering/Shader.h"
#include "../engine/rendering/Mesh.h"
#include "../splat/SplatMap.h"
#include <memory>
#include <vector>


class Scoreboard : public Component {
    std::unique_ptr<Mesh> m_Mesh;
    std::unique_ptr<Shader> m_Shader;
    SplatMap* m_SplatMap;

    float m_ScreenW, m_ScreenH;
    float m_Timer = 0.0f;
    glm::vec2 m_CurrentScores = glm::vec2(0.0f);

public:
    Scoreboard(float w, float h, SplatMap* map) : m_ScreenW(w), m_ScreenH(h), m_SplatMap(map) {
        m_Shader = std::make_unique<Shader>("assets/shaders/ui.vert", "assets/shaders/scoreboard.frag");
        SetupMesh();
    }

    void Update(float dt) override {
        m_Timer += dt;
        if (m_Timer > 0.5f) {
            if (m_SplatMap) {
                m_CurrentScores = m_SplatMap->CalculateScore();
            }
            m_Timer = 0.0f;
        }
    }

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
        float yPos = m_ScreenH - 50.0f;

        glm::mat4 model = glm::mat4(1.0f);
        model = glm::translate(model, glm::vec3(xPos, yPos, 0.0f));
        model = glm::scale(model, glm::vec3(barWidth, barHeight, 1.0f));
        m_Shader->SetMat4("model", model);

        m_Shader->SetFloat("scoreA", m_CurrentScores.x);
        m_Shader->SetFloat("scoreB", m_CurrentScores.y);
        m_Shader->SetVec3("colorA", glm::vec3(1.0, 0.2, 0.2));
        m_Shader->SetVec3("colorB", glm::vec3(0.2, 1.0, 0.2));

        m_Mesh->Bind();
        if (m_Mesh->HasIndices())
            glDrawElements(GL_TRIANGLES, m_Mesh->GetCount(), GL_UNSIGNED_INT, 0);
        else
            glDrawArrays(GL_TRIANGLES, 0, m_Mesh->GetCount());

        m_Mesh->Unbind();

        glEnable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
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