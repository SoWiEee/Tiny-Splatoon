#pragma once

#include <RmlUi/Core/RenderInterface.h>

#include <glad/glad.h>

#include <memory>

namespace restoration {

class RmlRenderer final : public Rml::RenderInterface {
public:
    RmlRenderer();
    ~RmlRenderer() override;

    RmlRenderer(const RmlRenderer&) = delete;
    RmlRenderer& operator=(const RmlRenderer&) = delete;

    bool Begin(int width, int height);
    void End();

    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                Rml::Span<const int> indices) override;
    void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation,
                        Rml::TextureHandle texture) override;
    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;

    Rml::TextureHandle LoadTexture(Rml::Vector2i& textureDimensions, const Rml::String& source) override;
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source,
                                       Rml::Vector2i sourceDimensions) override;
    void ReleaseTexture(Rml::TextureHandle texture) override;
    void SetNativeMapTexture(GLuint texture);

    void EnableScissorRegion(bool enable) override;
    void SetScissorRegion(Rml::Rectanglei region) override;
    void EnableClipMask(bool enable) override;
    void RenderToClipMask(Rml::ClipMaskOperation operation, Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation) override;
    void SetTransform(const Rml::Matrix4f* transform) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace restoration
