#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace restoration {

enum class CharacterTier { Hero, Game, Far };
enum class CharacterQuality { Low, Medium, High, Ultra };

struct CharacterLodFrame {
    CharacterTier tier = CharacterTier::Game;
    CharacterTier to = CharacterTier::Game;
    double fade = 0.0;
    bool transitioning = false;
};

class CharacterLod {
public:
    CharacterLodFrame update(double screenPixels, bool local, double dt,
                             CharacterQuality quality = CharacterQuality::High);
    static double screenPixels(const glm::dmat4& root, glm::dvec3 camera,
                               double projectionY, int viewportHeight);
private:
    int tier_ = -1, to_ = -1;
    double fade_ = 0.0;
};

const char* CharacterTierName(CharacterTier tier);

} // namespace restoration
