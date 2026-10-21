#pragma once

#include "../render/CharacterRenderData.hpp"
#include "../render/GraphicsSettings.hpp"
#include <array>
#include <filesystem>
#include <string>
#include <string_view>

namespace restoration {

struct PlayerSettings {
    double sensitivity=1, padSensitivity=1, fov=82, cameraShake=1;
    double master=0.8, music=0.6, sfx=0.85, rumble=1, aimAssist=1, touchSensitivity=1;
    bool invertY=false, shadows=true, bloom=true, showFps=false, colorblind=false, minimap=true;
    bool aimAssistMouse=false, touchInvertY=false, fullscreen=false, qualityChosen=false, shadowsChosen=false, reducedMotion=false;
    int fpsCap=0, matchLength=180, bossLength=240, palette=0;
    std::string quality="high", lastMode="turf", difficulty="normal", touchControls="auto", timeOfDay="day";
    GraphicsSettings graphics{};
};

struct PlayerProfile {
    std::string name="Player", weapon="shooter", sub, special;
    int level=1, xp=0, wins=0, matches=0;
    double totalTurf=0;
    CharacterStyle style{};
    PlayerSettings settings{};
    std::string lastSettledMatch;
};

struct MatchRewardInput {
    std::string matchId;
    std::string mode="turf";
    bool won=false, knockout=false;
    double turf=0, zoneTurf=0, bossDamage=0;
    int splats=0;
};

struct MatchRewardResult {
    bool applied=false;
    int gained=0, levelBefore=1, levelAfter=1, xpBefore=0, xpAfter=0;
    int xpToNextBefore=1150, xpToNextAfter=1150;
};

// A matchId must be stable across duplicate result callbacks and distinct for each match.
MatchRewardResult ApplyMatchReward(PlayerProfile& profile, const MatchRewardInput& reward);
int XpForLevel(int level);
void NormalizeProfile(PlayerProfile& profile);
std::array<std::string_view,2> ProfilePaletteColors(int palette, bool colorblind=false);

class ProfileStore {
public:
    explicit ProfileStore(std::filesystem::path path={});
    PlayerProfile load() const;
    void save(const PlayerProfile& profile) const;
    const std::filesystem::path& path() const { return path_; }
private:
    std::filesystem::path path_;
};

} // namespace restoration
