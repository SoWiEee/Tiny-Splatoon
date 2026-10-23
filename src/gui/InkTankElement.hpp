#pragma once

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Geometry.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace restoration {

struct InkTankFrame {
    double ink=100,subCost=70;
    bool low=false,aiming=false,reducedMotion=false;
    double yaw=0,vx=0,vz=0,dt=0;
    Rml::Colourb color{255,138,20,255};
};

class InkTankElement final : public Rml::Element {
public:
    explicit InkTankElement(const Rml::String& tag);
    void Update(const InkTankFrame& frame);
    void Reset();

protected:
    void OnRender() override;

private:
    struct Bubble { float x=0,y=0,r=0,v=0; };
    InkTankFrame frame_;
    Rml::Geometry geometry_;
    std::array<Bubble,14> bubbles_{};
    std::size_t bubbleCount_=0;
    double level_=1,time_=0,slosh_=0,sloshVelocity_=0,previousInk_=1,bubbleCredit_=0;
    double previousYaw_=0,previousVx_=0,previousVz_=0;
    std::uint32_t randomState_=0x91e10da5u;
    bool initialized_=false,haveYaw_=false,haveVelocity_=false;
};

} // namespace restoration
