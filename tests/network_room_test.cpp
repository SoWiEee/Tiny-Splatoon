#include "../src/network/RoomServer.hpp"
#include "../src/network/RoomSession.hpp"
#include "../src/network/RoomProtocol.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using namespace restoration::network;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

SteamNetworkingIPAddr loopback(std::uint16_t port) {
    SteamNetworkingIPAddr address{};
    address.SetIPv4(0x7f000001u, port);
    return address;
}

Member player(std::string name) {
    Member result;
    result.name = std::move(name);
    result.main = "shooter";
    result.style = {1, 2, 3, 4, 0, 1};
    return result;
}

template<class Predicate>
bool pump(GnsRuntime& runtime, RoomServer* server, const std::vector<RoomSession*>& clients,
          Predicate done, std::chrono::milliseconds timeout = std::chrono::seconds(8)) {
    static const auto origin = std::chrono::steady_clock::now();
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        runtime.Poll();
        const double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - origin).count();
        if (server) server->Update(now);
        for (auto* client : clients) client->Update(now);
        if (done()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

std::unique_ptr<RoomServer> startLocalServer(GnsRuntime& runtime, SteamNetworkingIPAddr& address) {
    for (std::uint16_t port = 42000; port < 52000; ++port) {
        address = loopback(port);
        try { return std::make_unique<RoomServer>(runtime, address); }
        catch (const std::exception&) {}
    }
    throw std::runtime_error("could not find a loopback port for the room service");
}

SteamNetworkingIPAddr parseServer(const std::string& text) {
    SteamNetworkingIPAddr address{};
    if (!address.ParseString(text.c_str()) || !address.m_port)
        throw std::invalid_argument("--server must be a numeric IP and nonzero port");
    return address;
}

void testTwoPlayerFlow(GnsRuntime& runtime, RoomServer* server, const SteamNetworkingIPAddr& address) {
    RoomSession host(runtime), guest(runtime);
    host.Create(address, player("Host"));
    require(pump(runtime, server, {&host}, [&] { return host.view().phase == RoomPhase::Lobby; }),
            "room creator must enter the lobby");
    const auto code = host.view().code;
    guest.Join(address, code, player("Guest"));
    require(pump(runtime, server, {&host, &guest}, [&] {
        return host.view().members.size() == 2 && guest.view().members.size() == 2;
    }), "second player must join the room");
    const auto hostId = host.view().selfId;
    const auto guestId = guest.view().selfId;
    require(hostId && guestId && hostId != guestId && host.view().hostId == hostId && guest.view().hostId == hostId,
            "the oldest member must be elected host with stable ids");

    RoomSettings settings;
    settings.map = "tidewater"; settings.mode = "zones"; settings.duration = 60;
    host.SetSettings(settings);
    require(pump(runtime, server, {&host, &guest}, [&] {
        return host.view().settings.mode == "zones" && host.view().settings.duration == 300;
    }), "Zone Control must use its fixed five minute duration");
    settings.map = "cargo"; settings.mode = "boss";
    host.SetSettings(settings);
    require(pump(runtime, server, {&host, &guest}, [&] {
        return host.view().settings.mode == "boss" && host.view().settings.map != "cargo";
    }), "Cargo must fall back when Boss Battle is selected");
    settings.map = "cargo"; settings.mode = "turf"; settings.duration = 180; settings.bots = true;
    host.SetSettings(settings);
    require(pump(runtime, server, {&host, &guest}, [&] {
        return host.view().settings.map == "cargo" && !host.view().settings.bots;
    }), "Cargo must disable bot filling");
    require(host.view().members.size() == 2 && host.view().members[0].team != host.view().members[1].team,
            "automatic teams must balance two players");

    guest.SetReady(true);
    require(pump(runtime, server, {&host, &guest}, [&] { return guest.view().members[1].ready; }),
            "guest ready state must reach the lobby");
    require(host.StartMatch(), "host must be able to request a start");
    require(pump(runtime, server, {&host, &guest}, [&] {
        return host.view().phase == RoomPhase::Starting && guest.view().phase == RoomPhase::Starting &&
            host.startRoster().size() == 2 && guest.startRoster().size() == 2;
    }), "both clients must receive the resolved roster");
    host.MarkLoaded();
    require(!pump(runtime, server, {&host, &guest}, [&] {
        return host.view().phase == RoomPhase::Match || guest.view().phase == RoomPhase::Match;
    }, std::chrono::milliseconds(150)), "GO must wait for every connected human to load");
    guest.MarkLoaded();
    require(pump(runtime, server, {&host, &guest}, [&] {
        return host.view().phase == RoomPhase::Match && guest.view().phase == RoomPhase::Match;
    }), "the room must send GO after the load barrier completes");
    const auto roomCode = host.view().code;
    const auto firstEpoch = host.view().matchEpoch;
    const auto firstSeed = host.view().seed;
    require(firstEpoch != 0, "starting a match must assign an epoch");

    guest.ReturnToLobby();
    require(pump(runtime, server, {&host, &guest}, [&] {
        return host.view().phase == RoomPhase::Match && guest.view().phase == RoomPhase::Match &&
            guest.view().error == "只有房主可以結束對戰";
    }), "a guest return request must not end the shared match");

    require(guest.SendGame({0x31, 0x42, 0x53}, hostId), "game packet must route to a room member id");
    std::vector<GamePacket> received;
    require(pump(runtime, server, {&host, &guest}, [&] {
        auto batch = host.TakeGamePackets();
        received.insert(received.end(), std::make_move_iterator(batch.begin()), std::make_move_iterator(batch.end()));
        return !received.empty();
    }), "targeted game packet must arrive");
    require(received.front().from == guestId && received.front().payload == std::vector<std::uint8_t>({0x31, 0x42, 0x53}),
            "the server must attach the proven sender id to opaque game data");

    host.ReturnToLobby();
    require(pump(runtime, server, {&host, &guest}, [&] {
        return host.view().phase == RoomPhase::Lobby && guest.view().phase == RoomPhase::Lobby &&
            host.view().code == roomCode && guest.view().code == roomCode &&
            host.view().matchEpoch == firstEpoch && guest.view().matchEpoch == firstEpoch &&
            host.view().seed == firstSeed && guest.view().seed == firstSeed &&
            host.startRoster().empty() && guest.startRoster().empty() && !guest.view().members[1].ready;
    }), "returning must preserve room identity and completed epoch while clearing ready states");

    guest.SetReady(true);
    require(pump(runtime, server, {&host, &guest}, [&] { return host.view().members[1].ready; }),
            "the guest can ready up for a second Cargo match");
    require(host.StartMatch(), "host can start the second Cargo match");
    require(pump(runtime, server, {&host, &guest}, [&] {
        return host.view().phase == RoomPhase::Starting && guest.view().phase == RoomPhase::Starting;
    }), "both clients must enter loading for the second Cargo match");
    host.MarkLoaded(); guest.MarkLoaded();
    require(pump(runtime, server, {&host, &guest}, [&] {
        return host.view().phase == RoomPhase::Match && guest.view().phase == RoomPhase::Match;
    }), "the second Cargo match must reach GO");
    require(host.view().code == roomCode && host.view().matchEpoch != 0 && host.view().matchEpoch != firstEpoch,
            "restarting in the same room must allocate a fresh match epoch");
    const auto hostNid = host.startRoster().front().nid;
    guest.Leave();
    require(pump(runtime, server, {&host}, [&] {
        return host.view().phase == RoomPhase::Match && host.view().members.size() == 1 &&
            host.startRoster().size() == 1 && host.startRoster().front().nid == hostNid;
    }), "a Cargo leaver must be removed while the surviving roster keeps its network id");
    host.ReturnToLobby();
    require(pump(runtime, server, {&host}, [&] {
        return host.view().phase == RoomPhase::Lobby && host.view().members.size() == 1 && host.startRoster().empty();
    }), "the surviving Cargo room must return to an unlocked lobby");
}

void testHostMigration(GnsRuntime& runtime, RoomServer* server, const SteamNetworkingIPAddr& address) {
    RoomSession first(runtime), second(runtime), third(runtime);
    first.Create(address, player("First"));
    require(pump(runtime, server, {&first}, [&] { return first.view().phase == RoomPhase::Lobby; }),
            "migration room must be created");
    const auto code = first.view().code;
    second.Join(address, code, player("Second"));
    require(pump(runtime, server, {&first, &second}, [&] { return second.view().members.size() == 2; }),
            "second member must join migration room");
    third.Join(address, code, player("Third"));
    require(pump(runtime, server, {&first, &second, &third}, [&] { return third.view().members.size() == 3; }),
            "third member must join migration room");
    const auto secondId = second.view().selfId;
    const auto thirdId = third.view().selfId;
    first.Leave();
    require(pump(runtime, server, {&second, &third}, [&] {
        return second.view().hostId == secondId && third.view().hostId == secondId &&
            second.view().members.size() == 2 && third.view().members.size() == 2;
    }), "the oldest remaining member must become host after a disconnect");
    third.SetReady(true);
    require(pump(runtime, server, {&second, &third}, [&] { return second.view().members[1].ready; }),
            "the remaining guest must become ready");
    require(second.StartMatch(), "the migrated host must be able to start the match");
    require(pump(runtime, server, {&second, &third}, [&] {
        return second.view().phase == RoomPhase::Starting && third.view().phase == RoomPhase::Starting;
    }), "both remaining members must enter match loading");
    second.MarkLoaded(); third.MarkLoaded();
    require(pump(runtime, server, {&second, &third}, [&] {
        return second.view().phase == RoomPhase::Match && third.view().phase == RoomPhase::Match;
    }), "the host migration match must reach GO");
    const auto departed = std::find_if(second.startRoster().begin(), second.startRoster().end(), [&](const RosterEntry& entry) {
        return entry.owner == thirdId && !entry.bot;
    });
    require(departed != second.startRoster().end(), "the third player must own a roster actor before leaving");
    const auto departedNid = departed->nid;
    third.Leave();
    require(pump(runtime, server, {&second}, [&] {
        return second.view().phase == RoomPhase::Match && second.view().hostId == secondId &&
            second.view().members.size() == 1 && std::any_of(second.startRoster().begin(), second.startRoster().end(),
                [&](const RosterEntry& entry) { return entry.nid == departedNid && entry.owner == secondId && entry.bot; });
    }), "a non-host leaving in a match must update membership and transfer its actor to the unchanged host");
}

void testGameQueueOverload(GnsRuntime& runtime, RoomServer* server, const SteamNetworkingIPAddr& address) {
    RoomSession sender(runtime), receiver(runtime);
    sender.Create(address, player("Sender"));
    require(pump(runtime, server, {&sender}, [&] { return sender.view().phase == RoomPhase::Lobby; }),
            "overflow room must be created");
    receiver.Join(address, sender.view().code, player("Receiver"));
    require(pump(runtime, server, {&sender, &receiver}, [&] { return receiver.view().members.size() == 2; }),
            "overflow receiver must join");
    receiver.SetReady(true);
    require(pump(runtime, server, {&sender, &receiver}, [&] { return sender.view().members[1].ready; }),
            "overflow receiver must ready up");
    require(sender.StartMatch(), "overflow room host must start the match");
    require(pump(runtime, server, {&sender, &receiver}, [&] {
        return sender.view().phase == RoomPhase::Starting && receiver.view().phase == RoomPhase::Starting;
    }), "overflow room must enter loading");
    sender.MarkLoaded(); receiver.MarkLoaded();
    require(pump(runtime, server, {&sender, &receiver}, [&] {
        return sender.view().phase == RoomPhase::Match && receiver.view().phase == RoomPhase::Match;
    }), "overflow room must reach GO");

    const std::vector<std::uint8_t> payload(60 * 1024, 0x5a);
    for (int i = 0; i < 18; ++i) {
        require(sender.SendGame(payload, receiver.view().selfId, true), "reliable game packet send must be accepted");
        const std::string marker="queue"+std::to_string(i);
        sender.Emote(marker);
        require(pump(runtime, server, {&sender, &receiver}, [&] {
            return receiver.view().phase == RoomPhase::Error ||
                std::any_of(receiver.view().members.begin(),receiver.view().members.end(),[&](const Member& member){
                    return member.id==sender.view().selfId&&member.emote==marker;
                });
        }), "packet must leave the sender before exercising the receiver queue");
        if(receiver.view().phase == RoomPhase::Error)break;
    }
    require(pump(runtime, server, {&sender, &receiver}, [&] { return receiver.view().phase == RoomPhase::Error; }),
            "exceeding the bounded game queue must end the session visibly");
    require(receiver.view().error == "遊戲封包佇列已滿，連線已中斷",
            "game queue overload must report a localized terminal error");
}

void testCapacity(GnsRuntime& runtime, RoomServer* server, const SteamNetworkingIPAddr& address, int count) {
    std::vector<std::unique_ptr<RoomSession>> clients;
    std::vector<RoomSession*> active;
    for (int i = 0; i < count; ++i) {
        clients.push_back(std::make_unique<RoomSession>(runtime));
        auto* client = clients.back().get();
        if (i == 0) client->Create(address, player("Player0"));
        else client->Join(address, clients.front()->view().code, player("Player" + std::to_string(i)));
        active.push_back(client);
        require(pump(runtime, server, active, [&] {
            return std::all_of(active.begin(), active.end(), [&](RoomSession* session) {
                return session->view().phase == RoomPhase::Lobby && session->view().members.size() == active.size();
            });
        }), "each room must admit up to eight members");
    }
    std::size_t teams[2]{};
    for (const auto& member : clients.front()->view().members) ++teams[member.team];
    require(clients.front()->view().members.size() == static_cast<std::size_t>(count) &&
            teams[0] == (static_cast<std::size_t>(count) + 1) / 2 && teams[1] == static_cast<std::size_t>(count) / 2,
            "automatic teams must stay balanced");

    RoomSession next(runtime);
    next.Join(address, clients.front()->view().code, player("Next"));
    auto withNext = active;
    withNext.push_back(&next);
    require(pump(runtime, server, withNext, [&] {
        return count == 8 ? next.view().phase == RoomPhase::Error :
            next.view().phase == RoomPhase::Lobby && next.view().members.size() == static_cast<std::size_t>(count + 1);
    }), count == 8 ? "a ninth player must receive the room full response" : "a room must admit another player below capacity");
    if (count == 8) require(next.view().error == "房間已滿", "capacity errors must be localized");
}

void testLobbyHeartbeatLifetime(GnsRuntime& runtime){
    using namespace room_protocol;
    SteamNetworkingIPAddr address{};auto service=startLocalServer(runtime,address);
    auto client=runtime.Connect(address);double clock=0;bool welcomed=false,disconnected=false;double pong=-1;
    auto waitFor=[&](auto done){
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds(5);
        while(std::chrono::steady_clock::now()<deadline){
            runtime.Poll();service->Update(clock);
            for(const auto& event:client->TakeEvents()){
                if(event.type==GnsEvent::Type::Connected){
                    auto writer=Begin(Op::Hello);writer.boolean(true).string("PNG23");WriteProfile(writer,player("Heartbeat"));
                    require(client->Send(client->connection(),writer.data().data(),writer.data().size()),"heartbeat client must send hello");
                }else if(event.type==GnsEvent::Type::Message){
                    Reader reader(event.data);const auto op=ReadHeader(reader);
                    if(op==Op::Snapshot)welcomed=true;
                    else if(op==Op::Pong){pong=reader.f64();reader.finish();}
                }else disconnected=true;
            }
            if(done())return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return false;
    };
    require(waitFor([&]{return welcomed;}),"heartbeat lobby must be created");
    for(int i=0;i<80;++i){
        clock+=2;auto writer=Begin(Op::Ping);writer.f64(clock);
        require(client->Send(client->connection(),writer.data().data(),writer.data().size()),"active lobby heartbeat send failed");
        require(waitFor([&]{return pong==clock||disconnected;})&&!disconnected,"heartbeats must keep an otherwise idle lobby alive beyond 150 seconds");
    }
    clock+=151;service->Update(clock);
    require(waitFor([&]{return disconnected;})&&service->idle(),"a silent lobby peer must expire after 150 seconds without a heartbeat");
}
}

int main(int argc, char** argv) {
    try {
        std::string externalServer;
        int playerCount = 8;
        bool countProvided = false;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--server" && i + 1 < argc) externalServer = argv[++i];
            else {
                int count = 0;
                const auto [end, error] = std::from_chars(arg.data(), arg.data() + arg.size(), count);
                if (error != std::errc{} || end != arg.data() + arg.size() || countProvided)
                    throw std::invalid_argument("Usage: network_room_test [2..8] [--server numericIP:port]");
                playerCount = count;
                countProvided = true;
            }
        }
        if (playerCount < 2 || playerCount > 8)
            throw std::invalid_argument("player count must be from 2 to 8");
        GnsRuntime runtime;
        std::unique_ptr<RoomServer> localServer;
        SteamNetworkingIPAddr address{};
        if (externalServer.empty()) localServer = startLocalServer(runtime, address);
        else address = parseServer(externalServer);

        testGameQueueOverload(runtime, localServer.get(), address);
        if (playerCount == 2) testTwoPlayerFlow(runtime, localServer.get(), address);
        else if (playerCount == 3) testHostMigration(runtime, localServer.get(), address);
        else testCapacity(runtime, localServer.get(), address, playerCount);
        if(playerCount==2&&localServer)testLobbyHeartbeatLifetime(runtime);
        std::cout << "network_room_test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "network_room_test failed: " << error.what() << '\n';
        return 1;
    }
}
