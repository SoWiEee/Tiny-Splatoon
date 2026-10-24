#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace restoration {

class CharacterData;
struct CharacterStyle;

inline constexpr std::size_t CharacterBoneCount = 87;
inline constexpr std::size_t CharacterPoseChannelCount = 126;

struct CharacterTransform {
    glm::dvec3 position{0.0};
    glm::dquat rotation{1.0, 0.0, 0.0, 0.0};
    glm::dvec3 scale{1.0};
    bool visible = true;
    // Some source nodes deliberately use a non-decomposable affine transform (for example
    // the brolly strut rig). Preserve that matrix instead of losing its shear in TRS export.
    bool matrixOverride = false;
    glm::dmat4 authoredMatrix{1.0};
    glm::dmat4 matrix() const;
};

struct CharacterBone {
    std::string name;
    int parent = -1;
    glm::dvec3 rest{0.0}; // Absolute kid-space source rest position.
    glm::dmat4 inverseBind{1.0};
};

struct CharacterHairStrand {
    glm::dvec3 direction{0.0, -1.0, 0.0};
    double length = 0.0, stiffness = 1.0, gravity = 1.0;
};

struct CharacterRig {
    std::array<CharacterBone, CharacterBoneCount> bones;
    std::vector<CharacterHairStrand> hair;
};

enum class CharacterForm { Kid, Squid, Swim, Climb };

struct CharacterRunnerState {
    bool charging = false, firing = false, streaming = false, dodge = false;
    double lockT = 0.0, charge = 0.0, streamT = 0.0;
    // Kit-specific values are filled by the source weapon runner adapter.
    double open = 0.0, punchT = 99.0, punchSide = 1.0, leapT = 99.0;
    int hand = 0;
    std::array<double,2> sinceHand{99.0,99.0};
    bool hasSinceHand = false, leaping = false, canopyOpening = false;
    double landT = 99.0, flashT = 99.0;
    double canopyOpen = 0.0, canopyGrow = 1.0, canopyHp = 0.0, canopyHpMax = 0.0;
    double canopyShake = 0.0, canopyFlash = 0.0, canopyRegrowK = 0.0;
    double bladeFlashTime = 99.0, mittsLandTime = 99.0;
    std::string canopyState = "ready";
};

struct CharacterAnimState {
    glm::dvec3 position{0.0};
    double yaw = 0.0, aimPitch = 0.0, speed = 0.0;
    glm::dvec2 localMove{0.0}; // Source x/z input; +x input becomes kid-space -x.
    std::optional<double> vy, turnRate, hp;
    CharacterForm form = CharacterForm::Kid;
    bool grounded = true, firing = false, rolling = false, subAim = false;
    bool lowInk = false, inEnemyInk = false, invuln = false, visible = true, subPropHidden = false;
    double charge = 0.0, ink = 1.0, special = 0.0;
    bool shakerCharging=false;double shakerCharge=0;
    glm::dvec3 wallNormal{0.0, 0.0, 1.0};
    std::optional<CharacterRunnerState> runner;
};

struct CharacterGroundHit {
    bool hit = false;
    double distance = 0.0;
    glm::dvec3 point{0.0}, normal{0.0, 1.0, 0.0};
};

struct CharacterAttentionActor {
    std::uint64_t id = 0;
    int team = -1;
    bool alive = true;
    glm::dvec3 position{0.0};
};

struct CharacterSceneInput {
    bool inWorld = false, isLocal = false;
    int team = -1, lifeLevel = 2;
    std::optional<glm::dvec3> cameraPosition;
    std::vector<CharacterAttentionActor> actors;
    std::function<CharacterGroundHit(glm::dvec3 origin, glm::dvec3 direction, double distance)> raycast;
    std::function<void(int side, glm::dvec3 position, double speed)> footstep;
};

struct CharacterMaterialState {
    glm::dvec3 team{1.0}, shirt{1.0}, shorts{1.0}, shoe{1.0}, sole{1.0}, sock{1.0}, strap{1.0};
    glm::dvec3 iris{1.0}, iris2{1.0}, flash{0.0}, glow{0.0};
    glm::dvec4 hurt{0.0}, mouth{0.75, 1.0, 0.0, 0.0}, mouth2{0.0}, lid{0.0}, gaze{0.0};
    glm::dvec2 look{0.0};
    glm::dvec3 wig{0.012, 9.0, 0.0}, rimLight{0.0, 0.0, 1.0}, fillLight{0.0}, tankEmissive{0.0};
    glm::dvec4 rim{0.0, 0.0, 0.0, 3.0};
    double pattern = 0.0, hurtSeed = 0.0, time = 0.0, opacity = 1.0, freckle = 0.0, pupil = 0.5;
    double coilEmissiveIntensity = 0.15;
};

struct CharacterPartPose {
    std::string name; // Source object path, stable within one weapon/form definition.
    int parentBone = -1;
    CharacterTransform local;
    glm::dmat4 kidMatrix{1.0};
    glm::dvec3 emissive{0.0};
    double emissiveIntensity = 1.0;
    std::optional<glm::dvec3> color;
    std::optional<double> opacity;
    std::optional<bool> transparent;
    std::map<std::string, std::vector<double>> uniforms;
};

struct CharacterPoseFrame {
    CharacterTransform root, model, kid, squid;
    std::array<CharacterTransform, CharacterBoneCount> bones;
    // Kid-space bone world * inverse bind; renderer applies root/model/kid separately.
    std::array<glm::dmat4, CharacterBoneCount> skinMatrices;
    std::vector<CharacterPartPose> parts;
    CharacterMaterialState material;
    glm::dvec3 muzzle{0.0}, muzzleLeft{0.0}, aimMuzzle{0.0}, head{0.0};
    double aimReady=1.0;
    glm::dvec3 shotMuzzle(int side) const {
        if(side>0)return muzzleLeft;
        return aimReady<0.98?muzzle+(aimMuzzle-muzzle)*(1.0-aimReady):muzzle;
    }
    glm::dmat4 subPropWorld{1.0};
    glm::dvec3 subNozzle{0.0},subNozzleDirection{0,1,0};
    double shakerKick=0.0;
    std::array<float, CharacterPoseChannelCount> channels{};
};

struct CharacterTrigger {
    std::string name;
    std::optional<double> amount;
    glm::dvec3 direction{0.0, 0.0, 1.0};
    bool hasDirection = false;
    int hand = 0, side = 1;
    double duration = 0.3;
};

// The source pose is stateful: a new instance is required at each actor lifetime.
// Runtime declarations are implemented in successive source-verified packages;
// the viewer must not treat an unverified foundation as the completed character.
class CharacterPose {
public:
    CharacterPose(const CharacterData& data, const CharacterRig& rig, std::string name, std::string weapon);
    ~CharacterPose();
    CharacterPose(CharacterPose&&) noexcept;
    CharacterPose& operator=(CharacterPose&&) noexcept;
    CharacterPose(const CharacterPose&) = delete;
    CharacterPose& operator=(const CharacterPose&) = delete;
    void setWeapon(const std::string& weapon);
    void setSub(const std::string& sub);
    void setDance(const std::string& dance,int variant=-1,double startTime=0.0);
    void setStyle(const CharacterStyle& style);
    void setHurt(double amount, glm::dvec3 enemyLinearColor);
    void setColor(glm::dvec3 linearColor);
    void trigger(const CharacterTrigger& event);
    const CharacterPoseFrame& update(double dt, const CharacterAnimState& state, const CharacterSceneInput& scene);
    const CharacterPoseFrame& frame() const;
private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace restoration
