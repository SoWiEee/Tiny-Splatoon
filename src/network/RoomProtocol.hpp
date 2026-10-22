#pragma once

#include "GnsTransport.hpp"
#include "RoomTypes.hpp"
#include "Wire.hpp"
#include "../game/ArsenalData.hpp"
#include "../world/ReferenceData.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace restoration::network::room_protocol {

enum class Op : std::uint8_t {
    Hello = 1, SetMe, SetTeam, SetReady, SetSettings, Start, Loaded, ReturnToLobby, Game, Emote,
    Ping, PingReport, Snapshot = 0x80, StartRoster, Go, Reject, GameEnvelope, Pong, EmoteEvent
};

enum class Error : std::uint8_t { Taken = 1, Missing, Full, Locked, RoomLimit, Invalid, HostOnly, NotReady, RateLimit };

constexpr std::string_view CodeAlphabet = "BCEFGHJKLMNPQRTUVXYZ23456789";
constexpr std::size_t MaxRoomCode = 8;
constexpr std::size_t MaxNameBytes = 64;
constexpr std::size_t MaxTextBytes = 64;
constexpr std::size_t MaxStringBytes = 256;
constexpr std::size_t GameEnvelopeBytes = 12;
constexpr std::size_t MaxGamePayloadBytes = GnsEndpoint::MaxMessageBytes - GameEnvelopeBytes;

inline Writer Begin(Op op) {
    Writer writer;
    writer.u16(WireVersion).u8(static_cast<std::uint8_t>(op));
    return writer;
}

inline Op ReadHeader(Reader& reader) {
    if (reader.u16() != WireVersion) throw WireError("wire version mismatch");
    return static_cast<Op>(reader.u8());
}

inline void RequireOp(Op actual, Op expected) {
    if (actual != expected) throw WireError("unexpected room packet");
}

inline bool ValidUtf8(std::string_view text, std::size_t maxCharacters = static_cast<std::size_t>(-1)) {
    std::size_t chars = 0;
    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        std::uint32_t cp = 0;
        std::size_t count = 0;
        if (lead < 0x80) { cp = lead; count = 1; }
        else if (lead >= 0xc2 && lead <= 0xdf) { cp = lead & 0x1f; count = 2; }
        else if (lead >= 0xe0 && lead <= 0xef) { cp = lead & 0x0f; count = 3; }
        else if (lead >= 0xf0 && lead <= 0xf4) { cp = lead & 0x07; count = 4; }
        else return false;
        if (i + count > text.size()) return false;
        for (std::size_t j = 1; j < count; ++j) {
            const auto byte = static_cast<unsigned char>(text[i + j]);
            if ((byte & 0xc0) != 0x80) return false;
            cp = (cp << 6) | (byte & 0x3f);
        }
        if ((count == 3 && cp < 0x800) || (count == 4 && cp < 0x10000) || cp > 0x10ffff ||
            (cp >= 0xd800 && cp <= 0xdfff) || cp < 0x20 || (cp >= 0x7f && cp <= 0x9f)) return false;
        if (++chars > maxCharacters) return false;
        i += count;
    }
    return true;
}

inline std::string ReadText(Reader& reader, std::size_t maxBytes, std::size_t maxCharacters,
                            bool emptyAllowed = true) {
    auto text = reader.string(maxBytes);
    if ((!emptyAllowed && text.empty()) || !ValidUtf8(text, maxCharacters)) throw WireError("invalid UTF-8 text");
    return text;
}

inline void WriteStyle(Writer& writer, const restoration::CharacterStyle& style) {
    writer.i32(style.hair).i32(style.skin).i32(style.outfit).i32(style.eyes).i32(style.hat).i32(style.brows);
}

inline restoration::CharacterStyle ReadStyle(Reader& reader) {
    restoration::CharacterStyle style;
    style.hair = reader.i32(); style.skin = reader.i32(); style.outfit = reader.i32();
    style.eyes = reader.i32(); style.hat = reader.i32(); style.brows = reader.i32();
    if (style.hair < 0 || style.hair > 7 || style.skin < 0 || style.skin > 8 ||
        style.outfit < 0 || style.outfit > 9 || style.eyes < 0 || style.eyes > 7 ||
        style.hat < 0 || style.hat > 3 || style.brows < 0 || style.brows > 3)
        throw WireError("invalid character style");
    return style;
}

inline void WriteProfile(Writer& writer, const Member& member) {
    writer.string(member.name, MaxNameBytes).string(member.main, MaxTextBytes)
        .string(member.sub, MaxTextBytes).string(member.special, MaxTextBytes);
    WriteStyle(writer, member.style);
}

inline Member ReadProfile(Reader& reader) {
    Member member;
    member.name = ReadText(reader, MaxNameBytes, 16, false);
    member.main = ReadText(reader, MaxTextBytes, 32, false);
    member.sub = ReadText(reader, MaxTextBytes, 32);
    member.special = ReadText(reader, MaxTextBytes, 32);
    member.style = ReadStyle(reader);
    return member;
}

inline bool HasEquipment(restoration::EquipmentCategory category, std::string_view id) {
    const std::vector<restoration::EquipmentData>* items = nullptr;
    switch (category) {
    case restoration::EquipmentCategory::Main: items = &restoration::MainEquipment(); break;
    case restoration::EquipmentCategory::Sub: items = &restoration::SubEquipment(); break;
    case restoration::EquipmentCategory::Special: items = &restoration::SpecialEquipment(); break;
    }
    return items && std::any_of(items->begin(), items->end(), [id](const auto& item) { return item.id == id; });
}

inline void ValidateProfile(const Member& member) {
    if (!ValidUtf8(member.name, 16) || member.name.empty() || member.name.size() > MaxNameBytes ||
        !HasEquipment(restoration::EquipmentCategory::Main, member.main) ||
        (!member.sub.empty() && !HasEquipment(restoration::EquipmentCategory::Sub, member.sub)) ||
        (!member.special.empty() && !HasEquipment(restoration::EquipmentCategory::Special, member.special)) ||
        member.style.hair < 0 || member.style.hair > 7 || member.style.skin < 0 || member.style.skin > 8 ||
        member.style.outfit < 0 || member.style.outfit > 9 || member.style.eyes < 0 || member.style.eyes > 7 ||
        member.style.hat < 0 || member.style.hat > 3 || member.style.brows < 0 || member.style.brows > 3)
        throw WireError("invalid public profile");
}

inline void WriteMember(Writer& writer, const Member& member) {
    writer.u32(member.id).string(member.name, MaxNameBytes).i32(member.team).boolean(member.autoTeam).boolean(member.ready);
    writer.string(member.main, MaxTextBytes).string(member.sub, MaxTextBytes).string(member.special, MaxTextBytes);
    WriteStyle(writer, member.style);
    writer.u32(member.ping).string(member.emote, MaxTextBytes);
}

inline Member ReadMember(Reader& reader) {
    Member member;
    member.id = reader.u32();
    member.name = ReadText(reader, MaxNameBytes, 16, false);
    member.team = reader.i32();
    if (member.team < 0 || member.team > 1) throw WireError("invalid team");
    member.autoTeam = reader.boolean(); member.ready = reader.boolean();
    member.main = ReadText(reader, MaxTextBytes, 32, false);
    member.sub = ReadText(reader, MaxTextBytes, 32); member.special = ReadText(reader, MaxTextBytes, 32);
    member.style = ReadStyle(reader); member.ping = reader.u32();
    member.emote = ReadText(reader, MaxTextBytes, 16);
    ValidateProfile(member);
    if (member.id == 0 || member.ping > 9999) throw WireError("invalid room member");
    return member;
}

inline void WriteSettings(Writer& writer, const RoomSettings& settings) {
    writer.string(settings.map, MaxTextBytes).string(settings.time, MaxTextBytes).string(settings.mode, MaxTextBytes)
        .u32(settings.duration).boolean(settings.bots).string(settings.difficulty, MaxTextBytes).u32(settings.palette);
}

inline RoomSettings ReadSettings(Reader& reader) {
    RoomSettings settings;
    settings.map = ReadText(reader, MaxTextBytes, 32, false);
    settings.time = ReadText(reader, MaxTextBytes, 16, false);
    settings.mode = ReadText(reader, MaxTextBytes, 16, false);
    settings.duration = reader.u32(); settings.bots = reader.boolean();
    settings.difficulty = ReadText(reader, MaxTextBytes, 16, false); settings.palette = reader.u32();
    return settings;
}

inline const restoration::StageData* FindStage(std::string_view id, std::string_view mode = {}) {
    for (const auto& stage : restoration::ReferenceStages())
        if (stage.id == id && (mode.empty() || stage.mode == mode)) return &stage;
    return nullptr;
}

inline void ValidateSettings(RoomSettings& settings) {
    if (settings.mode != "turf" && settings.mode != "zones" && settings.mode != "boss")
        throw WireError("invalid match mode");
    if (settings.time != "day" && settings.time != "dusk") throw WireError("invalid stage time");
    if (settings.difficulty != "easy" && settings.difficulty != "normal" && settings.difficulty != "hard")
        throw WireError("invalid difficulty");
    if (settings.duration < 60 || settings.duration > 600 || settings.palette >= 5)
        throw WireError("invalid match settings");
    if (settings.mode == "zones") settings.duration = 300;
    const std::string_view stageMode = settings.mode == "zones" ? "zones" : "turf";
    auto* stage = FindStage(settings.map, stageMode);
    if (!stage) throw WireError("invalid stage");
    if (settings.mode == "boss" && stage->noBoss) {
        stage = nullptr;
        for (const auto& option : restoration::ReferenceStages())
            if (!option.onlineOnly && option.mode == "turf" && !option.noBoss) { stage = &option; break; }
        if (!stage) throw WireError("no Boss Battle stage is available");
        settings.map = std::string(stage->id);
    }
    if (stage->noBots) settings.bots = false;
}

inline void WriteRosterEntry(Writer& writer, const RosterEntry& entry) {
    writer.u32(entry.nid).u32(entry.owner).boolean(entry.bot).i32(entry.team).i32(entry.slot)
        .string(entry.name, MaxNameBytes).string(entry.main, MaxTextBytes).string(entry.sub, MaxTextBytes)
        .string(entry.special, MaxTextBytes);
    WriteStyle(writer, entry.style);
}

inline RosterEntry ReadRosterEntry(Reader& reader) {
    RosterEntry entry;
    entry.nid = reader.u32(); entry.owner = reader.u32(); entry.bot = reader.boolean();
    entry.team = reader.i32(); entry.slot = reader.i32();
    entry.name = ReadText(reader, MaxNameBytes, 16, false);
    entry.main = ReadText(reader, MaxTextBytes, 32, false); entry.sub = ReadText(reader, MaxTextBytes, 32);
    entry.special = ReadText(reader, MaxTextBytes, 32); entry.style = ReadStyle(reader);
    if (entry.nid > 7 || entry.owner == 0 || entry.team < 0 || entry.team > 1 || entry.slot < 0 || entry.slot > 7 ||
        !HasEquipment(restoration::EquipmentCategory::Main, entry.main) ||
        (!entry.sub.empty() && !HasEquipment(restoration::EquipmentCategory::Sub, entry.sub)) ||
        (!entry.special.empty() && !HasEquipment(restoration::EquipmentCategory::Special, entry.special)))
        throw WireError("invalid match roster");
    return entry;
}

inline std::string_view ErrorMessage(Error error) {
    switch (error) {
    case Error::Taken: return "房間代碼已被使用";
    case Error::Missing: return "找不到房間";
    case Error::Full: return "房間已滿";
    case Error::Locked: return "房間正在進行對戰";
    case Error::RoomLimit: return "房間數已達上限";
    case Error::Invalid: return "房間資料無效";
    case Error::HostOnly: return "只有房主可以更改房間設定或開始對戰";
    case Error::NotReady: return "請等所有玩家準備完成";
    case Error::RateLimit: return "訊息傳送過於頻繁";
    }
    return "連線要求無效";
}

} // namespace restoration::network::room_protocol
