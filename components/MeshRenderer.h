#pragma once
#include "../engine/Component.h"
#include "../engine/GameObject.h"
#include "../engine/rendering/Mesh.h"
#include "../engine/rendering/Shader.h"
#include <vector>
#include <string>
#include <memory>

class MeshFactory {
public:
    static std::shared_ptr<Mesh> GetCube() {
        static std::shared_ptr<Mesh> cubeMesh = nullptr;
        if (!cubeMesh) {
            // 定義 36 個頂點 (位置, UV, 法線)
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
};

class MeshRenderer : public Component {
    std::shared_ptr<Mesh> m_Mesh;
    glm::vec3 m_Color;

public:
    // 建構子：根據字串決定載入哪個 Mesh
    MeshRenderer(std::string type, glm::vec3 c = glm::vec3(1.0f)) : m_Color(c) {
        if (type == "Cube") {
            m_Mesh = MeshFactory::GetCube();
        }
        else if (type == "Plane") {
            m_Mesh = MeshFactory::GetPlane();
        }
    }

    // 支援直接傳入 Mesh (給未來載入模型用)
    MeshRenderer(std::shared_ptr<Mesh> mesh, glm::vec3 c = glm::vec3(1.0f))
        : m_Mesh(mesh), m_Color(c) {
    }

    // 繪製
    void Draw(Shader& shader) override {
        if (m_Mesh) {
            // 1. 設定顏色
            shader.SetVec3("objectColor", m_Color);

            // 2. 設定 Model Matrix
            shader.SetMat4("model", gameObject->transform->GetModelMatrix());

            // 3. 綁定 Mesh
            m_Mesh->Bind();

            // 4. 繪製
            // 根據 Mesh 是否有索引來決定用 DrawElements 還是 DrawArrays
            // 這裡因為 Cube/Plane 都沒有索引，直接用 DrawArrays
            glDrawArrays(GL_TRIANGLES, 0, m_Mesh->GetCount());

            // 5. 解除綁定
            m_Mesh->Unbind();
        }
    }
};