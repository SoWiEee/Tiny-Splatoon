#pragma once
#include "../engine/Component.h"
#include "../engine/GameObject.h"
#include "../graphics/Shader.h"
#include <vector>

// 定義頂點結構
struct Vertex {
    glm::vec3 Position;
    glm::vec2 TexCoords;
    glm::vec3 Normal;
};

class MeshRenderer : public Component {
    unsigned int VAO, VBO;
    std::vector<Vertex> vertices;
    glm::vec3 color; // Debug 用顏色

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

    void Draw(Shader& shader) override {
        shader.setVec3("objectColor", color); // 設定顏色
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
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), &vertices[0], GL_STATIC_DRAW);

        // Position
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
        // TexCoords
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, TexCoords));
        // Normal
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Normal));

        glBindVertexArray(0);
    }

    void SetupPlane() {
        vertices = {
            // Pos                // UV      // Normal
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
            // Back face
            // Position           // TexCoords // Normal
            {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f}, {0.0f,  0.0f, -1.0f}}, // Bottom-left
            {{ 0.5f,  0.5f, -0.5f}, {1.0f, 1.0f}, {0.0f,  0.0f, -1.0f}}, // Top-right
            {{ 0.5f, -0.5f, -0.5f}, {1.0f, 0.0f}, {0.0f,  0.0f, -1.0f}}, // Bottom-right         
            {{ 0.5f,  0.5f, -0.5f}, {1.0f, 1.0f}, {0.0f,  0.0f, -1.0f}}, // Top-right
            {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f}, {0.0f,  0.0f, -1.0f}}, // Bottom-left
            {{-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f}, {0.0f,  0.0f, -1.0f}}, // Top-left

            // Front face
            {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}, {0.0f,  0.0f,  1.0f}}, // Bottom-left
            {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f}, {0.0f,  0.0f,  1.0f}}, // Bottom-right
            {{ 0.5f,  0.5f,  0.5f}, {1.0f, 1.0f}, {0.0f,  0.0f,  1.0f}}, // Top-right
            {{ 0.5f,  0.5f,  0.5f}, {1.0f, 1.0f}, {0.0f,  0.0f,  1.0f}}, // Top-right
            {{-0.5f,  0.5f,  0.5f}, {0.0f, 1.0f}, {0.0f,  0.0f,  1.0f}}, // Top-left
            {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}, {0.0f,  0.0f,  1.0f}}, // Bottom-left

            // Left face
            {{-0.5f,  0.5f,  0.5f}, {1.0f, 0.0f}, {-1.0f,  0.0f,  0.0f}}, // Top-right
            {{-0.5f,  0.5f, -0.5f}, {1.0f, 1.0f}, {-1.0f,  0.0f,  0.0f}}, // Top-left
            {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}, {-1.0f,  0.0f,  0.0f}}, // Bottom-left
            {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}, {-1.0f,  0.0f,  0.0f}}, // Bottom-left
            {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}, {-1.0f,  0.0f,  0.0f}}, // Bottom-right
            {{-0.5f,  0.5f,  0.5f}, {1.0f, 0.0f}, {-1.0f,  0.0f,  0.0f}}, // Top-right

            // Right face
            {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f}, {1.0f,  0.0f,  0.0f}}, // Top-left
            {{ 0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}, {1.0f,  0.0f,  0.0f}}, // Bottom-right
            {{ 0.5f,  0.5f, -0.5f}, {1.0f, 1.0f}, {1.0f,  0.0f,  0.0f}}, // Top-right         
            {{ 0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}, {1.0f,  0.0f,  0.0f}}, // Bottom-right
            {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f}, {1.0f,  0.0f,  0.0f}}, // Top-left
            {{ 0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}, {1.0f,  0.0f,  0.0f}}, // Bottom-left     

            // Bottom face
            {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}, {0.0f, -1.0f,  0.0f}}, // Top-right
            {{ 0.5f, -0.5f, -0.5f}, {1.0f, 1.0f}, {0.0f, -1.0f,  0.0f}}, // Top-left
            {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f}, {0.0f, -1.0f,  0.0f}}, // Bottom-left
            {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f}, {0.0f, -1.0f,  0.0f}}, // Bottom-left
            {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}, {0.0f, -1.0f,  0.0f}}, // Bottom-right
            {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}, {0.0f, -1.0f,  0.0f}}, // Top-right

            // Top face
            {{-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f}, {0.0f,  1.0f,  0.0f}}, // Top-left
            {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f}, {0.0f,  1.0f,  0.0f}}, // Bottom-right
            {{ 0.5f,  0.5f, -0.5f}, {1.0f, 1.0f}, {0.0f,  1.0f,  0.0f}}, // Top-right     
            {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f}, {0.0f,  1.0f,  0.0f}}, // Bottom-right
            {{-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f}, {0.0f,  1.0f,  0.0f}}, // Top-left
            {{-0.5f,  0.5f,  0.5f}, {0.0f, 0.0f}, {0.0f,  1.0f,  0.0f}}  // Bottom-left        
        };
    }
};