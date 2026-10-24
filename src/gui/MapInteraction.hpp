#pragma once

#include "GameUi.hpp"
#include <cstdint>
#include <memory>

namespace Rml { class Context; class ElementDocument; }
namespace restoration {
class RmlRenderer;
struct StageData;
class FacePaint;
class MatchSimulation;

struct MapDeathMark {
    glm::dvec3 pos{0.0};
    std::string name;
    int team=0;
    double born=0.0;
    std::uint64_t serial=0;
};

// Owns map presentation state and the ordering between map layout and cursor hit testing.
class MapInteraction {
public:
    MapInteraction();
    ~MapInteraction();
    MapInteraction(const MapInteraction&)=delete;
    MapInteraction& operator=(const MapInteraction&)=delete;

    void SetFrame(UiMapFrame frame,RmlRenderer& renderer);
    void SetWorldView(const glm::mat4& viewProjection);
    void Invalidate();
    bool open() const;
    int queuedKey() const;
    const std::string& queuedLabel() const;
    bool SelectTarget(int key);
    bool SelectHoveredTarget();
    int TakeSelection();

    void Update(Rml::Context* context,Rml::ElementDocument* hud,
                const StageData& stage,const FacePaint& paint,MatchSimulation& match,
                const PlayerProfile& player,const std::vector<MapDeathMark>& deathMarks,
                double deathClock);
    void LayoutAndHitTest(Rml::Context* context,Rml::ElementDocument* hud,
                          const glm::dvec2& pointerPosition,double scale);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace restoration
