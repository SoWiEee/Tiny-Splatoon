#pragma once

namespace restoration {

enum class GraphicsPreset { Low, Medium, High, Custom };

struct GraphicsSettings {
    GraphicsPreset preset=GraphicsPreset::Medium;
    float renderScale=.85f;
    int shadowMapSize=1024;
    bool shadows=true;
    bool reflections=false;
    bool postEffects=true;
    bool antialiasing=true;
};

inline GraphicsSettings ForPreset(GraphicsPreset preset) {
    GraphicsSettings settings;
    settings.preset=preset;
    switch(preset){
    case GraphicsPreset::Low:
        settings.renderScale=.65f;settings.shadowMapSize=0;settings.shadows=false;settings.reflections=false;settings.postEffects=false;settings.antialiasing=false;
        break;
    case GraphicsPreset::Medium:
        settings.renderScale=.85f;settings.shadowMapSize=1024;settings.shadows=true;settings.reflections=false;settings.postEffects=true;
        break;
    case GraphicsPreset::High:
        settings.renderScale=1.f;settings.shadowMapSize=2048;settings.shadows=true;settings.reflections=true;settings.postEffects=true;
        break;
    case GraphicsPreset::Custom:
        break;
    }
    return settings;
}

} // namespace restoration
