#pragma once

namespace restoration {
struct StageData;
class FacePaint;
class MatchSimulation;

enum class MatchHudAction { None, Resume, StageSelect, Close };
MatchHudAction DrawTurfHud(const StageData& stage,const FacePaint& paint,
                           MatchSimulation& match,int width,int height,bool paused,bool nativeApp);
} // namespace restoration
