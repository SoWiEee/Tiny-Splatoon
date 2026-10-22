#include "../src/game/BossSimulation.hpp"
#include "../src/game/CombatWorld.hpp"
#include "../src/world/FacePaint.hpp"
#include "../src/game/PlayerSimulation.hpp"
#include "../src/game/ZoneControl.hpp"
#include "../src/network/AuthorityState.hpp"
#include "../src/network/Wire.hpp"
#include "../src/world/ReferenceData.hpp"
#include "../src/world/StageGeometry.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace restoration;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

const StageData& stageFor(std::string_view mode, bool onlineOnly) {
    const auto& stages = ReferenceStages();
    const auto found = std::find_if(stages.begin(), stages.end(), [&](const StageData& stage) {
        return stage.mode == mode && stage.onlineOnly == onlineOnly && !stage.spawnPads.empty();
    });
    if (found == stages.end()) throw std::runtime_error("Missing test stage");
    return *found;
}

void testCodecsRejectMalformedPacketsAtomically() {
    using namespace restoration::network;
    BossAuthorityState boss;
    boss.state.hp = boss.state.maxHp = 100.0;
    boss.state.clock = 4.0;
    boss.state.visible = true;
    boss.hitGuards = {"3.000000|impact|0|0"};
    BossCrablet burstCrab; burstCrab.id=4; burstCrab.dead=true; burstCrab.burst=true;
    boss.crablets.push_back(burstCrab);
    const auto bossBytes = EncodeBossAuthorityState(boss);
    BossAuthorityState decodedBoss;
    require(DecodeBossAuthorityState(bossBytes, decodedBoss), "valid Boss authority state decodes");
    require(decodedBoss.state.hp == 100.0 && decodedBoss.hitGuards == boss.hitGuards &&
                decodedBoss.crablets.size()==1 && decodedBoss.crablets[0].burst,
            "Boss HP, hit guards, and crablet burst markers round-trip");
    auto invalidBurst = boss; invalidBurst.crablets[0].dead=false;
    bool invalidBurstRejected = false;
    try { (void)EncodeBossAuthorityState(invalidBurst); } catch (const WireError&) { invalidBurstRejected = true; }
    require(invalidBurstRejected, "Boss codec rejects a live crablet marked as already burst");
    auto malformedBoss = bossBytes;
    malformedBoss.push_back(0);
    decodedBoss.state.hp = 37.0;
    require(!DecodeBossAuthorityState(malformedBoss, decodedBoss), "Boss decoder rejects trailing bytes");
    require(decodedBoss.state.hp == 37.0, "failed Boss decode leaves the destination unchanged");

    ZoneAuthorityState zones;
    zones.zones.push_back({0});
    const auto zoneBytes = EncodeZonesAuthorityState(zones);
    ZoneAuthorityState decodedZones;
    require(DecodeZonesAuthorityState(zoneBytes, decodedZones), "valid Zones authority state decodes");
    auto malformedZones = zoneBytes;
    malformedZones.push_back(0);
    decodedZones.clock = 17.0;
    require(!DecodeZonesAuthorityState(malformedZones, decodedZones), "Zones decoder rejects trailing bytes");
    require(decodedZones.clock == 17.0, "failed Zones decode leaves the destination unchanged");

    bool bossBounded = false, zonesBounded = false;
    boss.hitGuards.assign(MaxAuthorityHitGuards + 1, "guard");
    try { (void)EncodeBossAuthorityState(boss); } catch (const WireError&) { bossBounded = true; }
    zones.penaltyLog.resize(513);
    try { (void)EncodeZonesAuthorityState(zones); } catch (const WireError&) { zonesBounded = true; }
    require(bossBounded && zonesBounded, "authority encoders reject out-of-bound collections");
    zones.penaltyLog.clear(); zones.nextSwap = -2.0;
    bool negativeSwapRejected = false;
    try { (void)EncodeZonesAuthorityState(zones); } catch (const WireError&) { negativeSwapRejected = true; }
    require(negativeSwapRejected, "Zones codec rejects negative finite swap timers while reserving -1 only on the wire");

    decodedBoss.state.hp = 37.0;
    std::vector<std::uint8_t> oversized(MaxWirePacketBytes + 1);
    require(!DecodeBossAuthorityState(oversized, decodedBoss), "Boss decoder rejects oversized packets");
    require(decodedBoss.state.hp == 37.0, "oversized Boss decode leaves the destination unchanged");
}

void testBossFollowerAndAdoption() {
    const auto& stage = stageFor("turf", false);
    StageGeometry geometry(stage);
    FacePaint paint(stage, geometry);
    CombatWorld world(stage, geometry, paint);
    PlayerSimulation actor(stage, geometry, paint, 0, &world);
    std::vector<PlayerSimulation*> squad{&actor};
    BossSimulation follower(stage, geometry, paint, squad, "normal", 91);

    auto snapshot = follower.captureAuthorityState();
    snapshot.state.clock = 5.0;
    snapshot.state.visible = true;
    snapshot.state.hp = snapshot.state.maxHp - 25.0;
    BossMove move;
    move.id = BossMoveId::Crablets; move.t0 = 4.0; move.tele = .9; move.act = .8; move.rec = .6;
    move.seed = 7; move.count = 3;
    snapshot.move = move;
    snapshot.moves = {move};
    BossCrablet crab;
    crab.id = 0; crab.target = -1; crab.pos = snapshot.state.pos; crab.age = 1.0;
    snapshot.crablets = {crab};

    require(follower.applyAuthorityState(snapshot), "Boss follower accepts a valid snapshot");
    const auto hp = follower.state().hp;
    const auto paintCounts = paint.counts();
    const auto actorHp = actor.state.hp;
    follower.tick(.05, true);
    BossSegmentHit weakHit; weakHit.weak = true;
    require(follower.hit(actor, 20.0, weakHit, "shooter") == 0.0, "follower does not apply Boss damage locally");
    require(follower.state().hp == hp && follower.crablets().size() == 1 && paint.counts() == paintCounts &&
                actor.state.hp == actorHp,
            "same-clock follower and forwarded Boss hit do not replay combat or paint effects");

    bool forwardedHit = false, forwardedSplash = false;
    follower.setHitForwarder([&](PlayerSimulation&, double, const BossSegmentHit&, const std::string&) { forwardedHit = true; });
    follower.setSplashForwarder([&](PlayerSimulation&, glm::dvec3, double, double, double, const std::string&) { forwardedSplash = true; });
    follower.hit(actor, 20.0, weakHit, "shooter");
    follower.splash(actor, snapshot.state.pos, 3.0, 40.0, 10.0, "bomb");
    require(forwardedHit && forwardedSplash, "follower routes direct and splash attacks to the authority");

    require(follower.adoptAuthorityState(snapshot), "new Boss host adopts a valid snapshot");
    require(follower.state().hp == hp && follower.move() && follower.move()->id == BossMoveId::Crablets &&
                follower.crablets().size() == 1,
            "Boss adoption preserves HP, the current move, and existing crablets");
    follower.tick(.05, true);
    require(follower.crablets().size() == 1, "adoption does not spawn the current move's crablets twice");

    auto invalid = snapshot;
    invalid.state.target = 1;
    require(!follower.applyAuthorityState(invalid) && follower.state().hp == hp,
            "Boss apply rejects references beyond the local squad without mutation");
}

void testBossVictimOwnedHazards() {
    const auto& stage = stageFor("turf", false);
    StageGeometry geometry(stage);
    FacePaint paint(stage, geometry);
    CombatWorld world(stage, geometry, paint);
    PlayerSimulation local(stage, geometry, paint, 0, &world);
    PlayerSimulation remote(stage, geometry, paint, 0, &world);
    PlayerSimulation hostBot(stage, geometry, paint, 0, &world);
    local.setNetworkIdentity(0, 7); remote.setNetworkIdentity(1, 8); hostBot.setNetworkIdentity(2, 7);
    hostBot.setBotControlled(true);
    std::vector<PlayerSimulation*> squad{&local, &remote, &hostBot};
    BossSimulation boss(stage, geometry, paint, squad, "normal", 92);
    boss.setNetworkLocalPeer(7);

    glm::dvec3 target{};
    bool foundTarget=false;
    for(const int cell:boss.nav().planIds()){
        const auto [x,z]=boss.nav().xz(cell);
        if(boss.nav().inPad(x,z,1.0))continue;
        target={x,boss.nav().floorAt(x,z),z};foundTarget=true;break;
    }
    require(foundTarget,"Boss test stage has a victim position outside its safe pads");
    local.state.pos=remote.state.pos=hostBot.state.pos=target;
    local.state.invuln=remote.state.invuln=hostBot.state.invuln=0;

    auto state=boss.captureAuthorityState();
    state.state.clock=4.8;state.state.visible=true;
    BossMove slam;slam.id=BossMoveId::Slam;slam.t0=4.0;slam.tele=.9;slam.act=1.9;slam.rec=1.0;
    slam.point=target;slam.origin=target;state.move=slam;state.moves={slam};
    const auto localHp=local.state.hp,remoteHp=remote.state.hp,botHp=hostBot.state.hp;

    BossSimulation follower(stage, geometry, paint, squad, "normal", 93);
    follower.setNetworkLocalPeer(7);
    require(follower.applyAuthorityState(state),"Boss follower accepts the initial hazard timeline");
    follower.tick(.05,true);
    require(local.state.hp==localHp&&remote.state.hp==remoteHp&&hostBot.state.hp==botHp,
            "a follower baselines its hazard clock without replaying old attacks");
    auto next=state;next.state.clock=5.0;
    require(follower.applyAuthorityState(next),"Boss follower accepts a newer hazard clock");
    follower.tick(.1,true);
    follower.tick(.1,true);
    const auto localAfterSlam=local.state.hp,botAfterSlam=hostBot.state.hp;
    require(localAfterSlam<localHp&&botAfterSlam<botHp&&remote.state.hp==remoteHp,
            "Boss impact damages only locally owned players and host-owned bots");

    auto burst=next;
    BossCrablet crab;crab.id=22;crab.pos=target;crab.dead=true;crab.burst=true;
    burst.crablets={crab};
    require(follower.applyAuthorityState(burst),"Boss follower accepts the replicated crablet burst");
    const auto localAfterBurst=local.state.hp,botAfterBurst=hostBot.state.hp;
    require(localAfterBurst<localAfterSlam&&botAfterBurst<botAfterSlam&&remote.state.hp==remoteHp,
            "replicated crablet bursts damage local victims without painting remote replicas");
    require(follower.applyAuthorityState(burst),"repeated Boss snapshot is accepted");
    require(local.state.hp==localAfterBurst&&hostBot.state.hp==botAfterBurst,
            "repeated authority snapshots do not replay a crablet burst");
    require(follower.adoptAuthorityState(burst),"migrating Boss host adopts follower hazard guards");
    const auto adopted=follower.captureAuthorityState();
    require(std::find(adopted.hitGuards.begin(),adopted.hitGuards.end(),"4.000000|impact|0|0")!=adopted.hitGuards.end()&&
                local.state.hp==localAfterBurst&&hostBot.state.hp==botAfterBurst,
            "host migration carries local once-hit guards without replaying prior damage");

    BossSimulation host(stage, geometry, paint, squad, "normal", 94);
    host.setNetworkLocalPeer(7);
    require(host.applyAuthorityState(state),"Boss host fixture accepts its authority state");
    host.setNetworkAuthority(true);
    host.tick(.1,true);
    require(local.state.hp<localHp&&hostBot.state.hp<botHp&&remote.state.hp==remoteHp,
            "Boss authority damages its owned players and bots but never remote-owned actors");
}

void testZonesFollowerAdoptionAndFinish() {
    const auto& stage = stageFor("zones", false);
    StageGeometry geometry(stage);
    FacePaint paint(stage, geometry);
    ZoneControl shortMatch(stage, geometry, paint, 20.0, [] { return .5; });
    require(!shortMatch.timeUp() && shortMatch.overtime(),
            "a tied final-lock match enters overtime before the authority snapshot");
    const auto finalLock = shortMatch.captureAuthorityState();
    require(finalLock.final && finalLock.overtime && std::isinf(finalLock.nextSwap),
            "captured final/overtime Zones state preserves the infinite no-swap timer");
    const auto finalLockBytes = network::EncodeZonesAuthorityState(finalLock);
    network::ZoneAuthorityState decodedFinalLock;
    require(network::DecodeZonesAuthorityState(finalLockBytes, decodedFinalLock) &&
                std::isinf(decodedFinalLock.nextSwap),
            "Zones codec round-trips the final-lock infinite timer through its finite sentinel");

    ZoneControl source(stage, geometry, paint, 300.0, [] { return .5; });
    auto snapshot = source.captureAuthorityState();
    snapshot.clock = 2.0; snapshot.sampleT = .1; snapshot.neutralT = 2.0;
    snapshot.penalty = {4.0, 8.0}; snapshot.start = {70.0, 75.0}; snapshot.tieEnd[0] = 42.0;
    snapshot.overtime = true; snapshot.overtimeT = 5.0; snapshot.overtimeLosing = 0;
    snapshot.penaltyLog.push_back({2.0, 70.0, 60.0, 4.0, 4.0, 0});
    snapshot.zones[0].flood = ZoneFloodState{0, 2.0, .6, 100.0, true};
    snapshot.floods.push_back({0, 0, 0, 0.0, .6, 100.0, true});

    FacePaint followerPaint(stage, geometry);
    ZoneControl follower(stage, geometry, followerPaint, 300.0, [] { return .5; });
    require(follower.applyAuthorityState(snapshot), "Zones follower accepts a valid snapshot");
    const auto before = followerPaint.counts();
    follower.update(.1, {});
    require(follower.time() == 2.0 && followerPaint.counts() == before && follower.penaltyLog().size() == 1 &&
                follower.overtime() && follower.overtimeTime() == 5.0,
            "Zones follower preserves penalty/overtime state without advancing or painting locally");

    auto invalid = snapshot;
    invalid.floods[0].next = follower.zones()[0].region.cells.size() + 1;
    require(!follower.applyAuthorityState(invalid) && follower.time() == 2.0,
            "Zones apply rejects an invalid flood cursor without partial mutation");

    FacePaint hostPaint(stage, geometry);
    ZoneControl host(stage, geometry, hostPaint, 300.0, [] { return .5; });
    require(host.adoptAuthorityState(snapshot), "new Zones host adopts a valid snapshot");
    host.update(.1, {});
    require(host.time() > snapshot.clock && hostPaint.counts()[0] > 0,
            "adopted Zones host advances the saved flood cursor and continues the match");

    auto finished = snapshot;
    finished.winner = 1; finished.reason = "retake";
    FacePaint finishedPaint(stage, geometry);
    ZoneControl completed(stage, geometry, finishedPaint, 300.0, [] { return .5; });
    require(completed.adoptAuthorityState(finished), "new Zones host adopts the finished result");
    const auto eventCount = completed.events().size();
    require(completed.ended() && completed.timeUp(), "an adopted finished match remains finished");
    completed.update(.1, {});
    require(completed.events().size() == eventCount && completed.reason() == "retake",
            "an adopted result is not emitted or completed a second time");
}

void testOnlineOnlyBossStageConstruction() {
    const auto& stage = stageFor("turf", true);
    StageGeometry geometry(stage);
    FacePaint paint(stage, geometry);
    CombatWorld world(stage, geometry, paint);
    PlayerSimulation actor(stage, geometry, paint, 0, &world);
    std::vector<PlayerSimulation*> squad{&actor};
    BossSimulation boss(stage, geometry, paint, squad, "normal", 5);
    require(boss.state().maxHp > 0.0, "online-only Turf stage supports network Boss initialization");
}

} // namespace

int main() {
    try {
        testCodecsRejectMalformedPacketsAtomically();
        testBossFollowerAndAdoption();
        testBossVictimOwnedHazards();
        testZonesFollowerAdoptionAndFinish();
        testOnlineOnlyBossStageConstruction();
        std::cout << "Network authority codec, Boss, and Zones migration checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
