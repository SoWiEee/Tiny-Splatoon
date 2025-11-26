#pragma once
#include "../engine/Component.h"
#include "../engine/GameObject.h"
#include "../engine/rendering/Mesh.h"
#include "../engine/rendering/Shader.h"
#include <vector>
#include <string>
#include <memory>
#include <cmath>

class MeshFactory {
public:
    static std::shared_ptr<Mesh> GetCube() {
        static std::shared_ptr<Mesh> cubeMesh = nullptr;
        if (!cubeMesh) {
            std::vector<Vertex> vertices = {
                // Back face
                {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f}, {0.0f,  0.0f, -1.0f}},
                {{ 0.5f,  0.5f, -0.5f}, {1.0f, 1.0f}, {0.0f,  0.0f, -1.0f}},
                {{ 0.5f, -0.5f, -0.5f}, {1.0f, 0.0f}, {0.0f,  0.0f, -1.0f}},
                {{ 0.5f,  0.5f, -0.5f}, {1.0f, 1.0f}, {0.0f,  0.0f, -1.0f}},
                {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f}, {0.0f,  0.0f, -1.0f}},
                {{-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f}, {0.0f,  0.0f, -1.0f}},

                // Front face
                {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}, {0.0f,  0.0f,  1.0f}},
                {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f}, {0.0f,  0.0f,  1.0f}},
                {{ 0.5f,  0.5f,  0.5f}, {1.0f, 1.0f}, {0.0f,  0.0f,  1.0f}},
                {{ 0.5f,  0.5f,  0.5f}, {1.0f, 1.0f}, {0.0f,  0.0f,  1.0f}},
                {{-0.5f,  0.5f,  0.5f}, {0.0f, 1.0f}, {0.0f,  0.0f,  1.0f}},
                {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}, {0.0f,  0.0f,  1.0f}},

                // Left face
                {{-0.5f,  0.5f,  0.5f}, {1.0f, 0.0f}, {-1.0f,  0.0f,  0.0f}},
                {{-0.5f,  0.5f, -0.5f}, {1.0f, 1.0f}, {-1.0f,  0.0f,  0.0f}},
                {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}, {-1.0f,  0.0f,  0.0f}},
                {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}, {-1.0f,  0.0f,  0.0f}},
                {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}, {-1.0f,  0.0f,  0.0f}},
                {{-0.5f,  0.5f,  0.5f}, {1.0f, 0.0f}, {-1.0f,  0.0f,  0.0f}},

                // Right face
                {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f}, {1.0f,  0.0f,  0.0f}},
                {{ 0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}, {1.0f,  0.0f,  0.0f}},
                {{ 0.5f,  0.5f, -0.5f}, {1.0f, 1.0f}, {1.0f,  0.0f,  0.0f}},
                {{ 0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}, {1.0f,  0.0f,  0.0f}},
                {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f}, {1.0f,  0.0f,  0.0f}},
                {{ 0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}, {1.0f,  0.0f,  0.0f}},

                // Bottom face
                {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}, {0.0f, -1.0f,  0.0f}},
                {{ 0.5f, -0.5f, -0.5f}, {1.0f, 1.0f}, {0.0f, -1.0f,  0.0f}},
                {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f}, {0.0f, -1.0f,  0.0f}},
                {{ 0.5f, -0.5f,  0.5f}, {1.0f, 0.0f}, {0.0f, -1.0f,  0.0f}},
                {{-0.5f, -0.5f,  0.5f}, {0.0f, 0.0f}, {0.0f, -1.0f,  0.0f}},
                {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}, {0.0f, -1.0f,  0.0f}},

                // Top face
                {{-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f}, {0.0f,  1.0f,  0.0f}},
                {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f}, {0.0f,  1.0f,  0.0f}},
                {{ 0.5f,  0.5f, -0.5f}, {1.0f, 1.0f}, {0.0f,  1.0f,  0.0f}},
                {{ 0.5f,  0.5f,  0.5f}, {1.0f, 0.0f}, {0.0f,  1.0f,  0.0f}},
                {{-0.5f,  0.5f, -0.5f}, {0.0f, 1.0f}, {0.0f,  1.0f,  0.0f}},
                {{-0.5f,  0.5f,  0.5f}, {0.0f, 0.0f}, {0.0f,  1.0f,  0.0f}}
            };
            cubeMesh = std::make_shared<Mesh>(vertices);
        }
        return cubeMesh;
    }

    static std::shared_ptr<Mesh> GetPlane() {
        static std::shared_ptr<Mesh> planeMesh = nullptr;
        if (!planeMesh) {
            std::vector<Vertex> vertices = {
                {{-0.5f, 0.0f, -0.5f}, {0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
                {{ 0.5f, 0.0f, -0.5f}, {1.0f, 1.0f}, {0.0f, 1.0f, 0.0f}},
                {{ 0.5f, 0.0f,  0.5f}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
                {{ 0.5f, 0.0f,  0.5f}, {1.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
                {{-0.5f, 0.0f,  0.5f}, {0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}},
                {{-0.5f, 0.0f, -0.5f}, {0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}}
            };
            planeMesh = std::make_shared<Mesh>(vertices);
        }
        return planeMesh;
    }

    static std::shared_ptr<Mesh> GetSphere() {
        static std::shared_ptr<Mesh> sphereMesh = nullptr;
        if (!sphereMesh) {
            std::vector<Vertex> vertices;
            std::vector<unsigned int> indices;

            const unsigned int X_SEGMENTS = 16; // 切分密度 (越高越圓，但效能越耗)
            const unsigned int Y_SEGMENTS = 16;
            const float PI = 3.14159265359f;

            for (unsigned int y = 0; y <= Y_SEGMENTS; ++y) {
                for (unsigned int x = 0; x <= X_SEGMENTS; ++x) {
                    float xSegment = (float)x / (float)X_SEGMENTS;
                    float ySegment = (float)y / (float)Y_SEGMENTS;
                    float xPos = std::cos(xSegment * 2.0f * PI) * std::sin(ySegment * PI);
                    float yPos = std::cos(ySegment * PI);
                    float zPos = std::sin(xSegment * 2.0f * PI) * std::sin(ySegment * PI);

                    Vertex v;
                    v.Position = glm::vec3(xPos, yPos, zPos) * 0.5f; // 半徑 0.5，直徑 1.0
                    v.TexCoords = glm::vec2(xSegment, ySegment);
                    v.Normal = glm::normalize(glm::vec3(xPos, yPos, zPos));
                    vertices.push_back(v);
                }
            }

            for (unsigned int y = 0; y < Y_SEGMENTS; ++y) {
                for (unsigned int x = 0; x < X_SEGMENTS; ++x) {
                    indices.push_back((y + 1) * (X_SEGMENTS + 1) + x);
                    indices.push_back(y * (X_SEGMENTS + 1) + x);
                    indices.push_back(y * (X_SEGMENTS + 1) + x + 1);

                    indices.push_back((y + 1) * (X_SEGMENTS + 1) + x);
                    indices.push_back(y * (X_SEGMENTS + 1) + x + 1);
                    indices.push_back((y + 1) * (X_SEGMENTS + 1) + x + 1);
                }
            }
            sphereMesh = std::make_shared<Mesh>(vertices, indices);
        }
        return sphereMesh;
    }
};

class MeshRenderer : public Component {
    std::shared_ptr<Mesh> m_Mesh;
    glm::vec3 m_Color;

public:
    MeshRenderer(std::string type, glm::vec3 c = glm::vec3(1.0f)) : m_Color(c) {
        if (type == "Cube") m_Mesh = MeshFactory::GetCube();
        else if (type == "Plane") m_Mesh = MeshFactory::GetPlane();
        else if (type == "Sphere") m_Mesh = MeshFactory::GetSphere();
    }

    // 支援直接傳入 Mesh (給未來載入模型用)
    MeshRenderer(std::shared_ptr<Mesh> mesh, glm::vec3 c = glm::vec3(1.0f))
        : m_Mesh(mesh), m_Color(c) {
    }

    void Draw(Shader& shader) override {
        if (m_Mesh) {
            shader.SetVec3("objectColor", m_Color);
            shader.SetMat4("model", gameObject->transform->GetModelMatrix());

            m_Mesh->Bind();
            glDrawArrays(GL_TRIANGLES, 0, m_Mesh->GetCount());
            m_Mesh->Unbind();
        }
    }
};