#pragma once

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Geometry.h>

#include <string>

namespace restoration {

struct SpecialGaugeFrame {
    double charge=0,subFill=1,dt=0,time=0;
    bool active=false,ready=false,rejected=false,reducedMotion=false;
    std::string subKind="bomb";
    Rml::Colourb color{255,138,20,255};
};

class SpecialGaugeElement final : public Rml::Element {
public:
    explicit SpecialGaugeElement(const Rml::String& tag);
    void Update(const SpecialGaugeFrame& frame);
    void Reset();

protected:
    void OnRender() override;

private:
    SpecialGaugeFrame frame_;
    Rml::Geometry geometry_;
    double time_=0,gainFlash_=0,readyFlash_=0,rejectFlash_=0,previousCharge_=0;
    bool initialized_=false,previousReady_=false,previousRejected_=false;
};

} // namespace restoration
