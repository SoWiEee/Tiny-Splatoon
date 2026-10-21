#pragma once

#include "WorldFxRecipes.hpp"
#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace restoration {
class CombatWorld;
class CharacterData;
struct WorldVisualEvent;
struct CharacterLightState;
class DeviceRenderer;

// fx.js' short-lived pools and specials.js' persistent world visuals, drawn inside the HDR scene.
class WorldEffects {
public:
    using Shape = WorldFxShape;
    using Item = WorldFxParticle;
    struct Counts { std::size_t drops=0,puffs=0,glows=0,rings=0,shells=0,beams=0,world=0; };

    explicit WorldEffects(const CharacterData& data);
    ~WorldEffects();
    WorldEffects(const WorldEffects&) = delete;
    WorldEffects& operator=(const WorldEffects&) = delete;
    void Update(double dt, CombatWorld& world, glm::dvec3 camera);
    int Draw(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& camera,const CharacterLightState* lights=nullptr);
    void SetDeviceRenderer(DeviceRenderer* models){models_=models;}
    void Clear();
    Counts counts() const;
    const std::vector<Item>& liveItems() const { return live_; }
    const std::vector<Item>& worldItems() const { return world_; }
    // The queue has a single simulation consumer; screen FX may read this
    // immutable copy without draining CombatWorld a second time.
    const std::vector<WorldVisualEvent>& lastEvents() const { return lastEvents_; }

private:
    void spawn(Item item);
    void burst(glm::vec3 pos, glm::vec3 normal, glm::vec3 color, float size=0.1f, int count=12);
    void explosion(glm::vec3 pos, glm::vec3 color, float radius);
    void emitSpecial(const char* kind, glm::vec3 pos, glm::vec3 normal, glm::vec3 color, float radius, glm::vec3 camera);
    float random();
    glm::vec3 cone(glm::vec3 normal, float spread);
    void initGpu();
    void initSourceGpu();
    struct GpuMesh;
    const GpuMesh& sourceMesh(const char* kind, const char* role);
    int drawSourceItems(const glm::mat4& view, const glm::mat4& projection,const glm::vec3& camera,const CharacterLightState* lights);
    DeviceRenderer* models_=nullptr;
    const CharacterData* data_ = nullptr;
    std::vector<Item> live_, world_;
    std::vector<WorldVisualEvent> lastEvents_;
    struct ActorState { glm::dvec3 pos{0}; double hp=100,charge=0,step=0,born=0,climbEnd=-9,wakeTimer=0; bool initialized=false,alive=true,squid=false,swimming=false,climbing=false,grounded=false; std::string special,phase; };
    std::unordered_map<const void*,ActorState> actors_;
    std::uint32_t random_ = 0x6d2b79f5u;
    float time_ = 0;
    unsigned vao_ = 0, vbo_ = 0, program_ = 0;
    unsigned sourceProgram_ = 0;
    std::unordered_map<std::string,std::unique_ptr<GpuMesh>> sourceMeshes_;
};
}
