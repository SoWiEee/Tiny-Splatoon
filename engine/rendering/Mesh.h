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

    int GetCount() const { return m_Indices.empty() ? (int)m_Vertices.size() : (int)m_Indices.size(); }
    bool HasIndices() const { return !m_Indices.empty(); }

private:
    unsigned int VAO, VBO, EBO;
    std::vector<Vertex> m_Vertices;
    std::vector<unsigned int> m_Indices;

    void SetupMesh();
};