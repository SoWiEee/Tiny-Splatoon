#pragma once

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

struct GLFWwindow;
namespace restoration {
struct StageData;
class FacePaint;
class MatchSimulation;

enum class UiAction { None, Start, Resume, StageSelect, Close };
struct UiSetup {
    std::string stage="tidewater",mode="turf",main="shooter",sub="bomb",special="burst",difficulty="normal";
    int duration=240;
    bool reducedMotion=false;
};

// The player interface shares the game's context; documents own presentation only.
class GameUi {
public:
    GameUi(GLFWwindow* window,const std::filesystem::path& assets="assets");
    ~GameUi();
    GameUi(const GameUi&)=delete;
    GameUi& operator=(const GameUi&)=delete;
    void ShowMenu(const std::string& initialStage);
    UiAction RenderMenu(int width,int height);
    UiAction RenderHud(const StageData&,const FacePaint&,MatchSimulation&,int width,int height,bool paused,bool nativeApp);
    const UiSetup& setup() const;
    void SetSoundSink(std::function<void(const std::string&)> sink);
    void SetVolumeSink(std::function<void(float,float,float)> sink);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace restoration
