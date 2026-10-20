#pragma once
#include "LensInk.hpp"
#include <filesystem>
#include <memory>
#include <glm/vec2.hpp>
namespace restoration {
class LensField {
public:
    explicit LensField(std::filesystem::path assets="assets/reference/post");
    ~LensField();
    LensField(const LensField&)=delete;
    LensField& operator=(const LensField&)=delete;
    int Render(const LensInk& ink,int framebufferWidth,int framebufferHeight,double scale=1./3);
    unsigned texture()const;
    glm::vec2 texel()const;
private:
    struct State;
    std::unique_ptr<State> state_;
};
}
