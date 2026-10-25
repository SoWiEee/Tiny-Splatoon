#pragma once
#include "InkStream.hpp"
#include <filesystem>
#include <memory>

namespace restoration {
struct CharacterLightState;
enum class InkStreamMode { Tube, Sdf, ScreenSpace };
class InkStreamRenderer {
public:
    explicit InkStreamRenderer(const std::filesystem::path& assets="assets/shaders/ink-stream");
    ~InkStreamRenderer();
    void Update(const std::vector<InkStreamPiece>& pieces,const std::vector<InkStreamDrop>& drops);
    int Draw(const glm::dmat4& view,const glm::dmat4& projection,glm::dvec3 eye,const CharacterLightState& light,
             InkStreamMode mode=InkStreamMode::Tube,bool shadow=false);
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
