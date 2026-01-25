#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <string>
#include <unordered_map>

class Shader {
public:
    unsigned int ID; // OpenGL Program ID

    // 建構子：讀取路徑、編譯、連結
    Shader(const std::string& vertexPath, const std::string& fragmentPath);

    // 解構子：釋放 OpenGL 資源 (RAII)
    ~Shader();

    void Bind() const;
    void Unbind() const;

    void SetBool(const std::string& name, bool value) const;
    void SetInt(const std::string& name, int value) const;
    void SetFloat(const std::string& name, float value) const;
    void SetVec2(const std::string& name, const glm::vec2& value) const;
    void SetVec3(const std::string& name, const glm::vec3& value) const;
    void SetVec4(const std::string& name, const glm::vec4& value) const;
    void SetMat4(const std::string& name, const glm::mat4& mat) const;

private:
    // 檢查編譯錯誤的輔助函式
    void CheckCompileErrors(unsigned int shader, std::string type);

    // Uniform 快取系統
    mutable std::unordered_map<std::string, int> m_UniformLocationCache;

    // 取得 Uniform 位置 (如果快取有就直接回傳，沒有就去查 OpenGL)
    int GetUniformLocation(const std::string& name) const;
};