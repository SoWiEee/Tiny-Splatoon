#include "RoomSession.hpp"

#include "RoomProtocol.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <deque>
#include <random>
#include <string>
#include <unordered_set>
#include <utility>

namespace restoration::network {
using namespace room_protocol;
namespace {
constexpr std::size_t MaxQueuedGamePackets = 256;
constexpr std::size_t MaxQueuedGamePacketBytes = 1024 * 1024;

std::string NewCode(std::mt19937& random) {
    std::uniform_int_distribution<std::size_t> pick(0, CodeAlphabet.size() - 1);
    std::string result(5, ' ');
    for (auto& character : result) character = CodeAlphabet[pick(random)];
    return result;
}

std::string NormalizeCode(std::string_view input) {
    std::string code;
    for (const unsigned char ch : input) {
        if (std::isalnum(ch)) code.push_back(static_cast<char>(std::toupper(ch)));
    }
    return code;
}

} // namespace

struct RoomSession::Impl {
    GnsRuntime& runtime;
    std::unique_ptr<GnsEndpoint> endpoint;
    RoomView room;
    Member me;
    std::vector<RosterEntry> roster;
    std::deque<GamePacket> gamePackets;
    std::size_t gamePacketBytes = 0;
    std::mt19937 random{std::random_device{}()};
    double now = 0.0;
    double nextPingAt = 0.0;
    double lastPingToken = 0.0;
    bool pingPending = false;
    bool createRequest = false;
    unsigned createAttempts = 0;
    bool welcomed = false;
    bool expectedDisconnect = false;

    explicit Impl(GnsRuntime& value) : runtime(value) { room.status = "離線"; }

    void setError(std::string message, bool terminal) {
        room.error = std::move(message);
        room.status = room.error;
        if (terminal) room.phase = RoomPhase::Error;
    }

    void reset() {
        expectedDisconnect = true;
        endpoint.reset();
        expectedDisconnect = false;
        room = RoomView{};
        room.status = "離線";
        me = Member{};
        roster.clear();
        gamePackets.clear();
        gamePacketBytes = 0;
        nextPingAt = 0.0;
        lastPingToken = 0.0;
        pingPending = false;
        createRequest = false;
        createAttempts = 0;
        welcomed = false;
    }

    void sendHello() {
        if (!endpoint) return;
        auto writer = Begin(Op::Hello);
        writer.boolean(createRequest).string(room.code, MaxRoomCode);
        WriteProfile(writer, me);
        if (!endpoint->Send(endpoint->connection(), writer.data().data(), writer.data().size()))
            setError("無法送出房間連線要求", true);
        else if (createRequest) ++createAttempts;
    }

    void connect(const SteamNetworkingIPAddr& address, Member profile, bool create, std::string code) {
        reset();
        createRequest = create;
        me = std::move(profile);
        me.id = 0; me.team = -1; me.autoTeam = true; me.ready = false; me.ping = 0; me.emote.clear();
        try { ValidateProfile(me); }
        catch (const std::exception&) { setError("玩家資料無效", true); return; }
        room.phase = RoomPhase::Connecting;
        room.status = "正在連線房間伺服器";
        room.code = std::move(code);
        try { endpoint = runtime.Connect(address); }
        catch (const std::exception&) { setError("無法連接房間伺服器", true); endpoint.reset(); }
    }

    void handleReject(Reader& reader) {
        const auto rawError = reader.u8();
        const auto detail = ReadText(reader, 128, 64, false);
        reader.finish();
        const auto error = static_cast<Error>(rawError);
        if (createRequest && error == Error::Taken && createAttempts < 4 && endpoint) {
            room.code = NewCode(random);
            room.status = "正在建立房間";
            sendHello();
            return;
        }
        room.error = detail;
        room.status = detail;
        if (!welcomed) room.phase = RoomPhase::Error;
    }

    void handleSnapshot(Reader& reader) {
        RoomView updated;
        updated.code = ReadText(reader, MaxRoomCode, MaxRoomCode, false);
        const auto rawPhase = reader.u8();
        if (rawPhase != static_cast<std::uint8_t>(RoomPhase::Lobby) &&
            rawPhase != static_cast<std::uint8_t>(RoomPhase::Starting) &&
            rawPhase != static_cast<std::uint8_t>(RoomPhase::Match)) throw WireError("invalid room phase");
        updated.phase = static_cast<RoomPhase>(rawPhase);
        updated.status = ReadText(reader, 64, 32, false);
        updated.selfId = reader.u32(); updated.hostId = reader.u32();
        updated.settings = ReadSettings(reader); ValidateSettings(updated.settings);
        updated.matchEpoch = reader.u64(); updated.seed = reader.u32();
        const auto count = reader.u8();
        if (!updated.selfId || !updated.hostId || count == 0 || count > 8) throw WireError("invalid room membership");
        updated.members.reserve(count);
        bool hasSelf = false, hasHost = false;
        for (std::uint8_t i = 0; i < count; ++i) {
            auto member = ReadMember(reader);
            hasSelf |= member.id == updated.selfId;
            hasHost |= member.id == updated.hostId;
            updated.members.push_back(std::move(member));
        }
        reader.finish();
        if (!hasSelf || !hasHost) throw WireError("room host or self is missing");
        updated.error.clear();
        room = std::move(updated);
        me.id = room.selfId;
        const auto self = std::find_if(room.members.begin(), room.members.end(), [&](const Member& member) {
            return member.id == room.selfId;
        });
        if (self != room.members.end()) me = *self;
        welcomed = true;
        if (room.phase == RoomPhase::Lobby) roster.clear();
    }

    void handleStartRoster(Reader& reader) {
        const auto epoch = reader.u64();
        const auto seed = reader.u32();
        const auto count = reader.u8();
        if (!epoch || !count || count > 8 || epoch != room.matchEpoch ||
            (room.phase != RoomPhase::Starting && room.phase != RoomPhase::Match))
            throw WireError("invalid start roster");
        std::vector<RosterEntry> next;
        next.reserve(count);
        std::unordered_set<std::uint32_t> nids;
        for (std::uint8_t i = 0; i < count; ++i) {
            auto entry = ReadRosterEntry(reader);
            if (!nids.insert(entry.nid).second || std::none_of(room.members.begin(), room.members.end(), [&](const Member& member) {
                    return member.id == entry.owner;
                })) throw WireError("invalid roster network ids or owner");
            next.push_back(std::move(entry));
        }
        reader.finish();
        roster = std::move(next);
        room.seed = seed;
    }

    void handleGame(Reader& reader) {
        GamePacket packet;
        packet.from = reader.u32();
        packet.payload = reader.bytes(MaxGamePayloadBytes);
        reader.finish();
        if ((room.phase != RoomPhase::Starting && room.phase != RoomPhase::Match) || !packet.from ||
            packet.from == room.selfId || std::none_of(room.members.begin(), room.members.end(), [&](const Member& member) {
                return member.id == packet.from;
            })) return;
        if (gamePackets.size() >= MaxQueuedGamePackets ||
            packet.payload.size() > MaxQueuedGamePacketBytes - gamePacketBytes) {
            setError("遊戲封包佇列已滿，連線已中斷", true);
            welcomed = false;
            gamePackets.clear();
            gamePacketBytes = 0;
            if (endpoint) {
                const auto connection = endpoint->connection();
                if (connection != k_HSteamNetConnection_Invalid) endpoint->Disconnect(connection);
                endpoint.reset();
            }
            return;
        }
        gamePacketBytes += packet.payload.size();
        gamePackets.push_back(std::move(packet));
    }

    void process(const GnsEvent& event) {
        try {
            if (event.type == GnsEvent::Type::Connected) {
                sendHello();
                return;
            }
            if (event.type == GnsEvent::Type::Disconnected || event.type == GnsEvent::Type::Error) {
                if (expectedDisconnect || room.phase == RoomPhase::Offline) return;
                if (!room.error.empty() && room.phase == RoomPhase::Error) return;
                setError("與房間伺服器的連線中斷", true);
                return;
            }
            if (event.type != GnsEvent::Type::Message) return;
            Reader reader(event.data);
            const auto op = ReadHeader(reader);
            switch (op) {
            case Op::Reject: handleReject(reader); break;
            case Op::Snapshot: handleSnapshot(reader); break;
            case Op::StartRoster: handleStartRoster(reader); break;
            case Op::Go: {
                const auto epoch = reader.u64(); reader.finish();
                if (room.phase == RoomPhase::Starting && epoch == room.matchEpoch) {
                    room.phase = RoomPhase::Match; room.status = "對戰進行中"; room.error.clear();
                }
                break;
            }
            case Op::GameEnvelope: handleGame(reader); break;
            case Op::Pong: {
                const auto token = reader.f64(); reader.finish();
                if (pingPending && token == lastPingToken && now >= token) {
                    const auto ping = static_cast<std::uint32_t>(std::clamp(std::lround((now - token) * 1000.0), 0L, 9999L));
                    me.ping = ping; pingPending = false;
                    auto writer = Begin(Op::PingReport); writer.u32(ping);
                    endpoint->Send(endpoint->connection(), writer.data().data(), writer.data().size());
                }
                break;
            }
            case Op::EmoteEvent: {
                const auto id = reader.u32(); const auto emote = ReadText(reader, MaxTextBytes, 16, false); reader.finish();
                const auto found = std::find_if(room.members.begin(), room.members.end(), [id](const Member& member) {
                    return member.id == id;
                });
                if (found != room.members.end()) found->emote = emote;
                break;
            }
            default: throw WireError("unexpected server room packet");
            }
        } catch (const WireError&) {
            setError("房間伺服器傳回無效資料", true);
        } catch (const std::exception&) {
            setError("無法讀取房間資料", true);
        }
    }

    bool sendPacket(Writer writer, bool reliable = true) {
        if (!endpoint || !welcomed || endpoint->connection() == k_HSteamNetConnection_Invalid) return false;
        const auto& bytes = writer.data();
        return endpoint->Send(endpoint->connection(), bytes.data(), bytes.size(), reliable);
    }

    void Update(double time) {
        if (!std::isfinite(time)) return;
        now = time;
        if (endpoint) {
            for (const auto& event : endpoint->TakeEvents()) {
                if (room.phase == RoomPhase::Error) break;
                process(event);
            }
            if (room.phase == RoomPhase::Error) return;
            if (welcomed && now >= nextPingAt) {
                auto writer = Begin(Op::Ping); writer.f64(now);
                if (sendPacket(std::move(writer))) { lastPingToken = now; pingPending = true; }
                nextPingAt = now + 2.0;
            }
        }
    }
};

RoomSession::RoomSession(GnsRuntime& runtime) : impl_(std::make_unique<Impl>(runtime)) {}
RoomSession::~RoomSession() = default;

void RoomSession::Create(const SteamNetworkingIPAddr& address, Member me) {
    impl_->connect(address, std::move(me), true, NewCode(impl_->random));
    if (impl_->room.phase == RoomPhase::Connecting) impl_->room.status = "正在建立房間";
}

void RoomSession::Join(const SteamNetworkingIPAddr& address, std::string code, Member me) {
    code = NormalizeCode(code);
    if (code.size() < 4 || code.size() > MaxRoomCode) {
        impl_->reset(); impl_->setError("房間代碼無效", true); return;
    }
    impl_->connect(address, std::move(me), false, std::move(code));
}

void RoomSession::Leave() { impl_->reset(); }

void RoomSession::Update(double now) { impl_->Update(now); }

void RoomSession::SetMe(Member me) {
    if (impl_->room.phase != RoomPhase::Lobby) return;
    me.id = impl_->room.selfId;
    try { room_protocol::ValidateProfile(me); } catch (const std::exception&) { return; }
    impl_->me.name = me.name; impl_->me.main = me.main; impl_->me.sub = me.sub;
    impl_->me.special = me.special; impl_->me.style = me.style;
    auto writer = room_protocol::Begin(room_protocol::Op::SetMe);
    room_protocol::WriteProfile(writer, impl_->me);
    impl_->sendPacket(std::move(writer));
}

void RoomSession::SetTeam(int team) {
    if (impl_->room.phase != RoomPhase::Lobby || team < -1 || team > 1) return;
    auto writer = room_protocol::Begin(room_protocol::Op::SetTeam); writer.i32(team);
    impl_->sendPacket(std::move(writer));
}

void RoomSession::SetReady(bool ready) {
    if (impl_->room.phase != RoomPhase::Lobby) return;
    auto writer = room_protocol::Begin(room_protocol::Op::SetReady); writer.boolean(ready);
    impl_->sendPacket(std::move(writer));
}

void RoomSession::SetSettings(RoomSettings settings) {
    if (impl_->room.phase != RoomPhase::Lobby || impl_->room.selfId != impl_->room.hostId) return;
    try { room_protocol::ValidateSettings(settings); } catch (const std::exception&) { return; }
    auto writer = room_protocol::Begin(room_protocol::Op::SetSettings);
    room_protocol::WriteSettings(writer, settings);
    impl_->sendPacket(std::move(writer));
}

void RoomSession::Emote(std::string name) {
    if (impl_->room.phase != RoomPhase::Lobby && impl_->room.phase != RoomPhase::Match) return;
    if (name.size() > room_protocol::MaxTextBytes || !room_protocol::ValidUtf8(name, 16) || name.empty()) return;
    auto writer = room_protocol::Begin(room_protocol::Op::Emote); writer.string(name, room_protocol::MaxTextBytes);
    impl_->sendPacket(std::move(writer));
}

bool RoomSession::StartMatch() {
    if (impl_->room.phase != RoomPhase::Lobby || impl_->room.selfId != impl_->room.hostId) return false;
    auto writer = room_protocol::Begin(room_protocol::Op::Start);
    return impl_->sendPacket(std::move(writer));
}

void RoomSession::MarkLoaded() {
    if (impl_->room.phase != RoomPhase::Starting || !impl_->room.matchEpoch) return;
    auto writer = room_protocol::Begin(room_protocol::Op::Loaded); writer.u64(impl_->room.matchEpoch);
    impl_->sendPacket(std::move(writer));
}

void RoomSession::ReturnToLobby() {
    if (impl_->room.phase != RoomPhase::Starting && impl_->room.phase != RoomPhase::Match) return;
    auto writer = room_protocol::Begin(room_protocol::Op::ReturnToLobby);
    impl_->sendPacket(std::move(writer));
}

bool RoomSession::SendGame(std::vector<std::uint8_t> payload, MemberId toMemberId, bool reliable) {
    if ((impl_->room.phase != RoomPhase::Starting && impl_->room.phase != RoomPhase::Match) ||
        payload.size() > room_protocol::MaxGamePayloadBytes) return false;
    auto writer = room_protocol::Begin(room_protocol::Op::Game);
    writer.u32(toMemberId).boolean(reliable).bytes(payload, room_protocol::MaxGamePayloadBytes);
    return impl_->sendPacket(std::move(writer), reliable);
}

std::vector<GamePacket> RoomSession::TakeGamePackets() {
    std::vector<GamePacket> packets;
    packets.reserve(impl_->gamePackets.size());
    while (!impl_->gamePackets.empty()) {
        impl_->gamePacketBytes -= impl_->gamePackets.front().payload.size();
        packets.push_back(std::move(impl_->gamePackets.front()));
        impl_->gamePackets.pop_front();
    }
    return packets;
}

const RoomView& RoomSession::view() const { return impl_->room; }

const std::vector<RosterEntry>& RoomSession::startRoster() const { return impl_->roster; }

} // namespace restoration::network
