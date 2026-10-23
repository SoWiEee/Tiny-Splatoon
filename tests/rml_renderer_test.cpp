#include "../src/gui/RmlRenderer.hpp"

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <RmlUi/Core.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

GLuint makeProbeProgram() {
    static constexpr char vertex[] = "#version 450 core\nvoid main(){gl_Position=vec4(0,0,0,1);}";
    static constexpr char fragment[] = "#version 450 core\nout vec4 c; void main(){c=vec4(1);}";
    const auto compile = [](GLenum type, const char* source) {
        const GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &source, nullptr);
        glCompileShader(shader);
        GLint okay = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &okay);
        if (okay != GL_TRUE) {
            glDeleteShader(shader);
            throw std::runtime_error("Could not compile GL state probe shader");
        }
        return shader;
    };
    const GLuint vs = compile(GL_VERTEX_SHADER, vertex);
    const GLuint fs = compile(GL_FRAGMENT_SHADER, fragment);
    const GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    GLint okay = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &okay);
    if (okay != GL_TRUE) {
        glDeleteProgram(program);
        throw std::runtime_error("Could not link GL state probe shader");
    }
    return program;
}

std::array<unsigned char, 4> pixel(const std::vector<unsigned char>& image, int width, int height, int x, int yTop) {
    const std::size_t offset = (static_cast<std::size_t>(height - 1 - yTop) * width + x) * 4;
    return {image[offset], image[offset + 1], image[offset + 2], image[offset + 3]};
}

std::size_t brightPixels(const std::vector<unsigned char>& image, int width, int height,
                         int x0, int y0, int x1, int y1) {
    std::size_t count = 0;
    for (int y = y0; y < y1; ++y)
        for (int x = x0; x < x1; ++x) {
            const auto sample = pixel(image, width, height, x, y);
            if (sample[0] > 80 || sample[1] > 80 || sample[2] > 80) ++count;
        }
    return count;
}
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Expected the repository source directory as argument\n";
        return 2;
    }
    constexpr int width = 128;
    constexpr int height = 96;
    if (!glfwInit()) return 1;
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_RED_BITS, 8);
    glfwWindowHint(GLFW_GREEN_BITS, 8);
    glfwWindowHint(GLFW_BLUE_BITS, 8);
    glfwWindowHint(GLFW_ALPHA_BITS, 8);
    glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_FALSE);
    GLFWwindow* window = glfwCreateWindow(width, height, "RmlRenderer smoke", nullptr, nullptr);
    if (!window) {
        glfwTerminate();
        return 1;
    }
    glfwMakeContextCurrent(window);
    if (!gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress))) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    int result = 0;
    GLuint borrowedMapTexture = 0;
    {
        restoration::RmlRenderer renderer;
        Rml::SystemInterface system;
        bool initialized = false;
        try {
            Rml::SetSystemInterface(&system);
            Rml::SetRenderInterface(&renderer);
            check(Rml::Initialise(), "RmlUi initialization failed");
            initialized = true;

            const std::filesystem::path root(argv[1]);
            const auto inter = root / "assets" / "fonts" / "Inter.ttf";
            const auto noto = root / "assets" / "fonts" / "NotoSansTC.ttf";
            check(std::filesystem::is_regular_file(inter), "Missing assets/fonts/Inter.ttf");
            check(std::filesystem::is_regular_file(noto), "Missing assets/fonts/NotoSansTC.ttf");
            check(Rml::LoadFontFace(inter.string()), "RmlUi could not load Inter.ttf");
            check(Rml::LoadFontFace(noto.string(), true), "RmlUi could not load NotoSansTC.ttf fallback");

            const unsigned char mapPixel[] = {17, 231, 73, 255};
            glGenTextures(1, &borrowedMapTexture);
            glBindTexture(GL_TEXTURE_2D, borrowedMapTexture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, mapPixel);
            renderer.SetNativeMapTexture(borrowedMapTexture);

            Rml::Context* context = Rml::CreateContext("rml-renderer-smoke", {width, height}, &renderer);
            check(context != nullptr, "Could not create RmlUi context");
            const std::string documentSource = R"RML(<rml><head><style>
html, body { margin: 0; padding: 0; background-color: transparent; }
#red { position: absolute; left: 8px; top: 8px; width: 24px; height: 24px; background-color: #ff0000; }
#clip { position: absolute; left: 48px; top: 8px; width: 16px; height: 24px; overflow: hidden; }
#green { display: block; width: 40px; height: 24px; background-color: #00ff00; }
.label { position: absolute; left: 8px; width: 112px; height: 24px; font-family: Inter; font-size: 16px; color: #ffffff; }
#latin { top: 42px; }
#cjk { top: 68px; }
#round { position: absolute; left: 104px; top: 8px; width: 24px; height: 24px; border-radius: 12px; overflow: hidden; }
#round img { width: 24px; height: 24px; }
#borrowed { position: absolute; left: 80px; top: 8px; width: 20px; height: 24px; }
</style></head><body>
<div id="red"></div><div id="clip"><div id="green"></div></div>
<img id="borrowed" src="native-map"/><div id="round"><img src="native-map"/></div>
<div id="latin" class="label">Inter Match UI</div>
<div id="cjk" class="label">繁體中文 墨水</div>
</body></rml>)RML";
            Rml::ElementDocument* document = context->LoadDocumentFromMemory(documentSource, "rml-renderer-smoke");
            check(document != nullptr, "Could not load the inline RmlUi document");
            document->Show();
            check(context->Update(), "RmlUi context update failed");
            auto* clipElement = document->GetElementById("clip");
            auto* greenElement = document->GetElementById("green");
            check(clipElement && greenElement, "RmlUi did not create the clipping elements");
            check(clipElement->GetScrollWidth() > clipElement->GetClientWidth(),
                  "RmlUi test fixture did not lay out content wider than its overflow clip");
            const GLuint probeProgram = makeProbeProgram();
            GLuint probeVao = 0, probeBuffer = 0, probeTexture0 = 0, probeTexture3 = 0;
            GLuint sampler0 = 0, sampler3 = 0;
            glGenVertexArrays(1, &probeVao);
            glGenBuffers(1, &probeBuffer);
            glGenTextures(1, &probeTexture0);
            glGenTextures(1, &probeTexture3);
            glGenSamplers(1, &sampler0);
            glGenSamplers(1, &sampler3);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            glReadBuffer(GL_BACK);
            glViewport(0, 0, width, height);
            glDisable(GL_SCISSOR_TEST);
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT);
            glUseProgram(probeProgram);
            glBindVertexArray(probeVao);
            glBindBuffer(GL_ARRAY_BUFFER, probeBuffer);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, probeTexture0);
            glBindSampler(0, sampler0);
            glActiveTexture(GL_TEXTURE3);
            glBindTexture(GL_TEXTURE_2D, probeTexture3);
            glBindSampler(3, sampler3);
            glViewport(3, 4, 91, 73);
            glEnable(GL_SCISSOR_TEST);
            glScissor(2, 3, 12, 13);
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            glEnable(GL_CULL_FACE);
            glEnable(GL_STENCIL_TEST);glStencilFuncSeparate(GL_FRONT,GL_NOTEQUAL,3,0x37);glStencilMaskSeparate(GL_FRONT,0x29);glStencilOpSeparate(GL_FRONT,GL_REPLACE,GL_INCR,GL_DECR);
            glStencilFuncSeparate(GL_BACK,GL_LESS,7,0x17);glStencilMaskSeparate(GL_BACK,0x19);glStencilOpSeparate(GL_BACK,GL_INCR,GL_KEEP,GL_REPLACE);glClearStencil(5);
            glDisable(GL_BLEND);
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO);
            glBlendEquationSeparate(GL_FUNC_REVERSE_SUBTRACT, GL_FUNC_ADD);
            check(glGetError() == GL_NO_ERROR, "RmlUi or GL state setup generated an OpenGL error");

            check(renderer.Begin(width, height), "RmlRenderer rejected a valid Begin call");
            check(context->Render(), "RmlUi context render failed");
            renderer.End();

            check(glIsEnabled(GL_STENCIL_TEST),"Renderer did not restore the stencil test");
            GLint value = 0;
            glGetIntegerv(GL_STENCIL_REF,&value);check(value==3,"Renderer did not restore front stencil reference");
            glGetIntegerv(GL_STENCIL_BACK_REF,&value);check(value==7,"Renderer did not restore back stencil reference");
            glGetIntegerv(GL_STENCIL_WRITEMASK,&value);check(value==0x29,"Renderer did not restore stencil write mask");
            glGetIntegerv(GL_STENCIL_CLEAR_VALUE,&value);check(value==5,"Renderer did not restore stencil clear value");
            glGetIntegerv(GL_CURRENT_PROGRAM, &value);
            check(value == static_cast<GLint>(probeProgram), "Renderer did not restore the active program");
            glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &value);
            check(value == static_cast<GLint>(probeVao), "Renderer did not restore the vertex array");
            glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &value);
            check(value == static_cast<GLint>(probeBuffer), "Renderer did not restore the array buffer");
            glGetIntegerv(GL_ACTIVE_TEXTURE, &value);
            check(value == GL_TEXTURE3, "Renderer did not restore the active texture unit");
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &value);
            check(value == static_cast<GLint>(probeTexture3), "Renderer did not restore the active texture binding");
            glGetIntegerv(GL_SAMPLER_BINDING, &value);
            check(value == static_cast<GLint>(sampler3), "Renderer did not disturb the non-UI sampler binding");
            glActiveTexture(GL_TEXTURE0);
            glGetIntegerv(GL_TEXTURE_BINDING_2D, &value);
            check(value == static_cast<GLint>(probeTexture0), "Renderer did not restore unit-zero texture binding");
            glGetIntegerv(GL_SAMPLER_BINDING, &value);
            check(value == static_cast<GLint>(sampler0), "Renderer did not restore unit-zero sampler binding");
            glActiveTexture(GL_TEXTURE3);
            GLint viewport[4]{};
            glGetIntegerv(GL_VIEWPORT, viewport);
            check(viewport[0] == 3 && viewport[1] == 4 && viewport[2] == 91 && viewport[3] == 73,
                  "Renderer did not restore the viewport");
            GLint scissor[4]{};
            glGetIntegerv(GL_SCISSOR_BOX, scissor);
            check(scissor[0] == 2 && scissor[1] == 3 && scissor[2] == 12 && scissor[3] == 13 &&
                  glIsEnabled(GL_SCISSOR_TEST), "Renderer did not restore scissor state");
            check(glIsEnabled(GL_DEPTH_TEST) && glIsEnabled(GL_CULL_FACE) && !glIsEnabled(GL_BLEND),
                  "Renderer did not restore depth, cull, and blend enable states");
            GLboolean depthWrite = GL_FALSE;
            glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWrite);
            check(depthWrite == GL_TRUE, "Renderer did not restore the depth write mask");
            glGetIntegerv(GL_BLEND_SRC_RGB, &value);
            check(value == GL_SRC_ALPHA, "Renderer did not restore the blend function");
            glGetIntegerv(GL_BLEND_EQUATION_RGB, &value);
            check(value == GL_FUNC_REVERSE_SUBTRACT, "Renderer did not restore the blend equation");
            glGetIntegerv(GL_BLEND_SRC_ALPHA, &value);
            check(value == GL_ONE, "Renderer did not restore the alpha blend function");
            glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &value);
            check(value == GL_FUNC_ADD, "Renderer did not restore the alpha blend equation");

            std::vector<unsigned char> image(static_cast<std::size_t>(width) * height * 4);
            glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, image.data());
            const auto red = pixel(image, width, height, 12, 12);
            const auto green = pixel(image, width, height, 52, 12);
            const auto clipped = pixel(image, width, height, 68, 12);
            const auto corner=pixel(image,width,height,104,8),roundCenter=pixel(image,width,height,116,20);
            check(corner[0]<5&&corner[1]<5&&corner[2]<5&&roundCenter[1]>220,"Rounded overflow must clip texture corners while preserving its center");
            const auto borrowed = pixel(image, width, height, 88, 16);
            check(red[0] > 240 && red[1] < 10 && red[2] < 10, "RmlUi red element did not reach the framebuffer");
            check(green[1] > 240 && green[0] < 10 && green[2] < 10, "RmlUi overflow clip did not draw its interior");
            check(clipped[0] < 5 && clipped[1] < 5 && clipped[2] < 5, "RmlUi overflow clip leaked pixels outside its scissor");
            check(std::abs(int(borrowed[0])-17)<3 && std::abs(int(borrowed[1])-231)<3 && std::abs(int(borrowed[2])-73)<3,
                  "RmlUi did not draw the borrowed native map texture");
            check(brightPixels(image, width, height, 8, 40, 120, 66) > 6,
                  "Inter glyph pixels were not rendered by the RmlUi document");
            check(brightPixels(image, width, height, 8, 66, 120, 95) > 6,
                  "Traditional Chinese fallback glyph pixels were not rendered");
            check(glGetError() == GL_NO_ERROR, "RmlRenderer smoke produced an OpenGL error");

            Rml::RemoveContext("rml-renderer-smoke");
            context = nullptr;
            check(glIsTexture(borrowedMapTexture) == GL_TRUE,
                  "RmlUi released a borrowed native map texture it does not own");
            Rml::Shutdown();
            initialized = false;

            glUseProgram(0);
            glBindVertexArray(0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glActiveTexture(GL_TEXTURE0);
            glBindSampler(0, 0);
            glActiveTexture(GL_TEXTURE3);
            glBindSampler(3, 0);
            glBindTexture(GL_TEXTURE_2D, 0);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, 0);
            glDeleteSamplers(1, &sampler0);
            glDeleteSamplers(1, &sampler3);
            glDeleteTextures(1, &probeTexture0);
            glDeleteTextures(1, &probeTexture3);
            glDeleteBuffers(1, &probeBuffer);
            glDeleteVertexArrays(1, &probeVao);
            glDeleteProgram(probeProgram);
            std::cout << "RML_RENDERER draws=inter+cjk scissor=pass state=restored renderer="
                      << reinterpret_cast<const char*>(glGetString(GL_RENDERER)) << '\n';
        } catch (const std::exception& error) {
            std::cerr << error.what() << '\n';
            result = 1;
            if (initialized) {
                Rml::RemoveContext("rml-renderer-smoke");
                Rml::Shutdown();
            }
        }
    }
    if(borrowedMapTexture){
        if(glIsTexture(borrowedMapTexture)!=GL_TRUE){std::cerr<<"RmlRenderer destructor deleted the borrowed map texture\n";result=1;}
        glDeleteTextures(1,&borrowedMapTexture);
    }
    glfwDestroyWindow(window);
    glfwTerminate();
    return result;
}
