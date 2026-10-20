#include "AudioEngine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <unordered_map>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

namespace restoration {
namespace {
constexpr std::array<std::string_view, 20> kClipNames = {
    "ui_hover", "ui_click", "ui_back", "ui_confirm", "ui_toggle", "ui_slider", "ui_error",
    "shoot_shooter", "hit_marker", "hurt", "squid_in", "squid_out", "jump", "land",
    "ready", "go_horn", "final_count", "times_up", "victory_fanfare", "defeat_jingle",
};
constexpr std::size_t kVoiceLimit = 48;

std::uint16_t U16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (static_cast<std::uint16_t>(p[1]) << 8));
}
std::uint32_t U32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) |
        (static_cast<std::uint32_t>(p[1]) << 8) |
        (static_cast<std::uint32_t>(p[2]) << 16) |
        (static_cast<std::uint32_t>(p[3]) << 24);
}
bool KnownName(std::string_view name) {
    return std::find(kClipNames.begin(), kClipNames.end(), name) != kClipNames.end();
}
std::string MiniaudioError(const char* operation, ma_result result) {
    return std::string(operation) + " failed: " + ma_result_description(result) +
        " (miniaudio result " + std::to_string(static_cast<int>(result)) + ")";
}
}

struct AudioEngine::Impl {
    struct Clip {
        AudioClipMetadata metadata;
        std::vector<std::int16_t> pcm;
    };
    struct Voice {
        // Each voice has its own playback cursor, while borrowing immutable PCM
        // from the owning Clip cache. Sound is uninitialized before this source.
        ma_audio_buffer_ref source{};
        ma_sound sound{};
        bool sourceInitialized = false;
        bool soundInitialized = false;
        bool active = false;
        std::uint64_t serial = 0;
    };

    explicit Impl(std::filesystem::path root) : assetDirectory(std::move(root)) {}
    ~Impl() { Shutdown(); }

    std::filesystem::path assetDirectory;
    std::unordered_map<std::string, Clip> clips;
    AudioVolumes volume;
    std::string error;
    std::uint64_t serial = 0, stolen = 0;
    bool initialized = false;
    ma_engine engine{};
    ma_sound_group music{};
    ma_sound_group sfx{};
    bool engineInitialized = false;
    bool musicInitialized = false;
    bool sfxInitialized = false;
    std::array<Voice, kVoiceLimit> voices{};

    std::optional<Clip> ReadClip(std::string_view name) {
        if (!KnownName(name)) {
            error = "Unknown audio clip: " + std::string(name);
            return std::nullopt;
        }
        const auto path = assetDirectory / (std::string(name) + ".wav");
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) {
            error = "Audio clip not found: " + path.string();
            return std::nullopt;
        }
        const auto end = file.tellg();
        if (end < 12 || static_cast<std::uint64_t>(end) > std::numeric_limits<std::uint32_t>::max()) {
            error = "Invalid WAV length: " + path.string();
            return std::nullopt;
        }
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!file || std::memcmp(bytes.data(), "RIFF", 4) || std::memcmp(bytes.data() + 8, "WAVE", 4)) {
            error = "Invalid RIFF/WAVE file: " + path.string();
            return std::nullopt;
        }

        std::uint16_t format = 0, channels = 0, bits = 0;
        std::uint32_t rate = 0, dataOffset = 0, dataLength = 0;
        for (std::size_t pos = 12; pos + 8 <= bytes.size();) {
            const auto length = U32(bytes.data() + pos + 4);
            const std::size_t payload = pos + 8;
            if (payload + length > bytes.size()) break;
            if (!std::memcmp(bytes.data() + pos, "fmt ", 4) && length >= 16) {
                format = U16(bytes.data() + payload);
                channels = U16(bytes.data() + payload + 2);
                rate = U32(bytes.data() + payload + 4);
                bits = U16(bytes.data() + payload + 14);
            } else if (!std::memcmp(bytes.data() + pos, "data", 4)) {
                dataOffset = static_cast<std::uint32_t>(payload);
                dataLength = length;
            }
            pos = payload + length + (length & 1u);
        }
        if (format != 1 || bits != 16 || (channels != 1 && channels != 2) || rate == 0 || dataLength == 0 ||
            dataOffset + dataLength > bytes.size() || dataLength % (channels * 2) != 0) {
            error = "Expected non-empty PCM16 mono/stereo WAV: " + path.string();
            return std::nullopt;
        }

        Clip clip;
        clip.metadata.name = std::string(name);
        clip.metadata.sampleRate = rate;
        clip.metadata.channels = channels;
        clip.metadata.bitsPerSample = bits;
        clip.metadata.frames = dataLength / (channels * 2);
        clip.metadata.dataBytes = dataLength;
        clip.metadata.durationSeconds = static_cast<double>(clip.metadata.frames) / rate;
        clip.pcm.resize(dataLength / 2);
        std::memcpy(clip.pcm.data(), bytes.data() + dataOffset, dataLength);
        double sumSquares = 0, peak = 0;
        for (const auto sample : clip.pcm) {
            const double x = static_cast<double>(sample) / 32768.0;
            peak = std::max(peak, std::abs(x));
            sumSquares += x * x;
        }
        clip.metadata.peak = peak;
        clip.metadata.rms = std::sqrt(sumSquares / clip.pcm.size());
        return clip;
    }

    Clip* FindClip(std::string_view name) {
        auto it = clips.find(std::string(name));
        if (it != clips.end()) return &it->second;
        auto clip = ReadClip(name);
        if (!clip) return nullptr;
        return &clips.emplace(clip->metadata.name, std::move(*clip)).first->second;
    }

    void StopVoice(Voice& voice) {
        if (voice.soundInitialized) {
            ma_sound_stop(&voice.sound);
            ma_sound_uninit(&voice.sound);
            voice.soundInitialized = false;
        }
        if (voice.sourceInitialized) {
            ma_audio_buffer_ref_uninit(&voice.source);
            voice.sourceInitialized = false;
        }
        voice.active = false;
        voice.serial = 0;
    }

    std::size_t ActiveVoices() {
        if (!initialized) return 0;
        for (auto& voice : voices) {
            if (voice.active && ma_sound_at_end(&voice.sound)) StopVoice(voice);
        }
        return static_cast<std::size_t>(std::count_if(voices.begin(), voices.end(), [](const Voice& v) { return v.active; }));
    }

    void Shutdown() {
        for (auto& voice : voices) StopVoice(voice);
        if (sfxInitialized) { ma_sound_group_uninit(&sfx); sfxInitialized = false; }
        if (musicInitialized) { ma_sound_group_uninit(&music); musicInitialized = false; }
        if (engineInitialized) { ma_engine_uninit(&engine); engineInitialized = false; }
        initialized = false;
    }
};

AudioEngine::AudioEngine(std::filesystem::path assetDirectory) : impl_(std::make_unique<Impl>(std::move(assetDirectory))) {}
AudioEngine::~AudioEngine() = default;

double AudioEngine::VolumeGain(double value) {
    if (!std::isfinite(value)) return 0.0;
    return std::pow(std::clamp(value, 0.0, 1.0), 1.5);
}

bool AudioEngine::Initialize() {
    if (impl_->initialized) return true;
    impl_->error.clear();
    ma_result result = ma_engine_init(nullptr, &impl_->engine);
    if (result != MA_SUCCESS) {
        impl_->error = MiniaudioError("ma_engine_init", result);
        impl_->Shutdown();
        return false;
    }
    impl_->engineInitialized = true;

    result = ma_sound_group_init(&impl_->engine, 0, nullptr, &impl_->sfx);
    if (result != MA_SUCCESS) {
        impl_->error = MiniaudioError("ma_sound_group_init(SFX)", result);
        impl_->Shutdown();
        return false;
    }
    impl_->sfxInitialized = true;

    result = ma_sound_group_init(&impl_->engine, 0, nullptr, &impl_->music);
    if (result != MA_SUCCESS) {
        impl_->error = MiniaudioError("ma_sound_group_init(music)", result);
        impl_->Shutdown();
        return false;
    }
    impl_->musicInitialized = true;

    impl_->initialized = true;
    SetVolumes(impl_->volume);
    return true;
}

bool AudioEngine::Play(std::string_view name, double volume, AudioBus bus) {
    auto* clip = impl_->FindClip(name);
    if (!clip) return false;
    if (!impl_->initialized || VolumeGain(impl_->volume.master) == 0 ||
        VolumeGain(bus == AudioBus::Music ? impl_->volume.music : impl_->volume.sfx) == 0 ||
        !std::isfinite(volume) || volume <= 0) return false;

    if (clip->metadata.sampleRate != 48000 || clip->metadata.channels != 2 || clip->metadata.bitsPerSample != 16) {
        impl_->error = "Miniaudio cue backend accepts source-rendered 48 kHz stereo PCM16 only: " + std::string(name);
        return false;
    }

    impl_->ActiveVoices();
    auto slot = std::find_if(impl_->voices.begin(), impl_->voices.end(), [](const Impl::Voice& v) { return !v.active; });
    if (slot == impl_->voices.end()) {
        slot = std::min_element(impl_->voices.begin(), impl_->voices.end(), [](const Impl::Voice& a, const Impl::Voice& b) { return a.serial < b.serial; });
        impl_->StopVoice(*slot);
        ++impl_->stolen;
    }

    ma_result result = ma_audio_buffer_ref_init(ma_format_s16, clip->metadata.channels, clip->pcm.data(),
        clip->metadata.frames, &slot->source);
    if (result != MA_SUCCESS) {
        impl_->error = MiniaudioError("ma_audio_buffer_ref_init", result);
        impl_->StopVoice(*slot);
        return false;
    }
    slot->source.sampleRate = clip->metadata.sampleRate;
    slot->sourceInitialized = true;

    const ma_uint32 flags = MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_NO_PITCH;
    auto* group = bus == AudioBus::Music ? &impl_->music : &impl_->sfx;
    result = ma_sound_init_from_data_source(&impl_->engine,
        reinterpret_cast<ma_data_source*>(&slot->source), flags, group, &slot->sound);
    if (result != MA_SUCCESS) {
        impl_->error = MiniaudioError("ma_sound_init_from_data_source", result);
        impl_->StopVoice(*slot);
        return false;
    }
    slot->soundInitialized = true;
    ma_sound_set_volume(&slot->sound, static_cast<float>(std::clamp(volume, 0.0, 1.0)));
    result = ma_sound_start(&slot->sound);
    if (result != MA_SUCCESS) {
        impl_->error = MiniaudioError("ma_sound_start", result);
        impl_->StopVoice(*slot);
        return false;
    }

    slot->active = true;
    slot->serial = ++impl_->serial;
    return true;
}

void AudioEngine::SetVolumes(double master, double music, double sfx) {
    impl_->volume.master = std::clamp(std::isfinite(master) ? master : 0.0, 0.0, 1.0);
    impl_->volume.music = std::clamp(std::isfinite(music) ? music : 0.0, 0.0, 1.0);
    impl_->volume.sfx = std::clamp(std::isfinite(sfx) ? sfx : 0.0, 0.0, 1.0);
    if (impl_->engineInitialized) ma_engine_set_volume(&impl_->engine, static_cast<float>(VolumeGain(impl_->volume.master)));
    if (impl_->musicInitialized) ma_sound_group_set_volume(&impl_->music, static_cast<float>(VolumeGain(impl_->volume.music)));
    if (impl_->sfxInitialized) ma_sound_group_set_volume(&impl_->sfx, static_cast<float>(VolumeGain(impl_->volume.sfx)));
}

void AudioEngine::Clear() {
    for (auto& voice : impl_->voices) impl_->StopVoice(voice);
}

std::optional<AudioClipMetadata> AudioEngine::Metadata(std::string_view name) {
    auto* clip = impl_->FindClip(name);
    if (!clip) return std::nullopt;
    return clip->metadata;
}

AudioVolumes AudioEngine::volumes() const { return impl_->volume; }
std::size_t AudioEngine::ActiveVoices() { return impl_->ActiveVoices(); }
std::uint64_t AudioEngine::stolenVoices() const { return impl_->stolen; }
bool AudioEngine::ready() const { return impl_->initialized; }
const std::string& AudioEngine::lastError() const { return impl_->error; }

} // namespace restoration
