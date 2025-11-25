#include "Mesh.h"

Mesh::Mesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices) {
    this->m_Vertices = vertices;
    this->m_Indices = indices;

    SetupMesh();
}

Mesh::~Mesh() {
    glDeleteVertexArrays(1, &VAO);
    glDeleteBuffers(1, &VBO);
    if (!m_Indices.empty()) {
        glDeleteBuffers(1, &EBO);
    }
}

void Mesh::SetupMesh() {
    // 1. 產生 Buffer ID
    glGenVertexArrays(1, &VAO);
    glGenBuffers(1, &VBO);
    if (!m_Indices.empty()) {
        glGenBuffers(1, &EBO);
    }

    // 2. 綁定 VAO (開始記錄狀態)
    glBindVertexArray(VAO);

    // 3. 設定 VBO (頂點資料)
    glBindBuffer(GL_ARRAY_BUFFER, VBO);
    // 使用 sizeof(Vertex) 可以直接取得結構體的大小
    glBufferData(GL_ARRAY_BUFFER, m_Vertices.size() * sizeof(Vertex), &m_Vertices[0], GL_STATIC_DRAW);

    // 4. 設定 EBO (索引資料 - 如果有的話)
    if (!m_Indices.empty()) {
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_Indices.size() * sizeof(unsigned int), &m_Indices[0], GL_STATIC_DRAW);
    }

    // 5. 設定頂點屬性指標 (Vertex Attribute Pointers)
    // 這些 offsetof 是根據 struct Vertex 的記憶體佈局自動計算的

    // layout(location = 0) in vec3 aPos;
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);

    // layout(location = 1) in vec2 aTexCoord;
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, TexCoords));

    // layout(location = 2) in vec3 aNormal;
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, Normal));

    // 6. 解除綁定 VAO (避免不小心修改到)
    glBindVertexArray(0);
}

void Mesh::Bind() const {
    glBindVertexArray(VAO);
}

void Mesh::Unbind() const {
    glBindVertexArray(0);
}