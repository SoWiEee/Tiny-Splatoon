#include "../restoration/CharacterShader.hpp"
#include "../restoration/CharacterRenderData.hpp"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <filesystem>
#include <iostream>
#include <set>
#include <stdexcept>

int main(int argc, char** argv) {
    if (!glfwInit()) { std::cerr << "GLFW initialization failed\n"; return 1; }
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    GLFWwindow* window = glfwCreateWindow(64, 64, "Character shader acceptance", nullptr, nullptr);
    if (!window) { glfwTerminate(); return 1; }
    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) { glfwDestroyWindow(window); glfwTerminate(); return 1; }
    int result = 0;
    try {
        bool ghostDepth=false;
        for(const auto& material:restoration::CharacterMaterials()){
            if(material.key=="squidGhost")ghostDepth=material.depthFunc==6&&!material.depthWrite&&material.transparent;
            else if(material.depthFunc!=3)throw std::runtime_error("Unexpected source depth function: "+material.key);
        }
        if(!ghostDepth)throw std::runtime_error("Source squid ghost must use GreaterDepth without depth writes");
        const std::filesystem::path directory = argc > 1 ? argv[1] : "assets/reference/characters/shaders";
        int count = 0;
        std::set<std::string> families;
        for (const auto& entry : std::filesystem::directory_iterator(directory)) {
            if (entry.path().extension() != ".vert") continue;
            auto fragment = entry.path(); fragment.replace_extension(".frag");
            restoration::CharacterShader program(restoration::CharacterShader::readSource(entry.path()), restoration::CharacterShader::readSource(fragment));
            program.use();
            program.matrices(glm::dmat4(1.0), glm::dmat4(1.0), glm::dmat4(1.0), {0.0, 0.0, 3.0});
            program.lights({}, glm::dmat4(1.0));
            program.material({});
            if (entry.path().stem() == "coil") {
                program.uniform("uCharge", 0.0); program.uniform("uFull", 0.0); program.uniform("uFlash", 0.0); program.uniform("uTime", 0.0);
            }
            std::array<glm::dmat4, restoration::CharacterBoneCount> palette;
            palette.fill(glm::dmat4(1.0)); program.bones(palette);
            if (glGetError() != GL_NO_ERROR) throw std::runtime_error("Native source uniform upload failed for " + entry.path().string());
            families.insert(entry.path().stem().string());
            ++count;
            std::cout << entry.path().stem().string() << " compiled, linked, uniforms uploaded\n";
        }
        if (count != 23 || !families.count("dark_plain") || !families.count("eye_plain") || !families.count("brollyInk_local") || !families.count("brollyCream") || !families.count("bladeAura") || !families.count("sponge"))
            throw std::runtime_error("Missing source character shader variants");
        std::cout << "Verified " << count << " source shader families on " << glGetString(GL_RENDERER) << '\n';
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; result = 1; }
    glfwDestroyWindow(window); glfwTerminate();
    return result;
}
