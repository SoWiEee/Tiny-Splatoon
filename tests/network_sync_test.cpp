#include "../src/network/MatchSync.hpp"
#include "../src/network/RoomServer.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace {
using namespace restoration;
using namespace restoration::network;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
PlayerSimulation& byId(MatchSimulation& match,std::uint32_t nid){
    for(const auto& actor:match.actors())if(actor->networkNid()==nid)return *actor;
    throw std::runtime_error("Missing network actor");
}
struct Scene {
    StageGeometry geometry;
    FacePaint paint;
    NavGraph nav;
    MatchSimulation match;
    MatchSync sync;
    Scene(const StageData& stage,RoomSession& session)
        :geometry(stage),paint(stage,geometry),nav(stage,geometry),
         match(stage,geometry,paint,nav,MatchSync::BuildOptions(session,{})),sync(session,match,paint){}
};
struct Fixture {
    GnsRuntime runtime;
    std::unique_ptr<RoomServer> server;
    std::vector<std::unique_ptr<RoomSession>> sessions;
    std::vector<std::unique_ptr<Scene>> scenes;
    std::chrono::steady_clock::time_point origin=std::chrono::steady_clock::now();
    SteamNetworkingIPAddr address{};
    Fixture(int clients,const std::string& mode="turf",bool bots=false,bool cargo=false,bool delayed=false){
        if(delayed){
            check(SteamNetworkingUtils()->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_FakePacketLag_Send,35),"Cannot enable GNS test lag");
            SteamNetworkingUtils()->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketReorder_Send,15);
            SteamNetworkingUtils()->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_FakePacketReorder_Time,20);
            SteamNetworkingUtils()->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketDup_Send,15);
        }
        for(unsigned port=52000;port<62000&&!server;++port){
            address.SetIPv4(0x7f000001u,static_cast<std::uint16_t>(port));
            try{server=std::make_unique<RoomServer>(runtime,address);}catch(const std::exception&){}
        }
        check(bool(server),"Cannot bind loopback room service");
        for(int i=0;i<clients;++i){
            sessions.push_back(std::make_unique<RoomSession>(runtime));
            Member member;member.name="Player "+std::to_string(i);member.main="shooter";member.sub="bomb";member.special="storm";
            if(i==0)sessions.back()->Create(address,member);
            else sessions.back()->Join(address,sessions[0]->view().code,member);
            wait([&]{return sessions[0]->view().members.size()==sessions.size()&&sessions.back()->view().phase==RoomPhase::Lobby;});
        }
        RoomSettings settings;settings.mode=mode;settings.duration=60;settings.bots=bots;
        if(cargo)for(const auto& stage:ReferenceStages())if(stage.mode=="turf"&&stage.noBots){settings.map=std::string(stage.id);break;}
        sessions[0]->SetSettings(settings);
        // Put the guest first in roster order: nid zero must remain a valid player ID.
        sessions[0]->SetTeam(1);sessions[1]->SetTeam(0);
        for(std::size_t i=2;i<sessions.size();++i)sessions[i]->SetTeam(0);
        for(auto& session:sessions)session->SetReady(true);
        wait([&]{return std::all_of(sessions[0]->view().members.begin(),sessions[0]->view().members.end(),[](const auto& m){return m.ready;})&&sessions[0]->view().settings.mode==mode;});
        check(sessions[0]->StartMatch(),"Host start request rejected");
        wait([&]{return std::all_of(sessions.begin(),sessions.end(),[](const auto& s){return s->view().phase==RoomPhase::Starting&&!s->startRoster().empty();});});
        const auto& room=sessions[0]->view();
        const StageData* selected=nullptr;
        for(const auto& stage:ReferenceStages())if(stage.id==room.settings.map&&stage.mode==(mode=="zones"?"zones":"turf")){selected=&stage;break;}
        check(selected!=nullptr,"Room selected an unknown stage");
        for(auto& session:sessions)scenes.push_back(std::make_unique<Scene>(*selected,*session));
        for(auto& session:sessions)session->MarkLoaded();
        wait([&]{return std::all_of(sessions.begin(),sessions.end(),[](const auto& s){return s->view().phase==RoomPhase::Match;});});
        scenes[0]->match.startPlaying();
        wait([&]{return scenes[1]->match.phase()==MatchPhase::Playing;});
    }
    double now()const{return std::chrono::duration<double>(std::chrono::steady_clock::now()-origin).count();}
    ~Fixture(){
        SteamNetworkingUtils()->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_FakePacketLag_Send,0);
        SteamNetworkingUtils()->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketReorder_Send,0);
        SteamNetworkingUtils()->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketDup_Send,0);
    }
    void step(bool tick=false){
        const double time=now();runtime.Poll();server->Update(time);
        for(auto& session:sessions){session->Update(time);if(session->view().phase==RoomPhase::Error)throw std::runtime_error(session->view().error);}
        for(auto& scene:scenes){scene->sync.Update(time,.002);if(!scene->sync.failure().empty())throw std::runtime_error(scene->sync.failure());}
        if(tick)for(auto& scene:scenes)scene->sync.Tick(1.0/60.0);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    template<class Predicate>void wait(Predicate done,bool tick=false){
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(std::chrono::steady_clock::now()<deadline){step(tick);if(done())return;}
        throw std::runtime_error("Real-GNS match condition timed out");
    }
    void run(int milliseconds,bool tick=false){const double until=now()+milliseconds/1000.0;while(now()<until)step(tick);}
};

void turfFlow(){
    Fixture f(3);auto& host=f.scenes[0]->match;auto& guest=f.scenes[1]->match;
    const auto guestNid=guest.local().networkNid(),hostNid=host.local().networkNid();
    check(guestNid==0,"Guest fixture must exercise valid nid zero");
    const auto position=guest.local().state.pos+glm::dvec3(1,0,1);
    guest.local().state.pos=position;guest.local().state.vel={0,0,0};
    f.wait([&]{return glm::length(byId(host,guestNid).state.pos-position)<.1;});
    check(glm::length(guest.local().state.pos-position)<1e-9,"Remote samples must not replace the owner's state");
    guest.world().paint(guest.local(),guest.stage().spawnPads[0],1.7,1234);
    f.wait([&]{return f.scenes[0]->paint.grid()==f.scenes[1]->paint.grid()&&f.scenes[0]->paint.counts()[0]>0;});
    const auto count=f.scenes[0]->paint.counts();f.run(200);
    check(f.scenes[0]->paint.counts()==count,"Remote replay must not keep repainting the grid");
    guest.local().state.hp=100;guest.local().state.invuln=0;byId(host,guestNid).state.invuln=0;
    host.world().applyHit(host.local(),byId(host,guestNid),25,"shooter");
    f.wait([&]{return guest.local().state.hp==75;});
    f.run(150);check(guest.local().state.hp==75,"Victim owner must apply a hit only once");

    NetworkActorEffect effect;effect.poison=1.5;effect.track=3;effect.reveal=2;effect.effectTeam=host.local().team();effect.velocityImpulse={1,0,0};
    const double vx=guest.local().state.vel.x;
    host.world().applyActorEffect(host.local(),byId(host,guestNid),effect);
    f.wait([&]{return guest.local().state.poison==1.5&&guest.local().state.track==3&&guest.local().state.reveal==2;});
    check(guest.local().state.vel.x==vx+1,"Cross-owner impulse must reach the victim owner once");
    auto& ally=f.scenes[2]->match.local();
    effect={};effect.shield=4;effect.effectTeam=guest.local().team();
    guest.world().applyActorEffect(guest.local(),byId(guest,ally.networkNid()),effect);
    f.wait([&]{return ally.state.shield==4;});

    for(auto& scene:f.scenes)scene->match.world().takeVisualEvents();
    WorldVisualEvent visual;visual.kind="device_hit";visual.source="tracer_trail";visual.pos={3,2,1};visual.team=guest.local().team();visual.sourceLocal=true;visual.victimNid=hostNid;
    guest.world().emitVisual(visual);f.run(200);
    const auto hostEvents=host.world().takeVisualEvents(),otherEvents=f.scenes[2]->match.world().takeVisualEvents();
    check(std::any_of(hostEvents.begin(),hostEvents.end(),[](const auto& e){return e.source=="tracer_trail"&&!e.sourceLocal&&e.sourceVictimLocal&&glm::length(e.pos)==0;}),"Tracer audio must identify the receiving local victim");
    check(std::any_of(otherEvents.begin(),otherEvents.end(),[](const auto& e){return e.source=="tracer_trail"&&!e.sourceLocal&&!e.sourceVictimLocal;}),"Other receivers must not inherit shooter-local audio flags");

    check(guest.local().superJumpToActor(byId(guest,ally.networkNid())),"Owner must be able to target a remote teammate");
    f.wait([&]{return byId(host,guestNid).superJumpState().has_value();});
    check(byId(host,guestNid).superJumpState()->actorTarget==&byId(host,ally.networkNid()),"Super jump snapshots must preserve the target actor for presentation");

    ShooterProjectile projectile;projectile.owner=&guest.local();projectile.pos=position+glm::dvec3(0,1,0);projectile.vel={0,0,3};projectile.kind="shot";projectile.weaponId="shooter";projectile.size=.2;projectile.radius=.1;projectile.damage=99;
    guest.world().launch(projectile);
    f.wait([&]{const auto& shots=host.world().projectiles();return std::any_of(shots.begin(),shots.end(),[](const auto& p){return p.networkRemote;});});
    const auto ghostPosition=byId(host,guestNid).state.pos;
    host.world().tick(.05);
    check(glm::length(byId(host,guestNid).state.pos-ghostPosition)<1e-9,"Remote projectile/world updates must not simulate ghost actors");
    check(guest.local().state.hp==75,"Remote projectile ghosts must not apply damage");

    const auto departedOwner=f.sessions[0]->view().selfId;
    projectile.owner=&host.local();host.world().launch(projectile);
    f.wait([&]{const auto& shots=guest.world().projectiles();return std::any_of(shots.begin(),shots.end(),[&](const auto& p){return p.networkRemote&&p.networkOwner==departedOwner;});});

    host.local().state.alive=false;host.local().state.hp=0;host.local().state.respawnTimer=4;
    f.wait([&]{return !byId(guest,hostNid).state.alive;});

    const auto owner=f.sessions[1]->view().selfId;
    f.sessions[0]->Leave();
    f.wait([&]{return f.sessions[1]->view().hostId==owner&&guest.networkAuthority();});
    check(guest.actors().size()==3,"Host departure must preserve stable actor indices");
    check(guest.bots().size()==1&&byId(guest,hostNid).botControlled(),"A departed host must become a bot on a normal stage");
    check(!byId(guest,hostNid).state.alive&&byId(guest,hostNid).state.respawnTimer>0,"Adoption must preserve a leaver's pending respawn");
    const auto& afterLeave=guest.world().projectiles();
    check(std::none_of(afterLeave.begin(),afterLeave.end(),[&](const auto& p){return p.networkRemote&&p.networkOwner==departedOwner;}),"Departed owners must not leave permanent projectile ghosts");
    guest.applyNetworkState(MatchPhase::Playing,.01,std::nullopt);
    f.wait([&]{return guest.phase()==MatchPhase::Judge;},true);
    guest.finishJudging();
    f.wait([&]{return f.scenes[2]->match.phase()==MatchPhase::Results;});
    check(f.scenes[2]->match.result()->winner==guest.result()->winner,"Survivors must receive the same final result");
    const auto code=f.sessions[1]->view().code;f.sessions[1]->ReturnToLobby();
    f.wait([&]{return f.sessions[2]->view().phase==RoomPhase::Lobby;});
    check(f.sessions[2]->view().code==code,"Result return must keep the same room");
    std::cout<<"Turf movement/paint/hit/ghost/host migration/result flow passed\n";
}

void beaconsAndFaults(){
    Fixture f(3,"turf",false,false,true);
    auto& host=f.scenes[0]->match;auto& owner=f.scenes[1]->match;auto& ally=f.scenes[2]->match;
    owner.local().setLoadout("shooter","beacon","storm");owner.world().deploy(owner.local(),"beacon");
    check(!owner.world().devices().empty(),"Beacon fixture must deploy a real device");
    const auto beaconId=owner.world().devices().front().id;
    check((beaconId>>32)==f.sessions[1]->view().selfId,"Device IDs must include their peer namespace");
    glm::dvec3 anchor;
    f.wait([&]{return ally.world().beaconAnchor(ally.local().team(),beaconId,anchor);});
    const int uses=owner.world().devices().front().uses;
    check(ally.world().consumeBeacon(ally.local(),beaconId),"A teammate must consume a remote beacon at landing");
    f.wait([&]{return owner.world().devices().front().uses==uses-1;});
    const double hp=owner.world().devices().front().hp;
    host.world().damageDevices(anchor,1.0,5,host.local().team());
    f.wait([&]{return owner.world().devices().front().hp==hp-5;});

    // Deliver deliberately repeated application messages after the live flow.
    const auto source=host.local().networkNid(),victim=owner.local().networkNid();
    owner.local().state.invuln=0;owner.local().state.hp=100;
    auto hit=[&](std::uint64_t sequence,std::uint64_t epoch,std::uint32_t attacker,bool trailing){
        Writer w;w.u32(0x4e595354).u16(WireVersion).u8(8).u64(epoch).u64(sequence);
        w.u64(777).u32(attacker).u32(victim).f64(13).string("shooter");if(trailing)w.u8(0);
        check(f.sessions[0]->SendGame(w.take()),"Fault fixture packet must enter the real transport");
    };
    const auto epoch=f.sessions[0]->view().matchEpoch;
    hit(100001,epoch-1,source,false);hit(100002,epoch,victim,false);hit(100003,epoch,source,true);f.run(400);
    check(owner.local().state.hp==100,"Stale epochs, forged owners and trailing bytes must not apply damage");
    hit(100004,epoch,source,false);hit(100005,epoch,source,false);
    f.wait([&]{return owner.local().state.hp==87;});f.run(250);
    check(owner.local().state.hp==87,"Application hit identity must deduplicate retransmitted damage");
    std::cout<<"Remote beacon/status ownership and GNS lag/reorder/duplicate/invalid-packet checks passed\n";
}

void modeMigration(const std::string& mode){
    Fixture f(3,mode);auto& host=f.scenes[0]->match;auto& guest=f.scenes[1]->match;
    if(mode=="zones"){
        auto state=host.zones()->captureAuthorityState();state.penalty={4,8};state.clock=2;state.sampleT=.1;
        check(const_cast<ZoneControl*>(host.zones())->adoptAuthorityState(state),"Cannot seed Zones authority fixture");
        f.wait([&]{return guest.zones()->penalty()[1]==8;});
    }else{
        auto state=host.boss()->captureAuthorityState();state.state.clock=8;state.state.visible=true;
        check(host.boss()->adoptAuthorityState(state),"Cannot seed Boss authority fixture");
        f.wait([&]{return guest.boss()->state().clock>=8;});
        const double hp=host.boss()->state().hp;BossSegmentHit hit;hit.shape=0;
        guest.boss()->hit(guest.local(),10,hit,"shooter");
        f.wait([&]{return host.boss()->state().hp<hp;});
    }
    f.sessions[0]->Leave();f.wait([&]{return guest.networkAuthority();});
    if(mode=="zones"){
        check(guest.zones()->penalty()[1]==8,"Zones migration lost saved penalties");
        const double clock=guest.zones()->time();f.run(100,true);check(guest.zones()->time()>clock,"Adopted Zones clock must resume");
    }else{
        const double clock=guest.boss()->state().clock;f.run(100,true);check(guest.boss()->state().clock>clock,"Adopted Boss clock must resume");
    }
    std::cout<<mode<<" authority/host migration flow passed\n";
}

void cargoAndTraffic(){
    {
        Fixture f(3,"turf",false,true);const auto count=f.scenes[0]->match.actors().size();const auto nid=f.scenes[2]->match.local().networkNid();f.sessions[2]->Leave();
        f.wait([&]{const auto& r=f.scenes[0]->match.roster();return std::any_of(r.begin(),r.end(),[&](const auto& e){return e.nid==nid&&!e.active;});});
        check(f.scenes[0]->match.actors().size()==count&&f.scenes[0]->match.bots().empty(),"Cargo departure must remove the actor without adding bots or shifting indices");
    }
    {
        Fixture f(2,"turf",true);check(f.scenes[0]->match.actors().size()==8,"Traffic fixture must contain eight actors");
        for(auto& scene:f.scenes){scene->match.local().input.fire=true;scene->match.local().input.aimPitch=-.4;}
        f.run(1600,true);
        check(f.sessions[0]->view().phase==RoomPhase::Match&&f.sessions[1]->view().phase==RoomPhase::Match,"Sustained shooting must not trigger the room rate limit");
        check(f.scenes[0]->paint.counts()[0]+f.scenes[0]->paint.counts()[1]>0,"Traffic fixture must actually paint the map");
    }
    std::cout<<"Cargo removal and eight-actor sustained traffic passed\n";
}
}
int main(){try{turfFlow();modeMigration("zones");modeMigration("boss");cargoAndTraffic();beaconsAndFaults();return 0;}
catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
