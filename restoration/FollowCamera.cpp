#include "FollowCamera.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <glm/geometric.hpp>

namespace restoration {
namespace {
double clamp(double x, double lo, double hi) { return std::clamp(x, lo, hi); }
double damp(double from, double to, double lambda, double dt) {
    return from + (to - from) * (1.0 - std::exp(-lambda * dt));
}
}

double FollowCamera::Spring::step(double target, double omega, double dt) {
    const double d = x - target, e = std::exp(-omega * dt), k = (v + omega * d) * dt;
    x = target + (d + k) * e;
    v = (v - omega * k) * e;
    return x;
}

void FollowCamera::reset(const CameraTarget& target) {
    const auto& p = target.visualPosition;
    const double h = target.squid ? 1.3 : 1.85;
    sx_.reset(p.x); sy_.reset(p.y + h); sz_.reset(p.z);
    height_.reset(h); boom_.reset(4.5); wantDist_ = 4.5;
    dip_.reset(0.0); side_.reset(0.0); lensLift_.reset(0.0); recoil_.reset(0.0);
    hasLastTY_ = false; vyF_ = 0.0; takeoffY_ = p.y; landSeen_ = target.landT;
    fovKick_ = 0.0; shoulder_ = 0.0;
    const glm::dvec3 forward{std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
    pose_.eye = p + glm::dvec3{0.0, h + 0.15, 0.0} - forward * 4.5;
    pose_.forward = forward;
    pose_.verticalFovDegrees = 2.0 * std::atan(std::tan(82.0 * std::acos(-1.0) / 360.0) / (16.0 / 9.0)) * 180.0 / std::acos(-1.0);
}

void FollowCamera::recoil(double amount) {
    recoil_.v = std::min(recoil_.v + amount * 55.0, 3.0);
}

void FollowCamera::tick(double dt, const CameraTarget& target, const StageGeometry& geometry) {
    const auto& p = target.visualPosition;
    const bool swim = target.swimming || target.climbing;
    const double h = height_.step(swim ? 1.15 : target.squid ? 1.3 : 1.85, 11.0, dt);
    const glm::dvec3 forward{std::sin(yaw) * std::cos(pitch), std::sin(pitch), std::cos(yaw) * std::cos(pitch)};
    const glm::dvec3 right{-std::cos(yaw), 0.0, std::sin(yaw)};
    const double lat = target.velocity.x * right.x + target.velocity.z * right.z;
    const double side = side_.step(clamp(lat * 0.045, -0.32, 0.32), 3.2, dt);
    const double lead = 0.72 * 2.0 / 30.0;
    const double tx = p.x + target.velocity.x * lead + right.x * side;
    const double tz = p.z + target.velocity.z * lead + right.z * side;
    const double ty = p.y + h;
    if (std::abs(sx_.x - p.x) + std::abs(sz_.x - p.z) > 6.0 || std::abs(sy_.x - ty) > 6.0) {
        sx_.reset(p.x); sy_.reset(ty); sz_.reset(p.z); hasLastTY_ = false; vyF_ = 0.0;
    }
    sx_.step(tx, 30.0, dt); sz_.step(tz, 30.0, dt);
    const bool fresh = !hasLastTY_ || dt <= 0.0;
    const double vyRaw = fresh ? 0.0 : clamp((ty - lastTY_) / dt, -30.0, 30.0);
    lastTY_ = ty; hasLastTY_ = true;
    vyF_ = fresh ? 0.0 : damp(vyF_, vyRaw, 30.0, dt);
    double omegaV = 13.0, leadV = 0.5;
    if (target.grounded || target.climbing) takeoffY_ = p.y;
    else if (p.y < takeoffY_ - 0.25) { omegaV = 15.0; leadV = 0.8; }
    else { omegaV = 7.5; leadV = 0.25; }
    sy_.step(ty + vyF_ * leadV * 2.0 / omegaV, omegaV, dt);
    if (target.landT < landSeen_ - 1e-6 && target.landSpeed > 4.0)
        dip_.v -= clamp((target.landSpeed - 4.0) * 0.045, 0.0, 0.45);
    landSeen_ = target.landT;
    const int steps = std::max(1, static_cast<int>(std::ceil(13.0 * dt / 0.12)));
    const double step = dt / steps;
    for (int i = 0; i < steps; ++i) {
        dip_.v += (-169.0 * dip_.x - 2.0 * 0.82 * 13.0 * dip_.v) * step;
        dip_.x += dip_.v * step;
    }
    const glm::dvec3 pivot{sx_.x, sy_.x + clamp(dip_.x, -0.3, 0.12), sz_.x};
    const double hs = std::hypot(target.velocity.x, target.velocity.z);
    double fk = 0.0;
    if (swim) fk = clamp((hs - 6.0) * 1.1, 0.0, 7.0);
    else if (!target.grounded && target.velocity.y > 2.0) fk = 1.2;
    fovKick_ = damp(fovKick_, fk, 5.0, dt);
    double want = target.squid ? 4.1 : 4.5;
    if (swim) want += clamp((hs - 6.0) / 6.0, 0.0, 1.0) * 0.35;
    wantDist_ = damp(wantDist_, want, 6.0, dt);
    const CameraProbe probe = geometry.cameraProbe(pivot, -forward, wantDist_, 0.62);
    const double deep = clamp(boom_.x - probe.hard, 0.0, 1.0);
    boom_.step(probe.soft, probe.soft < boom_.x ? 22.0 + 26.0 * deep : 3.6, dt);
    if (boom_.x > probe.hard + 1.6) { boom_.x = probe.hard + 1.6; boom_.v = std::min(boom_.v, 0.0); }
    if (probe.floor && boom_.x > probe.hard) { boom_.x = probe.hard; boom_.v = std::min(boom_.v, 0.0); }
    if (boom_.x < 0.45) { boom_.x = 0.45; boom_.v = std::max(boom_.v, 0.0); }
    glm::dvec3 eye = pivot - forward * boom_.x;
    eye.y += 0.15;
    const double gy = geometry.groundHeight(eye.x, eye.z, eye.y + 0.2);
    const double liftT = std::isfinite(gy) && gy < pivot.y - 0.6 ? std::max(0.0, gy + 0.24 - eye.y) : 0.0;
    lensLift_.step(liftT, liftT > lensLift_.x ? 34.0 : 7.0, dt);
    eye.y += lensLift_.x;
    const double closeK = clamp((2.8 - boom_.x) / 1.8, 0.0, 1.0);
    double shoulderT = 0.55 * closeK * closeK * (3.0 - 2.0 * closeK);
    if (shoulderT > 0.01) {
        const RayHit hit = geometry.raycast(eye, right, shoulderT + 0.25, true);
        if (hit.hit) shoulderT = std::max(0.0, hit.dist - 0.25);
    }
    shoulder_ = damp(shoulder_, shoulderT, 8.0, dt);
    if (shoulder_ > 1e-3) eye += right * shoulder_;
    const double kick = clamp(recoil_.step(0.0, 22.0, dt), -0.01, 0.035);
    const glm::dvec3 look = pivot + forward * 10.0 + glm::dvec3{0.0, 0.15, 0.0} + (shoulder_ > 1e-3 ? right * shoulder_ : glm::dvec3{0.0});
    const glm::dvec3 aim = glm::normalize(look - eye);
    const glm::dvec3 up = glm::normalize(glm::dvec3{0.0, 1.0, 0.0} - aim * aim.y);
    pose_.eye = eye;
    pose_.forward = aim * std::cos(kick) + up * std::sin(kick);
    const double fov = 2.0 * std::atan(std::tan(82.0 * std::acos(-1.0) / 360.0) / (16.0 / 9.0)) * 180.0 / std::acos(-1.0) + fovKick_ * 0.8;
    if (std::abs(pose_.verticalFovDegrees - fov) > 0.01) pose_.verticalFovDegrees = fov;
}

} // namespace restoration
