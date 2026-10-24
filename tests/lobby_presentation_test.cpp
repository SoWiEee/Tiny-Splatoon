#include "LobbyPresentation.hpp"
#include "../src/render/LobbyView.hpp"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace restoration;

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "lobby presentation: " << message << '\n';
        std::exit(1);
    }
}

LobbyLayout makeLayout() {
    LobbyLayout layout;
    layout.hubCamera.position = {0.0, 2.0, 6.0};
    layout.hubCamera.target = {0.0, 1.0, 0.0};
    layout.lobbyCamera.position = {0.0, 3.0, 8.0};
    layout.lobbyCamera.target = {0.0, 1.0, 0.0};
    layout.hubSpot.position = {-5.0, 0.0, 0.0};
    for (int i = 0; i < 8; ++i) {
        const double x = static_cast<double>(i % 4) * 1.4;
        const double z = i >= 4 ? -2.0 : 2.0;
        const glm::dvec3 spot{x, 0.0, z};
        layout.lineupSpots[static_cast<std::size_t>(i)] = {spot, 0.0};
        const glm::dvec3 start{x - 4.0, 0.0, z + 4.0};
        const glm::dvec3 bend{x - 1.5, 0.0, z + 1.0};
        layout.arrivalPaths[static_cast<std::size_t>(i)].points = {start, bend, spot};
        layout.exitPaths[static_cast<std::size_t>(i)].points = {spot, bend, start};
    }
    return layout;
}

network::Member member(network::MemberId id, int team, const char* name) {
    network::Member value;
    value.id = id;
    value.team = team;
    value.name = name;
    return value;
}

const LobbyActorState* findActor(const LobbyPresentation& presentation, network::MemberId id) {
    for (const auto& actor : presentation.actors()) if (actor.id == id) return &actor;
    return nullptr;
}

bool hasEvent(const std::vector<LobbyPresentationEvent>& events, const std::string& kind) {
    for (const auto& event : events) if (event.kind == kind) return true;
    return false;
}

void testArrivalSwitchAndLeave() {
    LobbyPresentation presentation(makeLayout());
    PlayerProfile profile;
    profile.name = "Local";
    network::RoomView view;
    view.phase = network::RoomPhase::Lobby;
    view.selfId = 100;
    view.hostId = 100;
    view.members = {member(100, 0, "Local"), member(101, 0, "Guest")};
    presentation.Update(view, profile, 0.0);
    require(!presentation.hubMode(), "lobby should use the lineup camera");
    const LobbyActorState* local = findActor(presentation, 100);
    require(local&&!local->sub.empty(),"source default sub must resolve for renderable lobby members");
    require(local && !local->visible && local->form == "swim" && local->mark == 0,
            "cold initial local roster should wait in the source pop entrance");
    require(findActor(presentation, 101) && findActor(presentation, 101)->mark == 1 &&
            findActor(presentation, 101)->visible && findActor(presentation, 101)->form == "kid",
            "second same-team member should receive the next center-out mark");
    auto events = presentation.takeEvents();
    require(hasEvent(events, "arrival"), "arrival should emit one presentation event");

    bool sawPop = false, sawLand = false;
    for (int i = 0; i < 100; ++i) {
        presentation.Update(view, profile, 0.1);
        events = presentation.takeEvents();
        sawPop = sawPop || hasEvent(events, "pop");
        sawLand = sawLand || hasEvent(events, "land");
    }
    require(sawPop && sawLand, "cold initial local roster should surface and land through the source pop sequence");
    require(findActor(presentation, 100)->mark == 0 && findActor(presentation, 100)->form == "kid",
            "landed local actor should return to the kid idle at mark zero");

    view.members.push_back(member(102, 0, "Late join"));
    presentation.Update(view, profile, 0.0);
    const auto* late = findActor(presentation, 102);
    const glm::dvec3 mouth = makeLayout().arrivalPaths[2].points.front();
    require(late && late->visible && late->form == "swim" && late->mark == 2 &&
            glm::length(glm::dvec2(late->position.x - mouth.x, late->position.z - mouth.z)) < 1e-8,
            "a real later join should swim in from its authored alley mouth");
    view.members.push_back(member(103, 0, "Second late join"));
    presentation.Update(view, profile, 0.0);
    bool sawLeap = false;
    for (int i = 0; i < 6; ++i) {
        presentation.Update(view, profile, 0.1);
        events = presentation.takeEvents();
        sawLeap = sawLeap || hasEvent(events, "leap");
    }
    late = findActor(presentation, 102);
    const auto* queued = findActor(presentation, 103);
    require(late && glm::length(glm::dvec2(late->position.x - mouth.x, late->position.z - mouth.z)) > 0.25 &&
            queued && queued->speed == 0.0 &&
            glm::length(queued->position - makeLayout().arrivalPaths[3].points.front()) < 1e-8,
            "real later joins should swim with the source 0.12-second start and 0.62-second spacing");
    bool lateLanded = false;
    for (int i = 0; i < 100; ++i) {
        presentation.Update(view, profile, 0.1);
        events = presentation.takeEvents();
        sawLeap = sawLeap || hasEvent(events, "leap");
        lateLanded = lateLanded || hasEvent(events, "land");
        require(findActor(presentation, 102) && findActor(presentation, 102)->speed <= 10.01,
                "real arrival swim speed should retain the source 10 m/s cap");
    }
    require(sawLeap && lateLanded && findActor(presentation, 102)->form == "kid",
            "real later join should complete the authored swim, leap and land sequence");

    view.members[1].team = 1;
    presentation.Update(view, profile, 0.1);
    events = presentation.takeEvents();
    require(hasEvent(events, "switch"), "team change should begin the dive and route transition");
    bool switched = false;
    for (int i = 0; i < 120; ++i) {
        presentation.Update(view, profile, 0.1);
        events = presentation.takeEvents();
        if (const auto* guest = findActor(presentation, 101); guest && guest->mark == 4 && guest->team == 1) {
            switched = true;
            break;
        }
    }
    require(switched, "team change should leave on the old exit and arrive at the new team's authored mark");

    view.members.erase(view.members.begin() + 1);
    presentation.Update(view, profile, 0.1);
    bool sawLeave = false;
    for (int i = 0; i < 120 && findActor(presentation, 101); ++i) {
        presentation.Update(view, profile, 0.1);
        events = presentation.takeEvents();
        sawLeave = sawLeave || hasEvent(events, "leave_swim") || hasEvent(events, "leave_complete");
    }
    require(sawLeave && !findActor(presentation, 101), "removed member should dive, swim out and be disposed");
}

void testHubToRoomInitialSurfacing() {
    LobbyPresentation presentation(makeLayout());
    PlayerProfile profile;
    network::RoomView view;
    view.phase = network::RoomPhase::Offline;
    view.selfId = 150;
    presentation.Update(view, profile, 0.3);
    view.phase = network::RoomPhase::Lobby;
    view.members = {member(150, 0, "Local"), member(151, 0, "Guest"), member(152, 1, "Rival")};
    presentation.Update(view, profile, 0.0);
    require(!findActor(presentation, 150)->visible && !findActor(presentation, 151)->visible &&
            !findActor(presentation, 152)->visible,
            "visible hub-to-room entry should initially hide each source pop entrance");
    for (int i = 0; i < 4; ++i) presentation.Update(view, profile, 0.1);
    require(!findActor(presentation, 150)->visible && !findActor(presentation, 151)->visible,
            "visible hub entry should keep the local pop hidden through its 0.2-second source wait");
    presentation.Update(view, profile, 0.1);
    require(findActor(presentation, 150)->visible && !findActor(presentation, 151)->visible,
            "local should pop after its source delay while remote members wait to surface");
    for (int i = 0; i < 8; ++i) presentation.Update(view, profile, 0.1);
    require(findActor(presentation, 151)->visible && !findActor(presentation, 152)->visible,
            "remote pop entrances should be spaced by the source 0.13-second slot offset");
}

void testCountdownEpochAndSourceLaunchMath() {
    LobbyPresentation presentation(makeLayout());
    PlayerProfile profile;
    network::RoomView view;
    view.phase = network::RoomPhase::Lobby;
    view.selfId = 200;
    view.members = {member(200, 0, "Local")};
    presentation.Update(view, profile, 0.0);
    for (int i = 0; i < 40; ++i) presentation.Update(view, profile, 0.1);

    view.phase = network::RoomPhase::Starting;
    view.matchEpoch = 9;
    presentation.Update(view, profile, 0.0);
    require(presentation.countdown() == -1 && presentation.launching(), "starting begins before the 0.35-second display delay");
    presentation.Update(view, profile, 0.35);
    require(presentation.countdown() == 3, "countdown shows 3 at source time 0.35");
    presentation.Update(view, profile, 0.85);
    require(presentation.countdown() == 2, "countdown shows 2 at source time 1.20");
    presentation.Update(view, profile, 0.85);
    require(presentation.countdown() == 1, "countdown shows 1 at source time 2.05");
    presentation.Update(view, profile, 0.85);
    require(presentation.countdown() == 0 && !presentation.launchComplete(), "GO fires at 2.90 before the handoff delay");
    auto events = presentation.takeEvents();
    require(hasEvent(events, "countdown_3") && hasEvent(events, "countdown_2") &&
            hasEvent(events, "countdown_1") && hasEvent(events, "go"),
            "countdown should emit each source boundary exactly once");

    // Source jump-out: after its 0.13 s anticipation, y = 7*x + 26*x*x and vy = 7 + 52*x.
    presentation.Update(view, profile, 0.1);
    require(findActor(presentation,200)->velocityY==0,"source anticipation must last 0.13 seconds after GO");
    presentation.Update(view, profile, 0.1);
    const auto* actor = findActor(presentation, 200);
    require(actor && actor->pitch < -1.5 && std::abs(actor->velocityY - (7.0 + 52.0 * 0.07)) < 1e-6,
            "super-jump takeoff should match the source vertical trajectory");
    require(std::abs(actor->position.y - (7.0 * 0.07 + 26.0 * 0.07 * 0.07)) < 1e-6,
            "super-jump height should match the source parabola");

    view.matchEpoch = 9; // repeated Starting snapshots in one epoch must not restart the clock.
    presentation.Update(view, profile, 0.64);
    require(!presentation.launchComplete(), "single actor handoff has not elapsed at GO + 0.84 s");
    presentation.Update(view, profile, 0.01);
    require(presentation.launchComplete() && !presentation.launching(), "single actor handoff completes at GO + 0.85 s");

    LobbyPresentation cancelled(makeLayout());
    network::RoomView cancelView;
    cancelView.phase = network::RoomPhase::Lobby;
    cancelView.selfId = 300;
    cancelView.members = {member(300, 0, "Local")};
    cancelled.Update(cancelView, profile, 0.0);
    cancelView.phase = network::RoomPhase::Starting;
    cancelView.matchEpoch = 10;
    cancelled.Update(cancelView, profile, 0.2);
    cancelView.phase = network::RoomPhase::Lobby;
    cancelled.Update(cancelView, profile, 0.1);
    require(cancelled.countdown() == -1 && !cancelled.launching(), "pre-GO return to lobby cancels the launch countdown");
    events = cancelled.takeEvents();
    require(hasEvent(events, "countdown_cancel") && !hasEvent(events, "go"), "cancel should not emit launch");
}

void testReducedMotionAndBossMarks() {
    LobbyPresentation presentation(makeLayout());
    PlayerProfile profile;
    profile.settings.reducedMotion = true;
    network::RoomView view;
    view.phase = network::RoomPhase::Lobby;
    view.selfId = 400;
    view.settings.mode = "boss";
    view.members = {member(400, 0, "Local"), member(401, 1, "One"), member(402, 1, "Two"),
                    member(403, 1, "Three"), member(404, 1, "Four"), member(405, 1, "Five"),
                    member(406, 1, "Six"), member(407, 1, "Seven")};
    presentation.Update(view, profile, 0.0);
    require(findActor(presentation, 400)->mark == 0 && findActor(presentation, 401)->mark == 1 &&
            findActor(presentation, 402)->mark == 2 && findActor(presentation, 407)->mark == 7,
            "boss mode should assign all participants marks in one row");
    require(findActor(presentation, 400)->team == findActor(presentation, 402)->team,
            "boss mode should keep every actor on the same presentation color");
    const auto before = findActor(presentation, 401)->position;
    view.phase = network::RoomPhase::Starting;
    view.matchEpoch = 11;
    presentation.Update(view, profile, 1.0);
    require(glm::length(findActor(presentation, 401)->position - before) < 1e-9,
            "reduced motion keeps lineup actors stable through the countdown");
    presentation.Update(view, profile, 1.91);
    require(presentation.countdown() == 0 && !findActor(presentation, 401)->visible,
            "reduced motion keeps the countdown and hides actors on GO");
    presentation.Update(view, profile, 1.33);
    require(!presentation.launchComplete(), "eight-member handoff remains pending at GO + 1.33 s");
    presentation.Update(view, profile, 0.01);
    require(presentation.launchComplete(), "reduced motion preserves the source launch handoff duration");
}

void testMemberEventsAndNativeFrameRates() {
    LobbyPresentation emotes(makeLayout());
    PlayerProfile profile;
    network::RoomView view;
    view.phase = network::RoomPhase::Lobby;
    view.selfId = 500;
    view.members = {member(500, 0, "Local"), member(501, 0, "Guest")};
    view.members[1].emote = "booyah";
    view.members[1].emoteSerial = 0; // A cached snapshot must not replay a stale emote.
    emotes.Update(view, profile, 0.0);
    auto events = emotes.takeEvents();
    require(!hasEvent(events, "emote_booyah"), "serial zero must not replay a stale member emote");
    for(int i=0;i<80;++i)emotes.Update(view,profile,.1);
    emotes.takeEvents();
    view.members[1].emoteSerial = 1;
    emotes.Update(view, profile, 0.0);
    events = emotes.takeEvents();
    require(hasEvent(events, "emote_booyah") && findActor(emotes, 501)->dance == "victory",
            "a new member emote serial should trigger the source victory pose and one-shot");
    emotes.Update(view, profile, 0.0);
    require(!hasEvent(emotes.takeEvents(), "emote_booyah"), "unchanged emote serial should not replay");
    view.members[1].emoteSerial = 2;
    view.members[1].main = "charger";
    view.members[1].sub = "splat_bomb";
    view.members[1].style.hair = 3;
    view.members[1].ready = true;
    emotes.Update(view, profile, 0.0);
    events = emotes.takeEvents();
    require(hasEvent(events, "emote_booyah") && hasEvent(events, "weapon") && hasEvent(events, "style_pop") &&
            hasEvent(events, "ready"),
            "GNS member updates should refresh repeated emotes, weapon/style and ready presentation");

    network::RoomView readyRoom;readyRoom.phase=network::RoomPhase::Lobby;readyRoom.selfId=700;
    readyRoom.members={member(700,0,"Local")};LobbyPresentation readyPose(makeLayout());
    for(int i=0;i<80;++i)readyPose.Update(readyRoom,profile,.1);
    readyRoom.members[0].ready=true;readyPose.Update(readyRoom,profile,0);
    readyPose.Update(readyRoom,profile,.09);readyPose.Update(readyRoom,profile,.09);
    require(std::abs(findActor(readyPose,700)->position.y-makeLayout().lineupSpots[0].position.y-.2)<1e-8,
            "ready pose must perform the source 0.2-meter hop at 0.18 seconds");
    for(int i=0;i<16;++i)readyPose.Update(readyRoom,profile,.1);
    require(findActor(readyPose,700)->dance=="lobby_pose","ready celebration must return to the source lineup pose");

    for (int fps : {15, 60}) {
        LobbyPresentation presentation(makeLayout());
        network::RoomView room;
        room.phase = network::RoomPhase::Lobby;
        room.selfId = static_cast<network::MemberId>(600 + fps);
        room.members = {member(room.selfId, 0, "Local")};
        presentation.Update(room, profile, 0.0);
        bool landed = false;
        auto frameEvents = presentation.takeEvents();
        for (int frame = 0; frame < fps * 5; ++frame) {
            presentation.Update(room, profile, 1.0 / fps);
            frameEvents = presentation.takeEvents();
            landed = landed || hasEvent(frameEvents, "land");
            const auto* actor = findActor(presentation, room.selfId);
            require(actor && actor->speed <= 10.01, "source swim acceleration stays capped at 10 m/s at 15/60 Hz");
        }
        require(landed && findActor(presentation, room.selfId)->form == "kid",
                "source arrival completes at both native frame rates");
        room.phase = network::RoomPhase::Starting;
        room.matchEpoch = static_cast<std::uint64_t>(fps);
        presentation.Update(room, profile, 0.0);
        int goCount = 0;
        const double frame = 1.0 / fps;
        for (int i = 0; i < fps * 3 + 2; ++i) {
            presentation.Update(room, profile, frame);
            frameEvents = presentation.takeEvents();
            for (const auto& event : frameEvents) if (event.kind == "go") ++goCount;
        }
        require(presentation.countdown() == 0 && goCount == 1,
                "countdown crosses GO once at 15/60 Hz");
        for (int i = 0; i < fps; ++i) presentation.Update(room, profile, frame);
        require(presentation.launchComplete(), "one-player launch handoff finishes within one second at 15/60 Hz");
    }
}

void testFirstObservedMatchAndReturnToLobby(network::RoomPhase initialPhase) {
    LobbyPresentation presentation(makeLayout());
    PlayerProfile profile;
    network::RoomView view;
    view.phase = initialPhase;
    view.matchEpoch = 77;
    view.selfId = 800;
    view.members = {member(800, 0, "Local"), member(801, 0, "Ally"), member(802, 1, "Rival")};
    presentation.Update(view, profile, 0.0);
    require(findActor(presentation, 800) && !findActor(presentation, 800)->visible &&
            findActor(presentation, 801) && findActor(presentation, 801)->visible &&
            findActor(presentation, 801)->form == "kid",
            "cold first-observed match should pop the local actor while remote members stand in place");
    require(!presentation.hubMode() && presentation.countdown() == -1 && presentation.launching(),
            "first observed Match should construct the room lineup and start its countdown");
    bool went = false;
    for (int i = 0; i < 30; ++i) {
        presentation.Update(view, profile, 0.1);
        const auto events = presentation.takeEvents();
        went = went || hasEvent(events, "go");
    }
    require(went && presentation.countdown() == 0, "first observed Match should fire the same GO sequence");
    presentation.Update(view, profile, 0.0);
    require(presentation.countdown() == 0, "repeated Match snapshots in one epoch must not restart the countdown");
    presentation.Update(view, profile, 1.4);
    require(presentation.launchComplete(), "first-observed Match launch should reach the normal handoff");

    view.phase = network::RoomPhase::Lobby;
    presentation.Update(view, profile, 0.0);
    require(presentation.countdown() == -1 && !presentation.launching() && !presentation.launchComplete(),
            "return to Lobby should clear the completed launch state");
    for (auto id : {800u, 801u, 802u}) {
        const auto* actor = findActor(presentation, id);
        require(actor && actor->visible && actor->form == "kid",
                "return to Lobby should restore each launched actor on its lineup mark");
    }
    require(hasEvent(presentation.takeEvents(), "match_return"), "return to Lobby should announce actor restoration");
    view.phase=network::RoomPhase::Starting;view.code="NEW12";
    presentation.Update(view,profile,0.0);
    require(presentation.launching()&&!presentation.launchComplete()&&presentation.countdown()==-1,
            "another room reusing an epoch must receive a fresh countdown");
    view.phase=network::RoomPhase::Offline;view.code.clear();
    presentation.Update(view,profile,0.0);
    require(!presentation.launching()&&!presentation.launchComplete(),"disconnect must clear pending launch state");
}
void testRealDisconnectReturnsLocalThroughInk(){
    const auto layout=makeLayout();LobbyPresentation presentation(layout);PlayerProfile profile;
    network::RoomView room;room.phase=network::RoomPhase::Lobby;room.selfId=950;room.code="ABCDE";
    room.members={member(950,0,"Local")};
    for(int i=0;i<80;++i)presentation.Update(room,profile,.1);
    const auto before=findActor(presentation,950)->position;
    presentation.Update({},profile,0);
    const auto* local=findActor(presentation,0);
    require(local&&glm::distance(local->position,before)<1e-9,"clearing RoomView must preserve the leaving local actor instead of teleporting it to the hub");
    bool swam=false;
    for(int i=0;i<100;++i){presentation.Update({},profile,.1);local=findActor(presentation,0);swam=swam||(local&&local->form=="swim");}
    require(swam&&local&&glm::distance(local->position,layout.hubSpot.position)<1e-8,"local actor must dive and swim back to the source hub mark");
}

void testBossRowColorsDuringModeChange(){
    LobbyPresentation presentation(makeLayout());PlayerProfile profile;
    network::RoomView room;room.phase=network::RoomPhase::Lobby;room.selfId=900;
    for(int i=0;i<8;++i)room.members.push_back(member(900+i,i%2,"Member"));
    for(int i=0;i<100;++i)presentation.Update(room,profile,.1);
    room.settings.mode="boss";
    for(int i=0;i<100;++i){
        presentation.Update(room,profile,.1);
        for(const auto& actor:presentation.actors())require(actor.team==0,"all Boss squad rows must keep the same ink during team-transition swimming");
    }
}
} // namespace

int main() {
    testArrivalSwitchAndLeave();
    testBossRowColorsDuringModeChange();
    testRealDisconnectReturnsLocalThroughInk();
    testHubToRoomInitialSurfacing();
    testCountdownEpochAndSourceLaunchMath();
    testReducedMotionAndBossMarks();
    testMemberEventsAndNativeFrameRates();
    testFirstObservedMatchAndReturnToLobby(network::RoomPhase::Match);
    testFirstObservedMatchAndReturnToLobby(network::RoomPhase::Starting);
    const auto layout=makeLayout();LobbyCameraMotion camera;
    camera.Update(layout,true,1280,720,0,0,false,-1,false);
    const auto halfway=camera.Update(layout,false,1280,720,.85,.85,false,-1,false);
    require(glm::length(halfway.eye-glm::mix(layout.hubCamera.position,layout.lobbyCamera.position,.5))<1e-8,
            "source camera glide must reach its midpoint at 0.85 seconds");
    const auto reduced=camera.Update(layout,true,1280,720,0,.85,false,-1,true);
    require(glm::length(reduced.eye-layout.hubCamera.position)<1e-8,"reduced motion must snap to the authored camera");
    std::cout << "Lobby presentation source choreography passed.\n";
}
