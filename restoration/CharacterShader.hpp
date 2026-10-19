#pragma once

#include "CharacterPose.hpp"
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace restoration {

struct CharacterLightState {
    glm::vec3 ambient{0.0f};
    glm::vec3 sunDirection{0.0f, 1.0f, 0.0f}, sunColor{1.0f};
    glm::vec3 skyDirection{0.0f, 1.0f, 0.0f}, skyColor{0.0f}, groundColor{0.0f};
    float exposure = 1.0f;
};

// Compiles the exported source shader. OpenGL resource ownership stays local to
// the renderer, while pose and asset data remain usable without a GL context.
class CharacterShader {
public:
    CharacterShader(const std::string& vertex, const std::string& fragment);
    ~CharacterShader();
    CharacterShader(const CharacterShader&) = delete;
    CharacterShader& operator=(const CharacterShader&) = delete;
    void use() const;
    int attribute(const std::string& name) const;
    void uniform(const std::string& name, const std::vector<double>& values) const;
    void uniform(const std::string& name, double value) const;
    void matrices(const glm::dmat4& model, const glm::dmat4& view, const glm::dmat4& projection, glm::dvec3 camera) const;
    void lights(const CharacterLightState& lighting, const glm::dmat4& view) const;
    void material(const CharacterMaterialState& material) const;
    void bones(const std::array<glm::dmat4, CharacterBoneCount>& palette, unsigned textureUnit = 7);
    static std::string readSource(const std::filesystem::path& path);
private:
    struct Uniform { int location = -1, size = 0; unsigned type = 0; };
    unsigned program_ = 0, boneTexture_ = 0;
    std::unordered_map<std::string, Uniform> uniforms_;
};

} // namespace restoration
