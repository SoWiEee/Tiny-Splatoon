#include "RoomServer.hpp"

#include "RoomProtocol.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <deque>
#include <random>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace restoration::network {
using namespace room_protocol;
namespace {

constexpr std::size_t MaxRooms = 32;
constexpr std::size_t MaxMembers = 8;
constexpr std::size_t TeamSize = 4;
constexpr std::size_t MaxMessagesPerSecond = 90;
constexpr double MatchIdleSeconds = 20.0;
constexpr double LobbyIdleSeconds = 150.0;
constexpr double PendingHandshakeSeconds = 10.0;

std::string MakeCode(std::mt19937& random) {
    std::uniform_int_distribution<std::size_t> pick(0, CodeAlphabet.size() - 1);
    std::string code(5, ' ');
    for (auto& character : code) character = CodeAlphabet[pick(random)];
    return code;
}

std::vector<std::uint8_t> RejectPacket(Error error, std::string_view detail = {}) {
    auto writer = Begin(Op::Reject);
    writer.u8(static_cast<std::uint8_t>(error)).string(detail.empty() ? ErrorMessage(error) : detail, 128);
    return writer.take();
}

bool AllLoaded(const std::vector<Member>& members, const std::vector<RosterEntry>& roster,
               const std::unordered_set<MemberId>& loaded) {
    for (const auto& entry : roster) {
        if (entry.bot) continue;
        const auto present = std::find_if(members.begin(), members.end(), [&](const Member& member) {
            return member.id == entry.owner;
        });
        if (present != members.end() && !loaded.count(entry.owner)) return false;
    }
    return true;
}

} // namespace

struct RoomServer::Impl {
    struct Room {
        std::string code;
        std::vector<Member> members;
        std::unordered_map<MemberId, HSteamNetConnection> connections;
        RoomSettings settings;
        RoomPhase phase = RoomPhase::Lobby;
        std::vector<RosterEntry> roster;
        std::unordered_set<MemberId> loaded;
        MemberId hostId = 0;
        bool botsPreference = true;
        std::uint64_t epoch = 0;
        std::uint32_t seed = 0;
        double startDeadline = 0.0;
    };

    struct Peer {
        MemberId id = 0;
        std::string roomCode;
        bool welcomed = false;
        bool kick = false;
        double connectedAt = 0.0;
        double lastSeen = 0.0;
        double kickAt = 0.0;
        std::deque<double> arrivals;
    };

    GnsEndpoint* endpoint = nullptr;
    std::unordered_map<HSteamNetConnection, Peer> peers;
    std::unordered_map<std::string, Room> rooms;
    std::mt19937 random{std::random_device{}()};
    std::uint32_t nextMemberId = 1;
    std::uint64_t nextEpoch = 1;
    double now = 0.0;

    Impl(GnsRuntime& runtime, const SteamNetworkingIPAddr& address) {
        endpoint = runtime.Listen(address, 128).release();
    }

    ~Impl() { delete endpoint; }

    Peer& peer(HSteamNetConnection connection) {
        auto [it, inserted] = peers.try_emplace(connection);
        if (inserted) it->second.connectedAt = now;
        return it->second;
    }

    bool send(HSteamNetConnection connection, const std::vector<std::uint8_t>& bytes, bool reliable = true) {
        if (!endpoint) return false;
        const bool sent = endpoint->Send(connection, bytes.data(), bytes.size(), reliable);
        if (!sent && reliable) endpoint->Disconnect(connection);
        return sent;
    }

    void reject(HSteamNetConnection connection, Error error, std::string_view detail = {}, bool kickAfter = false) {
        send(connection, RejectPacket(error, detail));
        if (kickAfter) {
            auto& sender = peer(connection);
            sender.kick = true;
            sender.kickAt = now + 0.1;
        }
    }

    std::uint32_t allocateMemberId() {
        for (;;) {
            auto id = nextMemberId++;
            if (!id) continue;
            bool used = false;
            for (const auto& [code, room] : rooms) {
                (void)code;
                if (room.connections.count(id)) { used = true; break; }
            }
            if (!used) return id;
        }
    }

    std::vector<std::uint8_t> snapshot(const Room& room, MemberId recipient) const {
        auto writer = Begin(Op::Snapshot);
        writer.string(room.code, MaxRoomCode).u8(static_cast<std::uint8_t>(room.phase))
            .string(room.phase == RoomPhase::Lobby ? "等待玩家" : room.phase == RoomPhase::Starting ? "等待玩家載入" : "對戰進行中", 64)
            .u32(recipient).u32(room.hostId);
        WriteSettings(writer, room.settings);
        writer.u64(room.epoch).u32(room.seed).u8(static_cast<std::uint8_t>(room.members.size()));
        for (const auto& member : room.members) WriteMember(writer, member);
        return writer.take();
    }

    std::vector<std::uint8_t> rosterPacket(const Room& room) const {
        auto writer = Begin(Op::StartRoster);
        writer.u64(room.epoch).u32(room.seed).u8(static_cast<std::uint8_t>(room.roster.size()));
        for (const auto& entry : room.roster) WriteRosterEntry(writer, entry);
        return writer.take();
    }

    void sendSnapshot(const Room& room) {
        for (const auto& member : room.members) {
            const auto connection = room.connections.find(member.id);
            if (connection != room.connections.end()) send(connection->second, snapshot(room, member.id));
        }
    }

    void broadcastRoster(const Room& room) {
        const auto packet = rosterPacket(room);
        for (const auto& member : room.members) {
            const auto connection = room.connections.find(member.id);
            if (connection != room.connections.end()) send(connection->second, packet);
        }
    }

    void broadcastEmote(const Room& room, MemberId sender, std::string_view name) {
        auto writer = Begin(Op::EmoteEvent);
        writer.u32(sender).string(name, MaxTextBytes);
        const auto packet = writer.take();
        for (const auto& member : room.members) {
            const auto connection = room.connections.find(member.id);
            if (member.id != sender && connection != room.connections.end()) send(connection->second, packet);
        }
    }

    void fixTeams(Room& room) {
        std::array<std::size_t, 2> count{};
        for (auto& member : room.members) {
            if (member.autoTeam || member.team < 0 || member.team > 1) {
                member.autoTeam = true;
                continue;
            }
            const auto team = static_cast<std::size_t>(member.team);
            if (count[team] < TeamSize) ++count[team];
            else member.autoTeam = true;
        }
        for (auto& member : room.members) {
            if (!member.autoTeam) continue;
            const int team = count[0] <= count[1] ? 0 : 1;
            member.team = team;
            ++count[static_cast<std::size_t>(team)];
        }
    }

    Member* findMember(Room& room, MemberId id) {
        const auto found = std::find_if(room.members.begin(), room.members.end(), [id](const Member& member) {
            return member.id == id;
        });
        return found == room.members.end() ? nullptr : &*found;
    }

    const restoration::StageData* stageFor(const Room& room) const {
        const std::string_view mode = room.settings.mode == "zones" ? "zones" : "turf";
        return FindStage(room.settings.map, mode);
    }

    bool noBotsBlocked(const Room& room) const {
        const auto* stage = stageFor(room);
        if (!stage || !stage->noBots) return false;
        if (room.members.size() < 2) return true;
        std::array<bool, 2> seen{};
        for (const auto& member : room.members)
            if (member.team >= 0 && member.team < 2) seen[static_cast<std::size_t>(member.team)] = true;
        return !seen[0] || !seen[1];
    }

    RosterEntry humanEntry(const Member& member, std::uint32_t nid, int team, int slot) const {
        RosterEntry entry;
        entry.nid = nid; entry.owner = member.id; entry.team = team; entry.slot = slot;
        entry.name = member.name; entry.main = member.main; entry.sub = member.sub;
        entry.special = member.special; entry.style = member.style;
        return entry;
    }

    RosterEntry botEntry(const Room& room, std::uint32_t nid, int team, int slot,
                         std::unordered_set<std::string>& usedWeapons) {
        const auto& names = restoration::BotNames();
        const auto& weapons = restoration::MainEquipment();
        const auto& subs = restoration::SubEquipment();
        const auto& specials = restoration::SpecialEquipment();
        if (names.empty() || weapons.empty()) throw WireError("bot content is unavailable");
        std::vector<std::string_view> weaponIds;
        for (const auto& weapon : weapons) weaponIds.push_back(weapon.id);
        std::shuffle(weaponIds.begin(), weaponIds.end(), random);
        const auto chosen = std::find_if(weaponIds.begin(), weaponIds.end(), [&](std::string_view id) {
            return !usedWeapons.count(std::string(id));
        });
        const std::string weapon = std::string(chosen == weaponIds.end() ? weaponIds[static_cast<std::size_t>(slot) % weaponIds.size()] : *chosen);
        usedWeapons.insert(weapon);
        std::uniform_int_distribution<std::size_t> namePick(0, names.size() - 1);
        std::uniform_int_distribution<int> coin(0, 1);
        RosterEntry entry;
        entry.nid = nid; entry.owner = room.hostId; entry.bot = true; entry.team = team; entry.slot = slot;
        entry.name = std::string(names[namePick(random)]); entry.main = weapon;
        if (coin(random) && !subs.empty()) {
            std::uniform_int_distribution<std::size_t> pick(0, subs.size() - 1);
            entry.sub = std::string(subs[pick(random)].id);
        }
        if (coin(random) && !specials.empty()) {
            std::uniform_int_distribution<std::size_t> pick(0, specials.size() - 1);
            entry.special = std::string(specials[pick(random)].id);
        }
        std::uniform_int_distribution<int> hair(0, 7), skin(0, 8), outfit(0, 9), eyes(0, 7), hat(0, 3), brows(0, 3);
        entry.style = {hair(random), skin(random), outfit(random), eyes(random), hat(random), brows(random)};
        return entry;
    }

    std::vector<RosterEntry> makeRoster(Room& room) {
        std::vector<RosterEntry> roster;
        const bool boss = room.settings.mode == "boss";
        const bool bots = room.settings.bots && !(stageFor(room) && stageFor(room)->noBots);
        if (boss) {
            int slot = 0;
            for (const auto& member : room.members) roster.push_back(humanEntry(member, static_cast<std::uint32_t>(roster.size()), 0, slot++));
            std::unordered_set<std::string> used;
            while (bots && slot < static_cast<int>(TeamSize * 2)) {
                roster.push_back(botEntry(room, static_cast<std::uint32_t>(roster.size()), 0, slot++, used));
            }
        } else {
            for (int team = 0; team < 2; ++team) {
                int slot = 0;
                for (const auto& member : room.members) {
                    if (member.team != team) continue;
                    roster.push_back(humanEntry(member, static_cast<std::uint32_t>(roster.size()), team, slot++));
                }
                std::unordered_set<std::string> used;
                while (bots && slot < static_cast<int>(TeamSize))
                    roster.push_back(botEntry(room, static_cast<std::uint32_t>(roster.size()), team, slot++, used));
            }
        }
        return roster;
    }

    void startMatch(Room& room) {
        fixTeams(room);
        if (noBotsBlocked(room)) throw WireError("這個場地需要至少兩位玩家，並且每隊都要有人");
        room.settings.bots = room.settings.bots && !(stageFor(room) && stageFor(room)->noBots);
        ValidateSettings(room.settings);
        room.roster = makeRoster(room);
        if (room.roster.empty() || room.roster.size() > MaxMembers) throw WireError("無法建立對戰名單");
        room.phase = RoomPhase::Starting;
        room.epoch = nextEpoch++;
        if (!room.epoch) room.epoch = nextEpoch++;
        room.seed = random();
        room.loaded.clear();
        room.startDeadline = now + 12.0;
        for (auto& member : room.members) member.ready = false;
        sendSnapshot(room);
        broadcastRoster(room);
    }

    void go(Room& room) {
        if (room.phase != RoomPhase::Starting) return;
        room.phase = RoomPhase::Match;
        auto writer = Begin(Op::Go);
        writer.u64(room.epoch);
        const auto packet = writer.take();
        sendSnapshot(room);
        for (const auto& member : room.members) {
            const auto connection = room.connections.find(member.id);
            if (connection != room.connections.end()) send(connection->second, packet);
        }
    }

    void removePeer(HSteamNetConnection connection, bool disconnect) {
        const auto peerIt = peers.find(connection);
        if (peerIt == peers.end()) return;
        const auto code = peerIt->second.roomCode;
        const auto id = peerIt->second.id;
        peers.erase(peerIt);
        if (disconnect && endpoint) endpoint->Disconnect(connection);
        if (code.empty()) return;
        const auto roomIt = rooms.find(code);
        if (roomIt == rooms.end()) return;
        auto& room = roomIt->second;
        if (id) {
            room.connections.erase(id);
            room.members.erase(std::remove_if(room.members.begin(), room.members.end(), [id](const Member& member) {
                return member.id == id;
            }), room.members.end());
            room.loaded.erase(id);
        }
        room.hostId = room.members.empty() ? 0 : room.members.front().id;
        if (room.phase == RoomPhase::Lobby) fixTeams(room);
        if (room.members.empty()) {
            rooms.erase(roomIt);
            return;
        }
        bool rosterChanged = false;
        if (room.phase == RoomPhase::Starting || room.phase == RoomPhase::Match) {
            const auto* stage = stageFor(room);
            const bool removeLeavers = stage && stage->noBots;
            if (removeLeavers) {
                const auto sizeBefore = room.roster.size();
                room.roster.erase(std::remove_if(room.roster.begin(), room.roster.end(), [id](const RosterEntry& entry) {
                    return !entry.bot && entry.owner == id;
                }), room.roster.end());
                rosterChanged = room.roster.size() != sizeBefore;
            }
            for (auto& entry : room.roster) {
                if (entry.bot) {
                    if (entry.owner != room.hostId) rosterChanged = true;
                    entry.owner = room.hostId;
                } else if (entry.owner == id && !removeLeavers) {
                    entry.bot = true;
                    entry.owner = room.hostId;
                    rosterChanged = true;
                }
            }
        }
        sendSnapshot(room);
        if (rosterChanged) broadcastRoster(room);
        if (room.phase == RoomPhase::Starting && AllLoaded(room.members, room.roster, room.loaded)) go(room);
    }

    void sendError(HSteamNetConnection connection, Error error, std::string_view detail = {}) {
        reject(connection, error, detail);
    }

    void handleHello(HSteamNetConnection connection, Reader& reader) {
        auto& sender = peer(connection);
        if (sender.welcomed) throw WireError("duplicate room handshake");
        const bool create = reader.boolean();
        auto code = ReadText(reader, MaxRoomCode, MaxRoomCode, false);
        Member profile = ReadProfile(reader);
        reader.finish();
        ValidateProfile(profile);
        if (create) {
            if (code.size() != 5 || !std::all_of(code.begin(), code.end(), [](char ch) { return CodeAlphabet.find(ch) != std::string_view::npos; })) {
                sendError(connection, Error::Invalid); return;
            }
            if (rooms.count(code)) { sendError(connection, Error::Taken); return; }
            if (rooms.size() >= MaxRooms) { sendError(connection, Error::RoomLimit); return; }
            Room room;
            room.code = code;
            auto [it, inserted] = rooms.emplace(code, std::move(room));
            if (!inserted) { sendError(connection, Error::Taken); return; }
            joinRoom(connection, sender, it->second, std::move(profile));
        } else {
            if (code.size() < 4 || code.size() > MaxRoomCode ||
                !std::all_of(code.begin(), code.end(), [](unsigned char ch) { return std::isalnum(ch) != 0; })) {
                sendError(connection, Error::Invalid); return;
            }
            const auto found = rooms.find(code);
            if (found == rooms.end()) { sendError(connection, Error::Missing); return; }
            if (found->second.phase != RoomPhase::Lobby) { sendError(connection, Error::Locked); return; }
            if (found->second.members.size() >= MaxMembers) { sendError(connection, Error::Full); return; }
            joinRoom(connection, sender, found->second, std::move(profile));
        }
    }

    void joinRoom(HSteamNetConnection connection, Peer& sender, Room& room, Member profile) {
        profile.id = allocateMemberId();
        profile.team = -1; profile.autoTeam = true; profile.ready = false; profile.ping = 0; profile.emote.clear();
        sender.id = profile.id; sender.roomCode = room.code; sender.welcomed = true; sender.lastSeen = now;
        room.members.push_back(std::move(profile));
        room.connections.emplace(sender.id, connection);
        room.hostId = room.members.front().id;
        fixTeams(room);
        sendSnapshot(room);
    }

    Room* memberRoom(Peer& sender, Member*& member) {
        if (!sender.welcomed || sender.roomCode.empty()) return nullptr;
        const auto found = rooms.find(sender.roomCode);
        if (found == rooms.end()) return nullptr;
        member = findMember(found->second, sender.id);
        return member ? &found->second : nullptr;
    }

    void process(HSteamNetConnection connection, const std::vector<std::uint8_t>& bytes) {
        auto& sender = peer(connection);
        if (sender.kick) return;
        sender.lastSeen = now;
        while (!sender.arrivals.empty() && sender.arrivals.front() <= now - 1.0) sender.arrivals.pop_front();
        if (sender.arrivals.size() >= MaxMessagesPerSecond) {
            reject(connection, Error::RateLimit, {}, true);
            return;
        }
        sender.arrivals.push_back(now);
        try {
            Reader reader(bytes);
            const auto op = ReadHeader(reader);
            if (op == Op::Hello) { handleHello(connection, reader); return; }
            Member* member = nullptr;
            Room* roomPtr = memberRoom(sender, member);
            if (!roomPtr || !member) throw WireError("room membership is missing");
            Room& room = *roomPtr;
            switch (op) {
            case Op::SetMe: {
                if (room.phase != RoomPhase::Lobby) break;
                Member profile = ReadProfile(reader); reader.finish(); ValidateProfile(profile);
                member->name = std::move(profile.name); member->main = std::move(profile.main);
                member->sub = std::move(profile.sub); member->special = std::move(profile.special); member->style = profile.style;
                sendSnapshot(room); break;
            }
            case Op::SetTeam: {
                if (room.phase != RoomPhase::Lobby) break;
                const auto team = reader.i32(); reader.finish();
                if (team < -1 || team > 1) throw WireError("invalid team request");
                member->autoTeam = team == -1;
                if (team >= 0) {
                    const auto count = std::count_if(room.members.begin(), room.members.end(), [&](const Member& other) {
                        return other.id != member->id && other.team == team;
                    });
                    if (count >= static_cast<std::ptrdiff_t>(TeamSize)) member->autoTeam = true;
                    else member->team = team;
                }
                fixTeams(room); sendSnapshot(room); break;
            }
            case Op::SetReady: {
                if (room.phase != RoomPhase::Lobby) break;
                member->ready = reader.boolean(); reader.finish(); sendSnapshot(room); break;
            }
            case Op::SetSettings: {
                if (room.phase != RoomPhase::Lobby) break;
                if (member->id != room.hostId) { sendError(connection, Error::HostOnly); break; }
                RoomSettings settings = ReadSettings(reader); reader.finish();
                const auto* currentStage = stageFor(room);
                const bool wasForced = currentStage && currentStage->noBots;
                const bool requestedBots = settings.bots;
                ValidateSettings(settings);
                const std::string_view stageMode = settings.mode == "zones" ? "zones" : "turf";
                const auto* nextStage = FindStage(settings.map, stageMode);
                const bool isForced = nextStage && nextStage->noBots;
                if (!wasForced) room.botsPreference = requestedBots;
                if (wasForced && !isForced) settings.bots = room.botsPreference;
                if (isForced) settings.bots = false;
                room.settings = std::move(settings); sendSnapshot(room); break;
            }
            case Op::Start: {
                reader.finish();
                if (room.phase != RoomPhase::Lobby) break;
                if (member->id != room.hostId) { sendError(connection, Error::HostOnly); break; }
                const bool ready = std::all_of(room.members.begin(), room.members.end(), [&](const Member& other) {
                    return other.id == room.hostId || other.ready;
                });
                if (!ready) { sendError(connection, Error::NotReady); break; }
                try { startMatch(room); }
                catch (const WireError& error) {
                    const std::string_view detail = std::string_view(error.what()).find("這個場地") == 0
                        ? std::string_view(error.what()) : std::string_view("房間設定無效，無法開始對戰");
                    sendError(connection, Error::NotReady, detail);
                }
                break;
            }
            case Op::Loaded: {
                const auto epoch = reader.u64(); reader.finish();
                if (room.phase != RoomPhase::Starting || epoch != room.epoch) break;
                const bool human = std::any_of(room.roster.begin(), room.roster.end(), [&](const RosterEntry& entry) {
                    return !entry.bot && entry.owner == member->id;
                });
                if (human) room.loaded.insert(member->id);
                if (AllLoaded(room.members, room.roster, room.loaded)) go(room);
                break;
            }
            case Op::ReturnToLobby: {
                reader.finish();
                if (room.phase == RoomPhase::Lobby) break;
                if (member->id != room.hostId) {
                    sendError(connection, Error::HostOnly, "只有房主可以結束對戰");
                    break;
                }
                room.phase = RoomPhase::Lobby; room.roster.clear(); room.loaded.clear();
                room.startDeadline = 0;
                for (auto& other : room.members) other.ready = false;
                sendSnapshot(room); break;
            }
            case Op::Game: {
                const auto target = reader.u32(); const bool reliable = reader.boolean();
                const auto payload = reader.bytes(MaxGamePayloadBytes); reader.finish();
                if (room.phase != RoomPhase::Starting && room.phase != RoomPhase::Match) break;
                auto writer = Begin(Op::GameEnvelope);
                writer.u32(member->id).bytes(payload, MaxGamePayloadBytes);
                const auto packet = writer.take();
                for (const auto& recipient : room.members) {
                    if (recipient.id == member->id || (target && recipient.id != target)) continue;
                    const auto peerConnection = room.connections.find(recipient.id);
                    if (peerConnection != room.connections.end()) send(peerConnection->second, packet, reliable);
                }
                break;
            }
            case Op::Emote: {
                const auto emote = ReadText(reader, MaxTextBytes, 16, false); reader.finish();
                if (room.phase != RoomPhase::Lobby && room.phase != RoomPhase::Match) break;
                member->emote = emote; broadcastEmote(room, member->id, emote); sendSnapshot(room); break;
            }
            case Op::Ping: {
                const auto token = reader.f64(); reader.finish();
                auto writer = Begin(Op::Pong); writer.f64(token);
                send(connection, writer.data()); break;
            }
            case Op::PingReport: {
                const auto ping = reader.u32(); reader.finish();
                if (ping > 9999) throw WireError("invalid ping report");
                if (member->ping != ping) { member->ping = ping; if (room.phase == RoomPhase::Lobby) sendSnapshot(room); }
                break;
            }
            default: throw WireError("unknown room packet");
            }
        } catch (const WireError&) {
            reject(connection, Error::Invalid, "房間封包格式錯誤", true);
        } catch (const std::exception&) {
            reject(connection, Error::Invalid, {}, true);
        }
    }

    void Update(double time) {
        if (!std::isfinite(time)) return;
        now = time;
        std::vector<std::string> startTimeouts;
        std::vector<HSteamNetConnection> expiredPeers;
        for (auto& event : endpoint->TakeEvents()) {
            if (event.type == GnsEvent::Type::Connected) {
                auto& sender = peer(event.connection);
                sender.connectedAt = now; sender.lastSeen = now;
            } else if (event.type == GnsEvent::Type::Message) {
                process(event.connection, event.data);
            } else if (event.type == GnsEvent::Type::Disconnected || event.type == GnsEvent::Type::Error) {
                removePeer(event.connection, false);
            }
        }
        for (auto& [connection, sender] : peers) {
            if (sender.kick && now >= sender.kickAt) expiredPeers.push_back(connection);
            else if (!sender.welcomed && now - sender.connectedAt >= PendingHandshakeSeconds) expiredPeers.push_back(connection);
            else if (sender.welcomed) {
                const auto room = rooms.find(sender.roomCode);
                if (room != rooms.end() && now - sender.lastSeen >=
                    (room->second.phase == RoomPhase::Lobby ? LobbyIdleSeconds : MatchIdleSeconds))
                    expiredPeers.push_back(connection);
            }
        }
        for (const auto connection : expiredPeers) removePeer(connection, true);
        for (auto& [code, room] : rooms) {
            if (room.phase == RoomPhase::Starting && now >= room.startDeadline) {
                room.startDeadline = std::numeric_limits<double>::infinity();
                room.loaded.clear();
                startTimeouts.push_back(code);
            }
        }
        for (const auto& code : startTimeouts) {
            const auto found = rooms.find(code);
            if (found != rooms.end()) go(found->second);
        }
    }
};

RoomServer::RoomServer(GnsRuntime& runtime, const SteamNetworkingIPAddr& address)
    : impl_(std::make_unique<Impl>(runtime, address)) {}

RoomServer::~RoomServer() = default;

std::uint16_t RoomServer::port() const { return impl_->endpoint ? impl_->endpoint->port() : 0; }

bool RoomServer::idle() const { return impl_->peers.empty(); }

void RoomServer::Update(double now) { impl_->Update(now); }

} // namespace restoration::network
