#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>
#include <cstddef>

struct Vertex {
    glm::vec3 Position;
    glm::vec2 TexCoords;
    glm::vec3 Normal;
};

class Mesh {
public:
    Mesh(const std::vector<Vertex>& vertices, const std::vector<unsigned int>& indices = {});
    ~Mesh();

    void Bind() const;
    void Unbind() const;

    // 取得頂點數量 (如果沒有索引，就回傳頂點數；有索引回傳索引數)
    // 這對於 glDrawArrays 和 glDrawElements 都很有用
    int GetCount() const { return m_Indices.empty() ? (int)m_Vertices.size() : (int)m_Indices.size(); }

    // 檢查是否有 EBO (索引緩衝)
    bool HasIndices() const { return !m_Indices.empty(); }

private:
    unsigned int VAO, VBO, EBO;

    // 儲存資料副本 (方便之後如果要做碰撞檢測或修改模型)
    std::vector<Vertex> m_Vertices;
    std::vector<unsigned int> m_Indices;

    void SetupMesh();
};