#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

namespace restoration {
enum class WorldFxShape : int { Drop, Puff, Glow, Ring, Shell, Beam, Swirl, Bubble, Orb, Stamp, Speaker, Missile, Cloud, WarningRing, SourceBeam };

// CPU pool record. The first fields are the common draw inputs; the remaining
// fields preserve the reference fx.js pool attributes so source recipes and
// their simulation can be checked field-for-field.
struct WorldFxParticle {
    WorldFxShape shape = WorldFxShape::Drop;
    glm::vec3 pos{0}, vel{0}, normal{0,1,0}, color{1};
    float radius=0, length=0, age=0, life=0, alpha=1, style=0, seed=0;
    float radius1=0, gravity=1, stretch=1, flags=0;
    float drag=2.2f, buoyancy=0, kind=0, fadeIn=0.08f, fadeOut=0;
    float rotation=0, rotationVelocity=0, wobble=0, expand=0, crown=0;
    float renderRadius=0, renderLength=0, renderAlpha=1;
};

struct WorldFxHit { glm::vec3 point{0}, normal{0,1,0}; };
using WorldFxCollider = std::function<bool(glm::vec3 from, glm::vec3 to, WorldFxHit& hit)>;

// Builds the source fx.js pooled recipes using its xorshift-fixture Math.random
// stream. `rng` is stateful and must be shared across sequential calls.
std::vector<WorldFxParticle> BuildWorldFxRecipe(std::string_view name,
    glm::dvec3 pos, glm::dvec3 normal, glm::dvec3 color, double radius,
    std::uint32_t& rng, double waterY=-1.6, glm::dvec3 camera={0,10,0});

// Mirrors fx.js pool update equations and Float32Array write boundaries. The
// optional collider has the same segment contract as FX.setCollider.
void UpdateWorldFxRecipe(std::vector<WorldFxParticle>& particles, double dt,
    std::uint32_t& rng, const WorldFxCollider& collider={}, float waterY=-1.6f,
    bool onSpeck=false, bool paintEffects=true);
}
