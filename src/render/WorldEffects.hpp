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
class WorldFxParticleRenderer;
class FacePaint;
class PlayerSimulation;

// fx.js' short-lived pools and specials.js' persistent world visuals, drawn inside the HDR scene.
class WorldEffects {
public:
    using Shape = WorldFxShape;
    using Item = WorldFxParticle;
    struct Counts { std::size_t drops=0,puffs=0,glows=0,rings=0,shells=0,beams=0,world=0,bossBlobs=0; };

    explicit WorldEffects(const CharacterData& data);
    ~WorldEffects();
    WorldEffects(const WorldEffects&) = delete;
    WorldEffects& operator=(const WorldEffects&) = delete;
    void Update(double dt, CombatWorld& world, glm::dvec3 camera);
    int Draw(const glm::mat4& view, const glm::mat4& projection, const glm::vec3& camera,const CharacterLightState* lights=nullptr);
    void SetDeviceRenderer(DeviceRenderer* models){models_=models;}
    void SetPaintSurface(FacePaint* paint){paint_=paint;}
    void Footstep(const PlayerSimulation& actor,glm::dvec3 position,double speed);
    // Pass the crablet's ground-root position when its source pop reaches 0.14 s.
    void BossCrabletPop(glm::dvec3 rootPosition,int team=1);
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
    void weaponHooks(double dt, CombatWorld& world, glm::dvec3 camera);
    void weaponDeviceHooks(double dt, CombatWorld& world, glm::dvec3 camera);
    void kitHooks(double dt, CombatWorld& world, glm::dvec3 camera);
    void specialHooks(double dt, CombatWorld& world, glm::dvec3 camera);
    void warningHooks(double dt, CombatWorld& world, glm::dvec3 camera);
    float random();
    glm::vec3 cone(glm::vec3 normal, float spread);
    void initSourceGpu();
    struct GpuMesh;
    const GpuMesh& sourceMesh(const char* kind, const char* role);
    int drawSourceItems(const glm::mat4& view, const glm::mat4& projection,const glm::vec3& camera,const CharacterLightState* lights);
    DeviceRenderer* models_=nullptr;
    FacePaint* paint_=nullptr;
    glm::dvec3 camera_{0.0};
    const CharacterData* data_ = nullptr;
    std::vector<Item> live_, world_;
    std::vector<WorldVisualEvent> lastEvents_;
    struct ActorState { glm::dvec3 pos{0}; double hp=100,charge=0,step=0,born=0,climbEnd=-9,wakeTimer=0; bool initialized=false,alive=true,squid=false,swimming=false,climbing=false,grounded=false,footsteps=false; std::string special,phase; };
    std::unordered_map<const void*,ActorState> actors_;
    struct WeaponFxState { double drip=0,spark=0,spin=0,dryT=-9,flickT=-9,dodgeTime=0; int shotsFired=-1; bool full=false,spinFull=false,burstFiring=false; };
    std::unordered_map<const void*,WeaponFxState> weaponActors_;
    struct KitFxActorState { int mittsLeapEvents=-1; bool mittsLeaping=false,mittsWasClimbing=false; double mittsLandTime=99.0,canopyHp=-1.0; std::string canopyState; glm::dvec3 canopyCenter{0},canopyNormal{0,0,1},mittsWallNormal{0,0,1}; };
    std::unordered_map<const PlayerSimulation*,KitFxActorState> kitActors_;
    struct KitFxDeviceState { double nextFxTime=0; glm::dvec3 previousPos{0}; bool initialized=false; };
    std::unordered_map<std::uint64_t,KitFxDeviceState> kitDevices_;
    struct SloshHead { glm::dvec3 pos{0},vel{0}; double time=0; int team=0; };
    std::unordered_map<std::uint64_t,SloshHead> sloshHeads_;
    std::unordered_map<std::uint64_t,double> weaponTrailDistance_;
    struct DeviceFxKey { std::uint32_t owner=0; std::uint64_t id=0; bool operator==(const DeviceFxKey& other) const { return owner==other.owner&&id==other.id; } };
    struct DeviceFxKeyHash { std::size_t operator()(const DeviceFxKey& key) const noexcept { return std::hash<std::uint64_t>{}(key.id ^ (std::uint64_t(key.owner)<<32)); } };
    struct BombFxState { glm::dvec3 ground{0},normal{0,1,0}; double beepT=0,previousVy=0; bool initialized=false,armed=false; };
    std::unordered_map<DeviceFxKey,BombFxState,DeviceFxKeyHash> bombFx_;
    struct SpecialFxState { double puddleT=0,flashT=0.5,pulseT=0; bool initialized=false; };
    std::unordered_map<DeviceFxKey,SpecialFxState,DeviceFxKeyHash> specialFx_;
    struct WarningFxActorState { double fizz=0,charge=0; bool charging=false; };
    std::unordered_map<const PlayerSimulation*,WarningFxActorState> warningActors_;
    struct WarningFxObjectState { double timer=0; int ticks=0; };
    std::unordered_map<DeviceFxKey,WarningFxObjectState,DeviceFxKeyHash> warningObjects_;
    std::uint32_t random_ = 0x6d2b79f5u;
    float time_ = 0;
    std::unique_ptr<WorldFxParticleRenderer> particles_;
    unsigned sourceProgram_ = 0;
    std::unordered_map<std::string,std::unique_ptr<GpuMesh>> sourceMeshes_;
};
}
