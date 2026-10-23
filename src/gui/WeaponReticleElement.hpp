#pragma once

#include "../game/WeaponRunner.hpp"
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Geometry.h>

#include <cstdint>
#include <string>

namespace restoration {

struct WeaponReticleFrame {
    std::string weapon="shooter";
    CombatPose pose{};
    double spreadPx=0,dt=0,burstDuration=1;
    std::uint64_t shotsFired=0;
    bool grounded=true,clinging=false;
    Rml::Colourb color{255,255,255,255},inkColor{255,138,20,255};
};

class WeaponReticleElement final : public Rml::Element {
public:
    explicit WeaponReticleElement(const Rml::String& tag);
    void Update(const WeaponReticleFrame& frame);
    void Reset();

protected:
    void OnRender() override;

private:
    WeaponReticleFrame frame_;
    Rml::Geometry geometry_;
    double bloom_=0;
    std::uint64_t previousShots_=0;
    bool initialized_=false;
};

} // namespace restoration
