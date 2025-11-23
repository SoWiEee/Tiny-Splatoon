#pragma once
#include "../engine/Component.h"
#include "../engine/GameObject.h"
#include "../graphics/Shader.h"
#include <vector>
#include <string>

struct Vertex {
    glm::vec3 Position;
    glm::vec2 TexCoords;
    glm::vec3 Normal;
};

class MeshRenderer : public Component {
    unsigned int VAO, VBO;
    std::vector<Vertex> vertices;
    glm::vec3 color;

public:
    MeshRenderer(std::string type, glm::vec3 c = glm::vec3(1.0f)) : color(c) {
        if (type == "Cube") SetupCube();
        else if (type == "Plane") SetupPlane();
        SetupMesh();
    }

    ~MeshRenderer() {
        glDeleteVertexArrays(1, &VAO);
        glDeleteBuffers(1, &VBO);
    }

    // 確保這裡是 override，並且設定 model 矩陣
    void Draw(Shader& shader) override {
        shader.setVec3("objectColor", color);
        shader.setMat4("model", gameObject->transform->GetModelMatrix());

        glBindVertexArray(VAO);
        glDrawArrays(GL_TRIANGLES, 0, vertices.size());
        glBindVertexArray(0);
    }

private:
    void SetupMesh() {
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);

        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        if (!vertices.empty()) {
            glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), &vertices[0], GL_STATIC_DRAW);
        }

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, TexCoords));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Normal));

        glBindVertexArray(0);
    }

    void SetupPlane() {
        vertices = {
            {{-0.5f, 0.0f, -0.5f}, {0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
            {{ 0.5f, 0.0f, -0.5f}, {1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
            {{ 0.5f, 0.0f,  0.5f}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
            {{ 0.5f, 0.0f,  0.5f}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
            {{-0.5f, 0.0f,  0.5f}, {0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
            {{-0.5f, 0.0f, -0.5f}, {0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}}
        };
    }

    void SetupCube() {
        vertices = {
            // 為了簡潔，請確保這裡填入了我上一則回覆中完整的 36 個頂點資料
            // 包含 Back, Front, Left, Right, Bottom, Top 六個面
            // 如果這裡是空的，子彈也會看不見
            {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f}, {0.0f,  0.0f, -1.0f}},
            {{ 0.5f,  0.5f, -0.5f}, {1.0f, 1.0f}, {0.0f,  0.0f, -1.0f}},
            {{ 0.5f, -0.5f, -0.5f}, {1.0f, 0.0f}, {0.0f,  0.0f, -1.0f}},
            {{ 0.5f,  0.5f, -0.5f}, {1.0f, 1.0f}, {0.0f,  0.0f, -1.0f}},
            {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f}, {0.0f,  0.0f, -1.0f}},
            {{-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f}, {0.0f,  0.0f, -1.0f}},

            {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}, {0.0f,  0.0f,  1.0f}},
            {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f}, {0.0f,  0.0f,  1.0f}},
            {{ 0.5f,  0.5f,  0.5f}, {1.0f, 1.0f}, {0.0f,  0.0f,  1.0f}},
            {{ 0.5f,  0.5f,  0.5f}, {1.0f, 1.0f}, {0.0f,  0.0f,  1.0f}},
            {{-0.5f,  0.5f,  0.5f}, {0.0f, 1.0f}, {0.0f,  0.0f,  1.0f}},
            {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}, {0.0f,  0.0f,  1.0f}},

            {{-0.5f,  0.5f,  0.5f}, {1.0f, 0.0f}, {-1.0f,  0.0f,  0.0f}},
            {{-0.5f,  0.5f, -0.5f}, {1.0f, 1.0f}, {-1.0f,  0.0f,  0.0f}},
            {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}, {-1.0f,  0.0f,  0.0f}},
            {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}, {-1.0f,  0.0f,  0.0f}},
            {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}, {-1.0f,  0.0f,  0.0f}},
            {{-0.5f,  0.5f,  0.5f}, {1.0f, 0.0f}, {-1.0f,  0.0f,  0.0f}},

            {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f}, {1.0f,  0.0f,  0.0f}},
            {{ 0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}, {1.0f,  0.0f,  0.0f}},
            {{ 0.5f,  0.5f, -0.5f}, {1.0f, 1.0f}, {1.0f,  0.0f,  0.0f}},
            {{ 0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}, {1.0f,  0.0f,  0.0f}},
            {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f}, {1.0f,  0.0f,  0.0f}},
            {{ 0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}, {1.0f,  0.0f,  0.0f}},

            {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}, {0.0f, -1.0f,  0.0f}},
            {{ 0.5f, -0.5f, -0.5f}, {1.0f, 1.0f}, {0.0f, -1.0f,  0.0f}},
            {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f}, {0.0f, -1.0f,  0.0f}},
            {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f}, {0.0f, -1.0f,  0.0f}},
            {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}, {0.0f, -1.0f,  0.0f}},
            {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}, {0.0f, -1.0f,  0.0f}},

            {{-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f}, {0.0f,  1.0f,  0.0f}},
            {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f}, {0.0f,  1.0f,  0.0f}},
            {{ 0.5f,  0.5f, -0.5f}, {1.0f, 1.0f}, {0.0f,  1.0f,  0.0f}},
            {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f}, {0.0f,  1.0f,  0.0f}},
            {{-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f}, {0.0f,  1.0f,  0.0f}},
            {{-0.5f,  0.5f,  0.5f}, {0.0f, 0.0f}, {0.0f,  1.0f,  0.0f}}
        };
    }
};