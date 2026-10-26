#pragma once
#include <filesystem>
struct GLFWwindow;
namespace restoration {
struct LaunchOptions;
struct StageData;
class GameUi;
class AudioEngine;
class GameAudio;
class ProfileStore;
class PlayerShowcase;
class OnlinePlay;
enum class MatchExit { Close, StageSelect, Rematch, PracticeStage };
// Owns one match and releases its bindings/resources before returning to the host.
MatchExit RunMatchSession(GLFWwindow* window,const LaunchOptions& options,const StageData& stage,
    const std::filesystem::path& assets,GameUi& ui,AudioEngine& audio,GameAudio& gameAudio,
    bool nativeApp=false,ProfileStore* store=nullptr,PlayerShowcase* showcase=nullptr,OnlinePlay* online=nullptr);
}
