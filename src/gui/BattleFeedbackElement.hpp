#pragma once

#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Geometry.h>

#include <vector>

namespace restoration {

struct DamageDirection {
    double angle=0,age=0,amount=0;
    Rml::Colourb color;
};

struct BattleFeedbackFrame {
    double hitAge=99,killAge=99,hitDamage=0,invuln=0,time=0;
    int hitChain=0;
    bool reducedMotion=false;
    Rml::Colourb color;
    std::vector<DamageDirection> directions;
};

class BattleFeedbackElement final : public Rml::Element {
public:
    explicit BattleFeedbackElement(const Rml::String& tag);
    void Update(const BattleFeedbackFrame& frame);

protected:
    void OnRender() override;

private:
    BattleFeedbackFrame frame_;
    Rml::Geometry geometry_;
};

class HudRingElement final : public Rml::Element {
public:
    explicit HudRingElement(const Rml::String& tag);
    void Update(double fraction,Rml::Colourb color,Rml::Colourb background);

protected:
    void OnRender() override;

private:
    double fraction_=0;
    Rml::Colourb color_{255,255,255,255},background_{0,0,0,0};
    Rml::Geometry geometry_;
};

} // namespace restoration
