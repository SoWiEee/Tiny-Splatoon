#pragma once

#include "../game/PlayerProfile.hpp"
#include "../render/GraphicsSettings.hpp"
#include "../network/RoomTypes.hpp"
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <glm/glm.hpp>

struct GLFWwindow;
namespace restoration {
struct StageData;
class FacePaint;
class MatchSimulation;

enum class UiAction { None, Start, Practice, Resume, StageSelect, Close, Rematch, PracticeReset, PracticeFill, PracticeSpawn, PracticeStage };
enum class UiScreen { Title, Home, Play, Loadout, Locker, Settings, HowTo, Credits, Online, Match, Pause, Results, Practice, PracticeGear, LeaveConfirm };
enum class UiPreviewType { None, Player, Loadout, Locker, Podium, Practice };
struct UiPreview {
    float x=0,y=0,width=0,height=0;
    UiScreen screen=UiScreen::Title;
    UiPreviewType type=UiPreviewType::None;
};
struct UiSetup {
    std::string stage="tidewater",mode="turf",main="shooter",sub="bomb",special="burst",difficulty="normal",theme="day";
    int duration=180;
    bool reducedMotion=false,practice=false;
};
struct UiJumpMapTarget {
    int key=0;
    double x=0.0,z=0.0;
    std::string label;
    bool available=false;
    double y=0.0;
    std::string weapon;
};
struct UiOnlineRequest {
    enum class Kind { Create, Join, Leave, Ready, Team, Settings, Start, Emote, LocalService };
    Kind kind=Kind::Create;
    std::string address,code,emote;
    network::RoomSettings settings;
    int team=-1;
    bool ready=false;
};

// The player interface shares the game's context; documents own presentation only.
class GameUi {
public:
    GameUi(GLFWwindow* window,const std::filesystem::path& assets="assets");
    ~GameUi();
    GameUi(const GameUi&)=delete;
    GameUi& operator=(const GameUi&)=delete;
    void ShowMenu(const std::string& initialStage);
    void ShowTitle(const std::string& initialStage);
    void ShowPracticeStage(const std::string& initialStage);
    void ShowPracticeGear();
    UiAction RenderMenu(int width,int height);
    UiAction RenderHud(const StageData&,const FacePaint&,MatchSimulation&,int width,int height,bool paused,bool nativeApp,bool practice=false);
    const UiSetup& setup() const;
    const PlayerProfile& profile() const;
    const GraphicsSettings& graphicsSettings() const;
    UiPreview preview() const;
    void SetProfile(PlayerProfile profile);
    void SetProfileSink(std::function<void(const PlayerProfile&)> sink);
    void SetSaveError(std::string error);
    void SetReward(MatchRewardResult reward);
    void SetSoundSink(std::function<void(const std::string&)> sink);
    void SetVolumeSink(std::function<void(float,float,float)> sink);
    void SetJumpMap(bool open,std::vector<UiJumpMapTarget> targets,int queuedKey=-1,std::string queuedLabel={});
    void SetWorldView(const glm::mat4& viewProjection,const glm::dvec3& eye,float projectionY=1.78f);
    void SetNativeMap(unsigned int texture,const glm::mat4& viewProjection);
    int TakeJumpMapSelection();
    void ShowOnline();
    void SetOnlineState(const network::RoomView& view);
    std::vector<UiOnlineRequest> TakeOnlineRequests();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace restoration
