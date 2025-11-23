
class MeshRenderer : public Component {
    unsigned int VAO, VBO, EBO;
    int indexCount;
public:
    MeshRenderer(const char* shapeType) { // "Cube" or "Plane"
        // 1. 根據 shapeType 生成頂點數據 (Vertices & UVs)
        // 2. 綁定 OpenGL VAO/VBO
        // (這部分的繁瑣程式碼建議直接讓 AI 生成)
    }

    void Draw(Shader& shader) override {
        shader.SetMat4("model", gameObject->transform->GetModelMatrix());
        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0);
    }
};