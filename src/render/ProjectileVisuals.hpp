#pragma once
#include <glm/glm.hpp>
#include <vector>

namespace restoration {
struct ShooterProjectile;
struct ProjectileBlob {
    glm::mat4 matrix{1};
    glm::vec4 shape{1,0.04f,0,0};
    glm::vec3 color{1};
};
// Source Projectiles._draw output, including its Float32 instance-buffer boundary.
std::vector<ProjectileBlob> BuildProjectileBlobs(const std::vector<ShooterProjectile>& projectiles);
void BuildProjectileBlobs(const std::vector<ShooterProjectile>& projectiles, std::vector<ProjectileBlob>& output, bool includeShooter=true);
// Ink-shot aShape: smooth-union radius (zero for detached drops), launch impulse, seed phase, normalized lifetime.
void AppendShooterInkBlobs(const std::vector<ShooterProjectile>& projectiles, std::vector<ProjectileBlob>& output);
}
