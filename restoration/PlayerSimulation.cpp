#include "PlayerSimulation.hpp"
#include "FacePaint.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <limits>
#include <glm/geometric.hpp>

namespace restoration {
namespace {
constexpr double pi = 3.14159265358979323846;
double smooth(double a, double b, double x) {
    const double t = std::clamp((x - a) / (b - a), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
double horizontalLength(glm::dvec3 v) { return std::hypot(v.x, v.z); }
double angleDifference(double a, double b) { return std::remainder(b - a, 2.0 * pi); }
}

PlayerSimulation::PlayerSimulation(const StageData& stage, const StageGeometry& geometry, FacePaint& paint, int team)
    : stage_(stage), geometry_(geometry), paint_(paint), team_(team) {
    if (team < 0 || team > 1) throw std::invalid_argument("player team must be 0 or 1");
    spawnAt(stage.spawnPads[team], team == 0 ? 0.0 : pi);
}

void PlayerSimulation::spawnAt(glm::dvec3 pos, double yaw) {
    state = {};
    state.pos = pos;
    state.yaw = yaw;
    state.invuln = 1.6;
    input = {};
    input.aimYaw = yaw;
    lastDamage_ = lastFire_ = kidT_ = 99.0;
    damageFromInk_ = coyote_ = jumpBuffer_ = fireBuffer_ = cooldown_ = bloom_ = hardLand_ = climbSpeed_ = climbExit_ = 0.0;
    firePressT_ = squidPressT_ = -1.0;
    smoothYVelocity_ = 0.0;
    yawVelocity_ = 0.0; hasFaceTarget_ = false;
    firingT_=fireFacing_=roofT_=roofStall_=0.0;roofDirection_=glm::dvec3(0);
    prevJump_ = prevFire_ = prevSquid_ = false;
    projectiles_.clear(); projectileTrail_.clear(); projectileSeed_.clear();
    ground_ = geometry_.groundProbe(pos.x, pos.y, pos.z, 0.3, 0.3, 0.24);
    if (ground_.hit && std::abs(ground_.y - pos.y) < 0.3) {
        state.pos.y = ground_.y;
        state.grounded = true;
    }
    surface();
}

bool PlayerSimulation::damage(double amount) {
    if (!state.alive || amount <= 0.0 || state.invuln > 0.0) return false;
    state.hp -= amount;
    lastDamage_ = 0.0;
    if (state.hp <= 0.0) { splat(); return true; }
    return false;
}

void PlayerSimulation::splat() {
    state.alive = false;
    state.hp = 0.0;
    state.respawnTimer = 5.5;
    state.specialCharge *= 0.5;
    state.climbing = false;
    projectiles_.clear();projectileTrail_.clear();projectileSeed_.clear();
}

glm::dvec3 PlayerSimulation::visualPos() const {
    return state.pos + glm::dvec3(0.0, state.smoothY, 0.0);
}

double PlayerSimulation::random() {
    seed_ ^= seed_ << 13; seed_ ^= seed_ >> 17; seed_ ^= seed_ << 5;
    return static_cast<double>(seed_ & 0xffffffu) / 16777216.0;
}

glm::dvec3 PlayerSimulation::aimDir() const {
    const double c = std::cos(input.aimPitch);
    return {std::sin(input.aimYaw) * c, std::sin(input.aimPitch), std::cos(input.aimYaw) * c};
}

void PlayerSimulation::surface() {
    state.groundTeam = 0;
    if (state.grounded && ground_.hit && ground_.face >= 0) {
        const int sample = paint_.sample(ground_.face, ground_.u, ground_.v);
        if (sample) state.groundTeam = sample - 1 == team_ ? 1 : 2;
    }
}

void PlayerSimulation::horizontal(double dt, bool enemy) {
    glm::dvec3 move(input.move.x, 0.0, input.move.z);
    const double mh = horizontalLength(move), mag = std::min(1.0, mh);
    const double sp = horizontalLength(state.vel);
    if (!state.grounded) {
        const double target = state.squid ? std::max(2.9, sp) : std::max(firingT_>0.0?4.6:6.0, 4.6);
        const glm::dvec3 desired = mh > 0.01 ? move / mh * target * mag : glm::dvec3(0.0);
        const glm::dvec3 delta(desired.x - state.vel.x, 0.0, desired.z - state.vel.z);
        const double length = horizontalLength(delta);
        const double rate = (mh > 0.01 ? (state.squid ? 14.0 : 20.0) : (state.squid ? 3.0 : 4.0)) * dt;
        if (length <= rate) { state.vel.x = desired.x; state.vel.z = desired.z; }
        else { state.vel.x += delta.x / length * rate; state.vel.z += delta.z / length * rate; }
        return;
    }
    double vt = state.squid ? (state.submerged ? 11.8 : 2.9) : (firingT_>0.0 ? 4.6 : 6.0);
    double accel = state.squid ? (state.submerged ? 64.0 : 34.0) : 70.0;
    double aIn = state.squid ? (state.submerged ? 0.75 : 0.6) : 0.5;
    double inKnee = state.squid ? (state.submerged ? 3.0 : 1.0) : 1.6;
    double outKnee = state.squid ? (state.submerged ? 0.22 : 0.3) : 0.28;
    double decel = state.squid ? (state.submerged ? 42.0 : 26.0) : 58.0;
    double dMin = state.squid ? 0.5 : 0.4;
    double dKnee = state.squid ? (state.submerged ? 4.0 : 2.0) : 2.2;
    double turn = state.squid ? (state.submerged ? 11.0 : 13.0) : 15.0;
    if (!state.squid && hardLand_ > 0) vt *= 1.0 - 0.28 * hardLand_;
    if (enemy) { vt = std::min(vt, 1.9); accel = std::min(accel, 30.0); decel = 30.0; }
    if (mh < 0.01) {
        if (sp < 1e-4) { state.vel.x = state.vel.z = 0.0; return; }
        const double d = decel * (dMin + (1.0 - dMin) * smooth(0.0, dKnee, sp)) * dt;
        const double k = std::max(0.0, sp - d) / sp;
        state.vel.x *= k; state.vel.z *= k;
        return;
    }
    const double tx = move.x / mh, tz = move.z / mh, vts = vt * mag;
    double dx = sp > 0.05 ? state.vel.x / sp : tx;
    double dz = sp > 0.05 ? state.vel.z / sp : tz;
    const double angle = std::acos(std::clamp(dx * tx + dz * tz, -1.0, 1.0));
    if (sp > 0.5 && angle > 2.2) {
        const double ex = tx * vts - state.vel.x, ez = tz * vts - state.vel.z;
        const double length = std::hypot(ex, ez), rate = std::max(78.0, decel) * dt * (enemy ? 0.5 : 1.0);
        if (length <= rate) { state.vel.x = tx * vts; state.vel.z = tz * vts; }
        else { state.vel.x += ex / length * rate; state.vel.z += ez / length * rate; }
        return;
    }
    const double rotation = std::min(angle, turn * (1.0 + 1.5 * (1.0 - smooth(0.0, vt, sp))) * dt);
    if (rotation > 1e-6) {
        const double signedRotation = rotation * (dz * tx - dx * tz >= 0.0 ? 1.0 : -1.0);
        const double c = std::cos(signedRotation), s = std::sin(signedRotation);
        const double nx = dx * c + dz * s, nz = -dx * s + dz * c;
        dx = nx; dz = nz;
    }
    double ns;
    if (sp < vts) {
        const double a = accel * (aIn + (1.0 - aIn) * smooth(0.0, inKnee, sp))
            * std::clamp((vts - sp) / (outKnee * vt), 0.22, 1.0);
        ns = std::min(vts, sp + a * dt);
    } else {
        ns = std::max(vts, sp - decel * (dMin + (1.0 - dMin) * smooth(0.0, dKnee, sp - vts)) * dt);
    }
    state.vel.x = dx * ns; state.vel.z = dz * ns;
}

void PlayerSimulation::climb(double dt) {
    climbExit_ = std::max(0.0, climbExit_ - dt);
    if (!state.squid || climbExit_ > 0.0) { state.climbing = false; climbSpeed_=0.0; return; }
    glm::dvec3 move(input.move.x, 0.0, input.move.z);
    const double mh = horizontalLength(move);
    if (!state.climbing && mh < 0.2) return;
    glm::dvec3 dir = state.climbing ? -wallNormal_ : move / mh;
    dir.y = 0.0;
    const double len = horizontalLength(dir);
    if (len < 1e-6) return;
    dir /= len;
    const RayHit hit = geometry_.raycast(state.pos + glm::dvec3(0.0, 0.3, 0.0), dir, 0.73);
    const bool wall = hit.hit && std::abs(hit.normal.y) < 0.5;
    const bool inked = wall && hit.face >= 0 && paint_.sample(hit.face, hit.u, hit.v) - 1 == team_;
    const double into = wall && mh > 0.01 ? -(move.x * hit.normal.x + move.z * hit.normal.z) / mh : 0.0;
    if (!state.climbing) {
        if (!inked || into <= 0.5) return;
        wallNormal_ = hit.normal;
        climbSpeed_ = std::max(0.0, state.vel.y);
        state.climbing = true;
    }
    if (!hit.hit) {
        const auto topHit=geometry_.raycast(state.pos+dir*0.70+glm::dvec3(0,1.4,0),{0,-1,0},2.0,true);
        const double top=topHit.hit && topHit.normal.y>0.68?topHit.point.y:state.pos.y+0.3;
        const double rise=std::max(0.25,top+0.42-state.pos.y);
        state.vel.y=std::max(std::sqrt(2.0*25.0*0.82*rise),std::min(climbSpeed_,6.5));
        state.vel.x=dir.x*2.5;state.vel.z=dir.z*2.5;
        state.climbing=false;climbSpeed_=0.0;climbExit_=0.3;state.grounded=false;
        return;
    }
    if (!inked) {
        state.climbing = false;climbSpeed_=0.0;climbExit_ = 0.2;
        state.vel.y=std::min(state.vel.y,1.5);
        state.vel.x+=hit.normal.x*1.2;state.vel.z+=hit.normal.z*1.2;
        return;
    }
    if (into < -0.45) {
        state.climbing = false;climbSpeed_=0.0;climbExit_ = 0.3;
        state.vel = hit.normal * 3.2 + glm::dvec3(0.0, 3.2, 0.0);
        return;
    }
    wallNormal_ = hit.normal;
    const RayHit upper = geometry_.raycast(state.pos + glm::dvec3(0.0, 0.85, 0.0), dir, 0.83);
    const bool capped = upper.hit && std::abs(upper.normal.y) < 0.5
        && !(upper.face >= 0 && paint_.sample(upper.face, upper.u, upper.v) - 1 == team_);
    double want = capped ? 0.0 : 7.5 * std::clamp(into, 0.0, 1.0) * std::min(1.0, mh);
    if(!upper.hit && want>0.0) want=std::min(want,std::sqrt(2.0*25.0*0.82*(0.42+0.3)));
    const double rate = 46.0 * dt * (want > climbSpeed_ ? 1.0 : 1.6);
    climbSpeed_ = want > climbSpeed_ ? std::min(want, climbSpeed_ + rate) : std::max(want, climbSpeed_ - rate);
    state.vel.y = climbSpeed_;
    move -= glm::dot(move, hit.normal) * hit.normal;
    state.vel.x = move.x * 5.2 - hit.normal.x * 1.2;
    state.vel.z = move.z * 5.2 - hit.normal.z * 1.2;
}

void PlayerSimulation::roofSlide(const BlockData& b,double dt) {
    if(roofT_==0.0) {
        roofStall_=0;roofPrevious_=state.pos;
        const auto& n=b.axes[1];
        if(n.y<0.995) roofDirection_=glm::normalize(glm::dvec3(n.x,0,n.z));
        else {
            const auto delta=state.pos-b.center;
            const double x=delta.x*b.axes[0].x+delta.z*b.axes[0].z;
            const double z=delta.x*b.axes[2].x+delta.z*b.axes[2].z;
            const bool across=b.half.x-std::abs(x)<b.half.z-std::abs(z);
            const auto axis=b.axes[across?0:2];const double side=(across?x:z)<0?-1.0:1.0;
            roofDirection_={axis.x*side,0,axis.z*side};
        }
    }
    roofT_+=dt;
    const auto delta=state.pos-roofPrevious_;
    const double moved=delta.x*roofDirection_.x+delta.z*roofDirection_.z;
    roofPrevious_=state.pos;
    roofStall_=roofT_>0.1 && moved<0.3*dt?roofStall_+dt:0.0;
    if(roofStall_>0.25) {roofDirection_={-roofDirection_.z,0,roofDirection_.x};roofStall_=0;}
    const double want=std::min(9.0,3.0+14.0*roofT_);
    const double along=state.vel.x*roofDirection_.x+state.vel.z*roofDirection_.z;
    if(along<want) state.vel+=roofDirection_*(want-along);
}

void PlayerSimulation::railFeet(double lo,double hi) {
    double best=ground_.hit?ground_.y+1e-3:-std::numeric_limits<double>::infinity();
    for(int id=0;id<static_cast<int>(stage_.blocks.size());++id) {
        const auto& b=stage_.blocks[id];
        if(!b.rail || !b.solid || b.axes[1].y<0.999) continue;
        const double y=b.center.y+b.half.y;
        if(y<lo || y>hi || y<=best) continue;
        const auto delta=state.pos-b.center;
        const double ex=std::max(0.0,std::abs(delta.x*b.axes[0].x+delta.z*b.axes[0].z)-b.half.x);
        const double ez=std::max(0.0,std::abs(delta.x*b.axes[2].x+delta.z*b.axes[2].z)-b.half.z);
        if(ex*ex+ez*ez>0.24*0.24) continue;
        best=y;ground_.hit=true;ground_.y=y;ground_.normal={0,1,0};ground_.block=id;
        ground_.face=-1;ground_.u=ground_.v=0;ground_.center=false;ground_.grate=true;
    }
}

void PlayerSimulation::integrate(double dt, bool jumped) {
    const bool stick = state.grounded && !jumped;
    const double prevY = state.pos.y;
    if (state.climbing) {
        state.pos += state.vel * dt;
        const Contacts contacts = geometry_.collideBody(state.pos, 0.38, 0.16, 0.55, false, true);
        if (contacts.ceiling && state.vel.y > 0.0) state.vel.y = 0.0;
        state.grounded = false;
        return;
    }
    const BlockData* support=stick && ground_.hit && ground_.block>=0?&stage_.blocks[ground_.block]:nullptr;
    if(support && support->roof) roofSlide(*support,dt);
    else roofT_=0.0;
    if(support && support->rail && !state.squid) {
        const auto& b=*support;const bool alongX=b.half.x>=b.half.z;
        const auto& axis=b.axes[alongX?2:0];const double half=alongX?b.half.z:b.half.x;
        const auto delta=state.pos-b.center;
        const double off=delta.x*axis.x+delta.z*axis.z,out=std::abs(off)-std::max(0.0,half-0.02),sign=off<0?-1.0:1.0;
        if(out>0 && (input.move.x*axis.x+input.move.z*axis.z)*sign<=0.25) {
            const double shift=std::min(out,1.1*dt)*sign;state.pos.x-=axis.x*shift;state.pos.z-=axis.z*shift;
        }
    }
    if (stick) {
        const auto& n = ground_.normal;
        state.vel.y = -(state.vel.x * n.x + state.vel.z * n.z) / std::max(0.35, n.y);
    } else {
        double g = 25.0 * (state.vel.y < 0.0 ? 1.2 : 1.0);
        if (std::abs(state.vel.y) < 1.6) g *= 0.82;
        state.vel.y = std::max(-40.0, state.vel.y - g * dt);
    }
    state.pos += state.vel * dt;
    const bool squid = state.squid;
    const Contacts contacts = geometry_.collideBody(state.pos, 0.38, squid ? 0.16 : 0.35,
                                                    squid ? 0.55 : 1.45, stick, squid);
    if (contacts.ceiling && state.vel.y > 0.0) state.vel.y = 0.0;
    if (contacts.wall) {
        const double vn = state.vel.x * contacts.wallNormal.x + state.vel.z * contacts.wallNormal.z;
        if (vn < 0.0) { state.vel.x -= contacts.wallNormal.x * vn; state.vel.z -= contacts.wallNormal.z * vn; }
    }
    const bool wasGrounded = state.grounded;
    bool grounded = false;
    if (stick) {
        ground_ = geometry_.groundProbe(state.pos.x, state.pos.y, state.pos.z,
                                        squid ? 0.24 : 0.35, 0.45, 0.24, squid);
        if(!squid) railFeet(state.pos.y-0.45,state.pos.y+0.35);
        if (ground_.hit) {
            const double dy = ground_.y - state.pos.y;
            state.pos.y = ground_.y; grounded = true;
            if (std::abs(dy) > 0.06) state.smoothY -= dy;
        }
    } else if (state.vel.y <= 0.5) {
        const double top = std::max(prevY, state.pos.y);
        ground_ = geometry_.groundProbe(state.pos.x, state.pos.y, state.pos.z,
                                        top - state.pos.y + (squid ? 0.24 : 0.35), 0.02, 0.24, squid);
        if(!squid) railFeet(state.pos.y-0.02,top+0.35);
        if (ground_.hit && ground_.y >= state.pos.y - 0.02
            && (state.vel.y <= 0.0 || ground_.y - state.pos.y < 0.02)) {
            const double pop = ground_.y - prevY;
            state.pos.y = ground_.y; grounded = true;
            if (pop > 0.035) state.smoothY -= pop;
        }
    }
    if (grounded) {
        if (!wasGrounded) {
            state.landSpeed = std::max(0.0, -state.vel.y);
            state.landT = 0.0;
            if (state.landSpeed > 11.5)
                hardLand_ = std::clamp((state.landSpeed - 11.5) / 6.0 + 0.5, 0.0, 1.0);
        }
        state.vel.y = 0.0;
    }
    state.grounded = grounded;
}

void PlayerSimulation::shoot(double dt, bool fire) {
    cooldown_ -= dt;
    firingT_=std::max(0.0,firingT_-dt);
    if (!fire) {
        cooldown_ = std::max(0.0, cooldown_);
        bloom_ = std::max(0.0, bloom_ - dt / 0.28);
        return;
    }
    firingT_=0.35;fireFacing_=0.5;
    int guard = 0;
    while (cooldown_ <= 0.0 && guard++ < 3) {
        if (state.ink < 0.95) { cooldown_ += 0.1; break; }
        state.ink -= 0.95;
        lastFire_ = 0.0;
        glm::dvec3 dir = aimDir();
        // shortcut: use the collision body's muzzle until the reference character's animated weapon rig is ported.
        const glm::dvec3 muzzle = state.pos + glm::dvec3(0.0, 1.05, 0.0) + dir * 0.3;
        if (input.hasAimPoint) {
            const glm::dvec3 to = input.aimPoint - muzzle;
            const double len = glm::length(to);
            if (len >= 2.0 && glm::dot(to, dir) >= 0.0) dir = to / len;
            const double hd = std::hypot(to.x, to.z);
            if (hd >= 1.5 && hd <= 12.5) {
                // Source ballistic correction uses its 60 Hz audit integrator.
                auto endpointY = [&](double pitch) {
                    double vh = std::cos(pitch) * 34.0, vy = std::sin(pitch) * 34.0, x = 0.0, y = 0.0;
                    double age=0;
                    for (int i = 1; i <= 90; ++i) {
                        const double px = x, py = y;
                        age+=1.0/60;
                        if (age > 0.13) { vy -= 28.0 / 60.0; vh *= 1.0 - 0.8 / 60.0; vy *= 1.0 - 0.8 / 60.0; }
                        x += vh / 60.0; y += vy / 60.0;
                        if (x >= hd) return py + (y - py) * ((hd - px) / std::max(1e-6, x - px));
                    }
                    return -1000.0;
                };
                const double hdir = std::hypot(dir.x, dir.z), base = std::atan2(dir.y, hdir);
                if (hdir > 1e-4) {
                    double p0 = base, e0 = endpointY(p0) - to.y;
                    double p1 = std::abs(e0)<0.01?p0:p0-std::atan2(e0,hd), e1 = endpointY(p1) - to.y;
                    for (int i = 0; i < 4 && std::abs(e1) > 0.005; ++i) {
                        const double diff = e1 - e0;
                        if (std::abs(diff) < 1e-6) break;
                        const double p2 = p1 - e1 * (p1 - p0) / diff;
                        p0 = p1; e0 = e1; p1 = std::clamp(p2, -1.2, 1.2); e1 = endpointY(p1) - to.y;
                    }
                    if (std::abs(e1) <= 0.25 && std::abs(p1 - base) <= 0.35)
                        dir = {dir.x / hdir * std::cos(p1), std::sin(p1), dir.z / hdir * std::cos(p1)};
                }
            }
        }
        const double spread = (state.grounded ? 5.5 : 11.0) * (0.45 + 0.55 * bloom_) * pi / 180.0;
        const double radius = spread * std::sqrt(random()), angle = random() * 2.0 * pi;
        glm::dvec3 side(-dir.z, 0.0, dir.x);
        if (glm::length(side) < 1e-4) side = {1.0, 0.0, 0.0};
        side = glm::normalize(side);
        const glm::dvec3 up = glm::cross(dir, side);
        dir = glm::normalize(dir + side * (std::cos(angle) * std::tan(radius))
                             + up * (std::sin(angle) * std::tan(radius) * 0.55));
        projectiles_.push_back({muzzle, dir * 34.0, 0.0});
        ++shotsFired_;
        projectileTrail_.push_back(-(2.5 - 1.05));
        projectileSeed_.push_back(random());
        bloom_ = std::min(1.0, bloom_ + 0.3);
        cooldown_ += 0.1;
    }
}

void PlayerSimulation::updateProjectiles(double dt) {
    for (std::size_t i = 0; i < projectiles_.size();) {
        auto& p = projectiles_[i];
        const glm::dvec3 prev = p.pos;
        p.age += dt;
        if (p.age > 0.13) { p.vel.y -= 28.0 * dt; p.vel *= 1.0 - 0.8 * dt; }
        p.pos += p.vel * dt;
        bool done = false;
        const glm::dvec3 path = p.pos - prev;
        const double pathLength = glm::length(path);
        if (pathLength > 0.0) {
            const RayHit hit = geometry_.raycast(prev, path / pathLength, pathLength, true);
            if (hit.hit) {
                SplatOptions options;
                options.stretch = glm::normalize(p.vel);
                options.hasStretch = true;
                options.stretchAmt = 0.7;
                const double radius = 0.85 * (0.85 + random() * 0.3);
                const double area = paint_.splat(hit.point + hit.normal * 0.14, radius, team_, projectileSeed_[i], options);
                turfPainted_ += area;
                state.specialCharge = std::min(190.0, state.specialCharge + area * 0.8);
                done = true;
            }
        }
        if (!done) {
            projectileTrail_[i] += glm::length(p.vel) * dt;
            if (projectileTrail_[i] > 1.05) {
                projectileTrail_[i] = 0.0;
                const RayHit hit = geometry_.raycast(p.pos, {0.0, -1.0, 0.0}, 4.0, true);
                if (hit.hit) {
                    const double area = paint_.splat(hit.point + hit.normal * 0.1,
                                                     0.44 * (0.8 + random() * 0.4), team_, random());
                    turfPainted_ += area;
                    state.specialCharge = std::min(190.0, state.specialCharge + area * 0.8);
                }
            }
        }
        if (done || p.age > 1.2 || p.pos.y < -3.4) {
            projectiles_.erase(projectiles_.begin() + i);
            projectileTrail_.erase(projectileTrail_.begin() + i);
            projectileSeed_.erase(projectileSeed_.begin() + i);
        } else ++i;
    }
}

void PlayerSimulation::tick(double dt) {
    if (!(dt > 0.0) || !std::isfinite(dt)) throw std::invalid_argument("player tick requires positive finite dt");
    time_ += dt;
    updateProjectiles(dt);
    if (!state.alive) {
        state.respawnTimer -= dt;
        if (state.respawnTimer <= 0.0) {
            const double special = state.specialCharge;
            spawnAt(stage_.spawnPads[team_] + glm::dvec3(std::cos(0.6)*1.1,4.5,std::sin(0.6)*1.1), team_ == 0 ? 0.0 : pi);
            state.specialCharge = special;
            state.grounded = false;
            state.vel.y = -4.0;
        }
        return;
    }
    const bool firePressed = input.fire && !prevFire_;
    const bool jumpPressed = input.jump && !prevJump_;
    if (input.squid && !prevSquid_) squidPressT_ = time_;
    if (firePressed) firePressT_ = time_;
    prevFire_ = input.fire; prevJump_ = input.jump; prevSquid_ = input.squid;
    state.invuln = std::max(0.0, state.invuln - dt);
    lastDamage_ += dt; lastFire_ += dt; state.landT += dt; kidT_ += dt;
    hardLand_ = std::max(0.0, hardLand_ - dt / 0.16);
    jumpBuffer_ = jumpPressed ? 0.13 : std::max(0.0, jumpBuffer_ - dt);
    fireBuffer_ = firePressed ? 0.16 : std::max(0.0, fireBuffer_ - dt);
    const bool fireWins = (input.fire || fireBuffer_ > 0.0) && firePressT_ >= squidPressT_;
    const bool squid = input.squid && !fireWins;
    if (state.squid && !squid) kidT_ = 0.0;
    state.squid = squid;
    surface();
    state.submerged = state.squid && state.grounded && state.groundTeam == 1;
    const bool enemy = state.grounded && state.groundTeam == 2 && !state.submerged;
    climb(dt);
    if (!state.climbing) horizontal(dt, enemy);
    coyote_ = state.grounded ? 0.12 : coyote_ - dt;
    bool jumped = false;
    if (jumpBuffer_ > 0.0 && (state.grounded || coyote_ > 0.0) && !state.climbing) {
        state.vel.y = (state.submerged ? 9.4 : 8.4) * (enemy ? 0.72 : 1.0);
        state.grounded = false; coyote_ = jumpBuffer_ = 0.0;
        jumped = true;
    }
    integrate(dt, jumped);
    const auto pad=stage_.spawnPads[1-team_];const auto delta=state.pos-pad;
    const double distance=std::hypot(delta.x,delta.z);
    if(distance<stage_.spawnBarrier && state.pos.y>pad.y-1.0) {
        const double d=std::max(distance,0.01),push=(stage_.spawnBarrier-distance)/d;
        state.pos.x+=delta.x*push;state.pos.z+=delta.z*push;
        const double vn=(state.vel.x*delta.x+state.vel.z*delta.z)/d;
        if(vn<0) {state.vel.x-=delta.x/d*vn*1.6;state.vel.z-=delta.z/d*vn*1.6;}
    }
    if (enemy) {
        if (damageFromInk_ < 40.0 && state.invuln <= 0.0) {
            const double d = std::min(20.0 * dt, 40.0 - damageFromInk_);
            damageFromInk_ += d;
            state.hp = std::max(1.0, state.hp - d);
        }
        lastDamage_ = std::min(lastDamage_, 0.4);
    } else damageFromInk_ = std::max(0.0, damageFromInk_ - dt * 30.0);
    if (lastDamage_ > 1.3 && state.hp < 100.0)
        state.hp = std::min(100.0, state.hp + (state.submerged ? 60.0 : 22.0) * dt);
    if (state.submerged || state.climbing) state.ink = std::min(100.0, state.ink + 42.0 * dt);
    else if (!squid && lastFire_ > 0.9) state.ink = std::min(100.0, state.ink + 9.0 * dt);
    else if (squid) state.ink = std::min(100.0, state.ink + 4.5 * dt);
    const bool firing = !squid && kidT_ >= 0.07 && (input.fire || fireBuffer_ > 0.0);
    shoot(dt,firing);
    if(!squid && kidT_>=0.07) fireBuffer_=0.0;
    if (state.pos.y < -1.45 && !std::isfinite(geometry_.groundHeight(state.pos.x, state.pos.z, state.pos.y + 0.6)))
        splat();
    const double mh = std::hypot(input.move.x, input.move.z);
    const double hs = horizontalLength(state.vel);
    double target = state.yaw;
    bool hasTarget=true;
    double omega = 20.0, maxRate = 12.5, maxAcc = 170.0;
    fireFacing_=std::max(0.0,fireFacing_-dt);
    if (firingT_>0.0 || fireFacing_>0.0) {
        target = input.aimYaw; omega = 36.0; maxRate = 24.0; maxAcc = 380.0;
    } else if (state.climbing) {
        target = std::atan2(-wallNormal_.x, -wallNormal_.z);
        omega = 26.0; maxRate = 18.0;
    } else {
        if (mh > 0.2) target = std::atan2(input.move.x, input.move.z);
        else if (hs > 0.6) target = std::atan2(state.vel.x, state.vel.z);
        else hasTarget=false;
        if (state.squid) { omega = 26.0; maxRate = state.submerged ? 14.0 : 17.0; maxAcc = 260.0; }
    }
    double targetRate = 0.0;
    if (hasTarget && hasFaceTarget_) {
        const double delta = angleDifference(faceTarget_, target);
        if (std::abs(delta) < 0.12) targetRate = std::clamp(delta / dt, -maxRate, maxRate);
    }
    faceTarget_ = target; hasFaceTarget_ = hasTarget;
    const double acceleration = hasTarget?omega * omega * angleDifference(state.yaw, target)
        + 2.0 * omega * (targetRate - yawVelocity_):-2.0*omega*yawVelocity_;
    yawVelocity_ = std::clamp(yawVelocity_ + std::clamp(acceleration, -maxAcc, maxAcc) * dt, -maxRate, maxRate);
    if(std::abs(yawVelocity_)<1e-5) yawVelocity_=0.0;
    state.yaw = std::remainder(state.yaw + yawVelocity_ * dt, 2.0 * pi);
    const double acc = -24.0 * 24.0 * state.smoothY - 48.0 * smoothYVelocity_;
    smoothYVelocity_ += acc * dt;
    state.smoothY += smoothYVelocity_ * dt;
    state.smoothY = std::clamp(state.smoothY, -0.7, 0.7);
    if (std::abs(state.smoothY) < 1e-4 && std::abs(smoothYVelocity_) < 1e-3)
        state.smoothY = smoothYVelocity_ = 0.0;
}

} // namespace restoration
