#pragma once

#include "../game/PlayerProfile.hpp"
#include "../render/GraphicsSettings.hpp"
#include <filesystem>
#include <functional>
#include <memory>
#include <string>

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
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace restoration
