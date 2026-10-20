#include "RmlRenderer.hpp"

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

// No other translation unit in this project supplies stb_image's implementation.
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace restoration {
namespace {

GLuint compileShader(GLenum type, const char* source) {
    const GLuint shader = glCreateShader(type);
    if (!shader) throw std::runtime_error("RmlRenderer could not create a shader");
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    GLint compiled = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_TRUE) return shader;

    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
    glGetShaderInfoLog(shader, length, nullptr, log.data());
    glDeleteShader(shader);
    throw std::runtime_error("RmlRenderer shader compilation failed: " + log);
}

GLuint createProgram() {
    static constexpr char vertexSource[] = R"GLSL(#version 450 core
layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec4 inColour;
layout(location = 2) in vec2 inTexCoord;
uniform mat4 projection;
uniform mat4 transform;
uniform vec2 translation;
out vec4 colour;
out vec2 texCoord;
void main() {
    colour = inColour;
    texCoord = inTexCoord;
    gl_Position = projection * transform * vec4(inPosition + translation, 0.0, 1.0);
})GLSL";
    static constexpr char fragmentSource[] = R"GLSL(#version 450 core
in vec4 colour;
in vec2 texCoord;
uniform sampler2D texture0;
uniform int hasTexture;
layout(location = 0) out vec4 outColour;
void main() {
    vec4 texel = hasTexture != 0 ? texture(texture0, texCoord) : vec4(1.0);
    outColour = texel * colour;
})GLSL";

    const GLuint vertex = compileShader(GL_VERTEX_SHADER, vertexSource);
    GLuint fragment = 0;
    GLuint program = 0;
    try {
        fragment = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
        program = glCreateProgram();
        if (!program) throw std::runtime_error("RmlRenderer could not create a program");
        glAttachShader(program, vertex);
        glAttachShader(program, fragment);
        glLinkProgram(program);
        GLint linked = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if (linked != GL_TRUE) {
            GLint length = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
            std::string log(static_cast<std::size_t>(std::max(length, 1)), '\0');
            glGetProgramInfoLog(program, length, nullptr, log.data());
            throw std::runtime_error("RmlRenderer program link failed: " + log);
        }
    } catch (...) {
        if (program) glDeleteProgram(program);
        if (fragment) glDeleteShader(fragment);
        glDeleteShader(vertex);
        throw;
    }
    glDetachShader(program, vertex);
    glDetachShader(program, fragment);
    glDeleteShader(vertex);
    glDeleteShader(fragment);
    return program;
}

bool fitsBuffer(std::size_t count, std::size_t elementSize) {
    return elementSize != 0 && count <= static_cast<std::size_t>(std::numeric_limits<GLsizeiptr>::max()) / elementSize;
}

} // namespace

struct RmlRenderer::Impl {
    struct Geometry {
        GLuint vao = 0;
        GLuint vertexBuffer = 0;
        GLuint indexBuffer = 0;
        GLsizei indexCount = 0;
    };

    struct SavedState {
        GLint program = 0;
        GLint vao = 0;
        GLint arrayBuffer = 0;
        GLint activeTexture = GL_TEXTURE0;
        GLint texture0 = 0;
        GLint sampler0 = 0;
        GLint viewport[4]{};
        GLint scissor[4]{};
        GLint blendSourceRgb = GL_ONE;
        GLint blendDestinationRgb = GL_ZERO;
        GLint blendSourceAlpha = GL_ONE;
        GLint blendDestinationAlpha = GL_ZERO;
        GLint blendEquationRgb = GL_FUNC_ADD;
        GLint blendEquationAlpha = GL_FUNC_ADD;
        GLboolean blend = GL_FALSE;
        GLboolean depth = GL_FALSE;
        GLboolean depthWrite = GL_TRUE;
        GLboolean cull = GL_FALSE;
        GLboolean scissorEnabled = GL_FALSE;
    };

    GLuint program = 0;
    GLint projectionLocation = -1;
    GLint transformLocation = -1;
    GLint translationLocation = -1;
    GLint samplerLocation = -1;
    GLint hasTextureLocation = -1;
    Rml::Matrix4f transform;
    std::unordered_map<Rml::CompiledGeometryHandle, Geometry> geometries;
    std::unordered_map<Rml::TextureHandle, GLuint> textures;
    Rml::CompiledGeometryHandle nextGeometry = 1;
    Rml::TextureHandle nextTexture = 1;
    SavedState saved;
    Rml::Rectanglei scissorRegion;
    int width = 0;
    int height = 0;
    bool active = false;
    bool scissorEnabled = false;

    Impl() {
        program = createProgram();
        projectionLocation = glGetUniformLocation(program, "projection");
        transformLocation = glGetUniformLocation(program, "transform");
        translationLocation = glGetUniformLocation(program, "translation");
        samplerLocation = glGetUniformLocation(program, "texture0");
        hasTextureLocation = glGetUniformLocation(program, "hasTexture");
    }

    ~Impl() {
        for (const auto& [handle, geometry] : geometries) {
            (void)handle;
            if (geometry.indexBuffer) glDeleteBuffers(1, &geometry.indexBuffer);
            if (geometry.vertexBuffer) glDeleteBuffers(1, &geometry.vertexBuffer);
            if (geometry.vao) glDeleteVertexArrays(1, &geometry.vao);
        }
        for (const auto& [handle, texture] : textures) {
            (void)handle;
            if (texture) glDeleteTextures(1, &texture);
        }
        if (program) glDeleteProgram(program);
    }

    Rml::TextureHandle uploadTexture(int textureWidth, int textureHeight,
                                     const Rml::byte* pixels, bool alreadyPremultiplied) {
        if (!pixels || textureWidth <= 0 || textureHeight <= 0) return 0;
        const std::size_t w = static_cast<std::size_t>(textureWidth);
        const std::size_t h = static_cast<std::size_t>(textureHeight);
        if (w > std::numeric_limits<std::size_t>::max() / h || w * h > std::numeric_limits<std::size_t>::max() / 4)
            return 0;
        const std::size_t byteCount = w * h * 4;

        std::vector<Rml::byte> premultiplied;
        if (!alreadyPremultiplied) {
            premultiplied.assign(pixels, pixels + byteCount);
            for (std::size_t i = 0; i < byteCount; i += 4) {
                const unsigned alpha = premultiplied[i + 3];
                premultiplied[i] = static_cast<Rml::byte>((premultiplied[i] * alpha + 127) / 255);
                premultiplied[i + 1] = static_cast<Rml::byte>((premultiplied[i + 1] * alpha + 127) / 255);
                premultiplied[i + 2] = static_cast<Rml::byte>((premultiplied[i + 2] * alpha + 127) / 255);
            }
            pixels = premultiplied.data();
        }

        GLint activeTexture = GL_TEXTURE0;
        GLint texture0 = 0;
        GLint unpackAlignment = 4;
        GLint unpackRowLength = 0;
        GLint unpackSkipPixels = 0;
        GLint unpackSkipRows = 0;
        GLint unpackBuffer = 0;
        glGetIntegerv(GL_ACTIVE_TEXTURE, &activeTexture);
        glActiveTexture(GL_TEXTURE0);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture0);
        glGetIntegerv(GL_UNPACK_ALIGNMENT, &unpackAlignment);
        glGetIntegerv(GL_UNPACK_ROW_LENGTH, &unpackRowLength);
        glGetIntegerv(GL_UNPACK_SKIP_PIXELS, &unpackSkipPixels);
        glGetIntegerv(GL_UNPACK_SKIP_ROWS, &unpackSkipRows);
        glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING, &unpackBuffer);

        GLuint name = 0;
        glGenTextures(1, &name);
        if (name) {
            glBindTexture(GL_TEXTURE_2D, name);
            glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
            glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
            glPixelStorei(GL_UNPACK_SKIP_ROWS, 0);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, textureWidth, textureHeight, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            GLint allocatedWidth = 0;
            GLint allocatedHeight = 0;
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &allocatedWidth);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &allocatedHeight);
            if (allocatedWidth != textureWidth || allocatedHeight != textureHeight) {
                glDeleteTextures(1, &name);
                name = 0;
            }
        }

        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, static_cast<GLuint>(unpackBuffer));
        glPixelStorei(GL_UNPACK_ALIGNMENT, unpackAlignment);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, unpackRowLength);
        glPixelStorei(GL_UNPACK_SKIP_PIXELS, unpackSkipPixels);
        glPixelStorei(GL_UNPACK_SKIP_ROWS, unpackSkipRows);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(texture0));
        glActiveTexture(static_cast<GLenum>(activeTexture));
        if (!name) return 0;

        const Rml::TextureHandle handle = allocateTextureHandle();
        try {
            textures.emplace(handle, name);
        } catch (...) {
            glDeleteTextures(1, &name);
            throw;
        }
        return handle;
    }

    Rml::TextureHandle allocateTextureHandle() {
        while (nextTexture == 0 || textures.find(nextTexture) != textures.end()) ++nextTexture;
        return nextTexture++;
    }

    void setScissor() const {
        if (!active || !scissorEnabled) return;
        const int x0 = std::max(scissorRegion.Left(), 0);
        const int x1 = std::min(scissorRegion.Right(), width);
        const int y0 = std::max(height - scissorRegion.Bottom(), 0);
        const int y1 = std::min(height - scissorRegion.Top(), height);
        glScissor(x0, y0, std::max(x1 - x0, 0), std::max(y1 - y0, 0));
    }

    void restore() {
        if (!active) return;
        glUseProgram(static_cast<GLuint>(saved.program));
        glBindVertexArray(static_cast<GLuint>(saved.vao));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(saved.arrayBuffer));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(saved.texture0));
        glBindSampler(0, static_cast<GLuint>(saved.sampler0));
        glActiveTexture(static_cast<GLenum>(saved.activeTexture));
        glViewport(saved.viewport[0], saved.viewport[1], saved.viewport[2], saved.viewport[3]);
        glScissor(saved.scissor[0], saved.scissor[1], saved.scissor[2], saved.scissor[3]);
        glBlendFuncSeparate(static_cast<GLenum>(saved.blendSourceRgb),
                            static_cast<GLenum>(saved.blendDestinationRgb),
                            static_cast<GLenum>(saved.blendSourceAlpha),
                            static_cast<GLenum>(saved.blendDestinationAlpha));
        glBlendEquationSeparate(static_cast<GLenum>(saved.blendEquationRgb),
                                static_cast<GLenum>(saved.blendEquationAlpha));
        glDepthMask(saved.depthWrite);
        if (saved.blend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
        if (saved.depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        if (saved.cull) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
        if (saved.scissorEnabled) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
        active = false;
    }
};

RmlRenderer::RmlRenderer() : impl_(std::make_unique<Impl>()) {}
RmlRenderer::~RmlRenderer() = default;

bool RmlRenderer::Begin(int width, int height) {
    if (!impl_ || impl_->active || width <= 0 || height <= 0) return false;
    auto& state = impl_->saved;
    glGetIntegerv(GL_CURRENT_PROGRAM, &state.program);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &state.vao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &state.arrayBuffer);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &state.activeTexture);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &state.texture0);
    glGetIntegerv(GL_SAMPLER_BINDING, &state.sampler0);
    glActiveTexture(static_cast<GLenum>(state.activeTexture));
    glGetIntegerv(GL_VIEWPORT, state.viewport);
    glGetIntegerv(GL_SCISSOR_BOX, state.scissor);
    glGetIntegerv(GL_BLEND_SRC_RGB, &state.blendSourceRgb);
    glGetIntegerv(GL_BLEND_DST_RGB, &state.blendDestinationRgb);
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &state.blendSourceAlpha);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &state.blendDestinationAlpha);
    glGetIntegerv(GL_BLEND_EQUATION_RGB, &state.blendEquationRgb);
    glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &state.blendEquationAlpha);
    state.blend = glIsEnabled(GL_BLEND);
    state.depth = glIsEnabled(GL_DEPTH_TEST);
    state.depthWrite = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &state.depthWrite);
    state.cull = glIsEnabled(GL_CULL_FACE);
    state.scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);

    impl_->width = width;
    impl_->height = height;
    impl_->active = true;
    impl_->scissorEnabled = false;
    impl_->scissorRegion = Rml::Rectanglei::FromPositionSize({0, 0}, {width, height});
    const std::array<float, 16> identity{1, 0, 0, 0,
                                         0, 1, 0, 0,
                                         0, 0, 1, 0,
                                         0, 0, 0, 1};
    impl_->transform = Rml::Matrix4f::FromColumnMajor(identity.data());

    glViewport(0, 0, width, height);
    glUseProgram(impl_->program);
    const glm::mat4 projection = glm::ortho(0.0f, static_cast<float>(width),
                                           static_cast<float>(height), 0.0f, -1.0f, 1.0f);
    glUniformMatrix4fv(impl_->projectionLocation, 1, GL_FALSE, glm::value_ptr(projection));
    glUniform1i(impl_->samplerLocation, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindSampler(0, 0);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_SCISSOR_TEST);
    return true;
}

void RmlRenderer::End() {
    if (impl_) impl_->restore();
}

Rml::CompiledGeometryHandle RmlRenderer::CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                         Rml::Span<const int> indices) {
    if (vertices.empty() || indices.empty() || indices.size() > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max()) ||
        !fitsBuffer(vertices.size(), sizeof(Rml::Vertex)) || !fitsBuffer(indices.size(), sizeof(std::uint32_t)))
        return 0;
    std::vector<std::uint32_t> convertedIndices;
    try {
        convertedIndices.reserve(indices.size());
        for (const int index : indices) {
            if (index < 0 || static_cast<std::size_t>(index) >= vertices.size()) return 0;
            convertedIndices.push_back(static_cast<std::uint32_t>(index));
        }
    } catch (...) {
        return 0;
    }

    GLint previousVao = 0;
    GLint previousArrayBuffer = 0;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousVao);
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);
    Impl::Geometry geometry;
    glGenVertexArrays(1, &geometry.vao);
    glGenBuffers(1, &geometry.vertexBuffer);
    glGenBuffers(1, &geometry.indexBuffer);
    if (!geometry.vao || !geometry.vertexBuffer || !geometry.indexBuffer) {
        if (geometry.indexBuffer) glDeleteBuffers(1, &geometry.indexBuffer);
        if (geometry.vertexBuffer) glDeleteBuffers(1, &geometry.vertexBuffer);
        if (geometry.vao) glDeleteVertexArrays(1, &geometry.vao);
        glBindVertexArray(static_cast<GLuint>(previousVao));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousArrayBuffer));
        return 0;
    }

    glBindVertexArray(geometry.vao);
    glBindBuffer(GL_ARRAY_BUFFER, geometry.vertexBuffer);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(Rml::Vertex)),
                 vertices.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, geometry.indexBuffer);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(convertedIndices.size() * sizeof(std::uint32_t)),
                 convertedIndices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Rml::Vertex),
                          reinterpret_cast<const void*>(offsetof(Rml::Vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Rml::Vertex),
                          reinterpret_cast<const void*>(offsetof(Rml::Vertex, colour)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Rml::Vertex),
                          reinterpret_cast<const void*>(offsetof(Rml::Vertex, tex_coord)));
    geometry.indexCount = static_cast<GLsizei>(convertedIndices.size());
    glBindVertexArray(static_cast<GLuint>(previousVao));
    glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(previousArrayBuffer));

    while (impl_->nextGeometry == 0 || impl_->geometries.find(impl_->nextGeometry) != impl_->geometries.end())
        ++impl_->nextGeometry;
    const Rml::CompiledGeometryHandle handle = impl_->nextGeometry++;
    try {
        impl_->geometries.emplace(handle, geometry);
    } catch (...) {
        glDeleteBuffers(1, &geometry.indexBuffer);
        glDeleteBuffers(1, &geometry.vertexBuffer);
        glDeleteVertexArrays(1, &geometry.vao);
        return 0;
    }
    return handle;
}

void RmlRenderer::RenderGeometry(Rml::CompiledGeometryHandle geometryHandle, Rml::Vector2f translation,
                                 Rml::TextureHandle textureHandle) {
    if (!impl_ || !impl_->active) return;
    const auto geometry = impl_->geometries.find(geometryHandle);
    if (geometry == impl_->geometries.end()) return;
    GLuint texture = 0;
    if (textureHandle) {
        const auto foundTexture = impl_->textures.find(textureHandle);
        if (foundTexture == impl_->textures.end()) return;
        texture = foundTexture->second;
    }

    std::array<GLfloat, 16> transform{};
    for (int row = 0; row < 4; ++row)
        for (int column = 0; column < 4; ++column)
            transform[column * 4 + row] = impl_->transform.GetRow(row)[column];
    glUseProgram(impl_->program);
    glUniformMatrix4fv(impl_->transformLocation, 1, GL_FALSE, transform.data());
    glUniform2f(impl_->translationLocation, translation.x, translation.y);
    glUniform1i(impl_->hasTextureLocation, textureHandle ? 1 : 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glBindSampler(0, 0);
    glBindVertexArray(geometry->second.vao);
    glDrawElements(GL_TRIANGLES, geometry->second.indexCount, GL_UNSIGNED_INT, nullptr);
}

void RmlRenderer::ReleaseGeometry(Rml::CompiledGeometryHandle geometryHandle) {
    if (!impl_) return;
    const auto found = impl_->geometries.find(geometryHandle);
    if (found == impl_->geometries.end()) return;
    if (found->second.indexBuffer) glDeleteBuffers(1, &found->second.indexBuffer);
    if (found->second.vertexBuffer) glDeleteBuffers(1, &found->second.vertexBuffer);
    if (found->second.vao) glDeleteVertexArrays(1, &found->second.vao);
    impl_->geometries.erase(found);
}

Rml::TextureHandle RmlRenderer::LoadTexture(Rml::Vector2i& textureDimensions, const Rml::String& source) {
    textureDimensions = {0, 0};
    try {
        const auto path = std::filesystem::u8path(source.begin(), source.end());
        std::ifstream file(path, std::ios::binary);
        if (!file) return 0;
        std::vector<unsigned char> encoded((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (encoded.empty() || encoded.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return 0;
        int width = 0;
        int height = 0;
        int channels = 0;
        std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
            stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()),
                                  &width, &height, &channels, STBI_rgb_alpha),
            stbi_image_free);
        if (!pixels) return 0;
        const auto handle = impl_->uploadTexture(width, height, pixels.get(), false);
        if (!handle) return 0;
        textureDimensions = {width, height};
        return handle;
    } catch (...) {
        return 0;
    }
}

Rml::TextureHandle RmlRenderer::GenerateTexture(Rml::Span<const Rml::byte> source,
                                               Rml::Vector2i sourceDimensions) {
    if (sourceDimensions.x <= 0 || sourceDimensions.y <= 0) return 0;
    const std::size_t width = static_cast<std::size_t>(sourceDimensions.x);
    const std::size_t height = static_cast<std::size_t>(sourceDimensions.y);
    if (width > std::numeric_limits<std::size_t>::max() / height ||
        width * height > std::numeric_limits<std::size_t>::max() / 4) return 0;
    const std::size_t expectedBytes = width * height * 4;
    if (source.size() != expectedBytes) return 0;
    try {
        return impl_->uploadTexture(sourceDimensions.x, sourceDimensions.y, source.data(), true);
    } catch (...) {
        return 0;
    }
}

void RmlRenderer::ReleaseTexture(Rml::TextureHandle textureHandle) {
    if (!impl_) return;
    const auto found = impl_->textures.find(textureHandle);
    if (found == impl_->textures.end()) return;
    if (found->second) glDeleteTextures(1, &found->second);
    impl_->textures.erase(found);
}

void RmlRenderer::EnableScissorRegion(bool enable) {
    if (!impl_) return;
    impl_->scissorEnabled = enable;
    if (!impl_->active) return;
    if (enable) {
        impl_->setScissor();
        glEnable(GL_SCISSOR_TEST);
    } else {
        glDisable(GL_SCISSOR_TEST);
    }
}

void RmlRenderer::SetScissorRegion(Rml::Rectanglei region) {
    if (!impl_) return;
    impl_->scissorRegion = region;
    if (impl_->active && impl_->scissorEnabled) impl_->setScissor();
}

void RmlRenderer::SetTransform(const Rml::Matrix4f* transform) {
    if (!impl_) return;
    if (transform) {
        impl_->transform = *transform;
    } else {
        const std::array<float, 16> identity{1, 0, 0, 0,
                                             0, 1, 0, 0,
                                             0, 0, 1, 0,
                                             0, 0, 0, 1};
        impl_->transform = Rml::Matrix4f::FromColumnMajor(identity.data());
    }
}

} // namespace restoration
