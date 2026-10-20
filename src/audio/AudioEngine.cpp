#include "AudioEngine.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <random>
#include <sstream>
#include <unordered_map>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

namespace restoration {
namespace {
constexpr std::size_t kVoiceLimit = 48;
constexpr std::size_t kLoopLimit = 24;
constexpr std::size_t kMusicVoices = 2;

std::uint16_t U16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | (static_cast<std::uint16_t>(p[1]) << 8));
}
std::uint32_t U32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) |
        (static_cast<std::uint32_t>(p[1]) << 8) |
        (static_cast<std::uint32_t>(p[2]) << 16) |
        (static_cast<std::uint32_t>(p[3]) << 24);
}
bool SafeName(std::string_view name) {
    return !name.empty() && std::all_of(name.begin(), name.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
            (c >= '0' && c <= '9') || c == '_' || c == '-';
    });
}
std::string MiniaudioError(const char* operation, ma_result result) {
    return std::string(operation) + " failed: " + ma_result_description(result) +
        " (miniaudio result " + std::to_string(static_cast<int>(result)) + ")";
}
double ClampFinite(double value, double fallback = 0.0) {
    return std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : fallback;
}
std::uint32_t MulberryNext(std::uint32_t& state) {
    state += 0x6D2B79F5u;
    std::uint32_t value = state;
    value = (value ^ (value >> 15)) * (value | 1u);
    value ^= value + ((value ^ (value >> 7)) * (value | 61u));
    return value ^ (value >> 14);
}
float MulberryUnit(std::uint32_t& state) {
    return static_cast<float>(MulberryNext(state)) / 4294967296.0f;
}
const std::vector<float>& SourceWhiteNoise() {
    static const auto samples = [] {
        constexpr std::size_t count = 96000;
        std::vector<float> result(count);
        std::uint32_t state = 11;
        double mean = 0;
        for (auto& sample : result) { sample = MulberryUnit(state) * 2.0f - 1.0f; mean += sample; }
        mean /= result.size();
        double squares = 0;
        for (auto& sample : result) { sample = static_cast<float>(sample - mean); squares += sample * sample; }
        const double gain = 0.3 / std::sqrt(squares / result.size());
        for (auto& sample : result) sample = static_cast<float>(sample * gain);
        return result;
    }();
    return samples;
}
struct BiquadState {
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    double Process(double x, double b0, double b1, double b2, double a1, double a2) {
        const double y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
};
double CompressDb(double sample, double thresholdDb, double kneeDb, double ratio) {
    const double amplitude = std::abs(sample);
    if (amplitude <= 1e-9) return sample;
    const double level = 20.0 * std::log10(amplitude);
    const double lower = thresholdDb - kneeDb * 0.5, upper = thresholdDb + kneeDb * 0.5;
    double output = level;
    if (kneeDb > 0 && level > lower && level < upper) {
        const double offset = level - lower;
        output = level + (1.0 / ratio - 1.0) * offset * offset / (2.0 * kneeDb);
    } else if (level >= upper) output = thresholdDb + (level - thresholdDb) / ratio;
    return std::copysign(std::pow(10.0, output / 20.0), sample);
}
}

struct AudioEngine::Impl {
    struct SourceMetadata { std::uint32_t maxVoices = 6; double jitter = 0.06, minGap = 0.018; bool loopable = false; };
    struct MusicMetadata { std::uint64_t frames = 0, loopBegin = 0, loopEnd = 0; double bpm = 0; };
    struct PendingMusic { std::string track; double fade = 0, startAt = 0; };
    struct Clip { AudioClipMetadata metadata; std::vector<std::int16_t> pcm; };
    struct Voice {
        ma_audio_buffer_ref source{};
        ma_sound sound{};
        bool sourceInitialized = false, soundInitialized = false, active = false, stopping = false;
        std::string name, loopKey;
        std::uint64_t serial = 0;
        double stopAt = 0;
    };
    struct MusicVoice {
        ma_sound sound{};
        ma_audio_buffer_ref source{};
        std::vector<std::int16_t> pcm;
        bool initialized = false, active = false, looping = true;
        bool sourceInitialized = false;
        std::string track;
        std::uint64_t serial = 0;
        double stopAt = 0;
    };

    explicit Impl(std::filesystem::path root) : assetDirectory(std::move(root)) { LoadCatalog(); }
    ~Impl() { Shutdown(); }

    std::filesystem::path assetDirectory;
    std::unordered_map<std::string, SourceMetadata> catalog;
    std::unordered_map<std::string, MusicMetadata> musicCatalog;
    std::unordered_map<std::string, Clip> clips;
    std::unordered_map<std::string, double> lastPlayed;
    std::unordered_map<std::string, std::size_t> loopSlots;
    AudioVolumes volume;
    std::string error;
    std::uint64_t serial = 0, stolen = 0, randomState = 0x9E3779B97F4A7C15ull;
    bool initialized = false;
    ma_engine engine{};
    ma_sound_group music{}, sfx{};
    bool engineInitialized = false, musicInitialized = false, sfxInitialized = false;
    std::array<Voice, kVoiceLimit + kLoopLimit> voices{};
    std::array<MusicVoice, kMusicVoices> musicVoices{};
    MusicVoice transitionRiser{};
    std::size_t currentMusic = kMusicVoices;
    std::optional<PendingMusic> pendingMusic;
    bool atMusicBoundary = false;
    double musicDuckUntil = 0;
    bool musicDucked = false;

    void LoadCatalog() {
        std::ifstream file(assetDirectory / "catalog.tsv");
        for (std::string line; std::getline(file, line);) {
            std::istringstream row(line);
            std::string name, max, jitter, gap, loop;
            if (!std::getline(row, name, '\t') || !SafeName(name) ||
                !std::getline(row, max, '\t') || !std::getline(row, jitter, '\t') ||
                !std::getline(row, gap, '\t') || !std::getline(row, loop, '\t')) continue;
            try {
                SourceMetadata meta;
                meta.maxVoices = std::max(1u, static_cast<std::uint32_t>(std::stoul(max)));
                meta.jitter = std::clamp(std::stod(jitter), 0.0, 0.5);
                meta.minGap = std::clamp(std::stod(gap), 0.0, 2.0);
                meta.loopable = loop == "1";
                catalog.emplace(std::move(name), meta);
            } catch (...) { }
        }
        std::ifstream tracks(assetDirectory / "music.tsv");
        for (std::string line; std::getline(tracks, line);) {
            std::istringstream row(line);
            std::string name, frames, begin, end, bpm;
            if (!std::getline(row, name, '\t') || !SafeName(name) ||
                !std::getline(row, frames, '\t') || !std::getline(row, begin, '\t') || !std::getline(row, end, '\t') ||
                !std::getline(row, bpm, '\t')) continue;
            try { musicCatalog.emplace(std::move(name), MusicMetadata{std::stoull(frames), std::stoull(begin), std::stoull(end), std::stod(bpm)}); }
            catch (...) { }
        }
    }

    double Now() const {
        if (!engineInitialized) return 0;
        return static_cast<double>(ma_engine_get_time_in_pcm_frames(const_cast<ma_engine*>(&engine))) /
            std::max(1u, ma_engine_get_sample_rate(const_cast<ma_engine*>(&engine)));
    }
    double RandomUnit() {
        randomState ^= randomState >> 12; randomState ^= randomState << 25; randomState ^= randomState >> 27;
        return static_cast<double>((randomState * 2685821657736338717ull) >> 11) / 9007199254740992.0;
    }
    std::optional<Clip> ReadClip(std::string_view name, bool loop) {
        const auto found = catalog.find(std::string(name));
        if (!SafeName(name) || found == catalog.end() || (loop && !found->second.loopable)) {
            error = "Unknown audio clip: " + std::string(name);
            return std::nullopt;
        }
        const auto path = assetDirectory / (loop ? "loops" : "") / (std::string(name) + ".wav");
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) { error = "Audio clip not found: " + path.string(); return std::nullopt; }
        const auto end = file.tellg();
        if (end < 12 || static_cast<std::uint64_t>(end) > std::numeric_limits<std::uint32_t>::max()) {
            error = "Invalid WAV length: " + path.string(); return std::nullopt;
        }
        std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
        file.seekg(0); file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!file || std::memcmp(bytes.data(), "RIFF", 4) || std::memcmp(bytes.data() + 8, "WAVE", 4)) {
            error = "Invalid RIFF/WAVE file: " + path.string(); return std::nullopt;
        }
        std::uint16_t format = 0, channels = 0, bits = 0;
        std::uint32_t rate = 0, dataOffset = 0, dataLength = 0;
        for (std::size_t pos = 12; pos + 8 <= bytes.size();) {
            const auto length = U32(bytes.data() + pos + 4);
            const std::size_t payload = pos + 8;
            if (payload + length > bytes.size()) break;
            if (!std::memcmp(bytes.data() + pos, "fmt ", 4) && length >= 16) {
                format = U16(bytes.data() + payload); channels = U16(bytes.data() + payload + 2);
                rate = U32(bytes.data() + payload + 4); bits = U16(bytes.data() + payload + 14);
            } else if (!std::memcmp(bytes.data() + pos, "data", 4)) {
                dataOffset = static_cast<std::uint32_t>(payload); dataLength = length;
            }
            pos = payload + length + (length & 1u);
        }
        if (format != 1 || bits != 16 || (channels != 1 && channels != 2) || rate != 48000 || dataLength == 0 ||
            dataOffset + dataLength > bytes.size() || dataLength % (channels * 2) != 0) {
            error = "Expected non-empty 48 kHz PCM16 mono/stereo WAV: " + path.string(); return std::nullopt;
        }
        Clip clip;
        clip.metadata.name = std::string(name); clip.metadata.sampleRate = rate; clip.metadata.channels = channels;
        clip.metadata.bitsPerSample = bits; clip.metadata.frames = dataLength / (channels * 2);
        clip.metadata.dataBytes = dataLength; clip.metadata.durationSeconds = static_cast<double>(clip.metadata.frames) / rate;
        clip.metadata.sourceMaxVoices = found->second.maxVoices; clip.metadata.sourcePitchJitter = found->second.jitter;
        clip.metadata.sourceMinGapSeconds = found->second.minGap; clip.metadata.loopable = found->second.loopable;
        clip.pcm.resize(dataLength / 2); std::memcpy(clip.pcm.data(), bytes.data() + dataOffset, dataLength);
        double sum = 0, peak = 0;
        for (const auto sample : clip.pcm) { const double x = static_cast<double>(sample) / 32768.0; peak = std::max(peak, std::abs(x)); sum += x * x; }
        clip.metadata.peak = peak; clip.metadata.rms = std::sqrt(sum / clip.pcm.size());
        return clip;
    }
    Clip* FindClip(std::string_view name, bool loop = false) {
        const auto key = (loop ? "loop:" : "") + std::string(name);
        auto it = clips.find(key);
        if (it != clips.end()) return &it->second;
        auto clip = ReadClip(name, loop);
        if (!clip) return nullptr;
        return &clips.emplace(key, std::move(*clip)).first->second;
    }
    void StopVoice(Voice& voice) {
        if (voice.soundInitialized) { ma_sound_stop(&voice.sound); ma_sound_uninit(&voice.sound); voice.soundInitialized = false; }
        if (voice.sourceInitialized) { ma_audio_buffer_ref_uninit(&voice.source); voice.sourceInitialized = false; }
        if (!voice.loopKey.empty()) {
            const auto it = loopSlots.find(voice.loopKey);
            if (it != loopSlots.end() && it->second == static_cast<std::size_t>(&voice - voices.data())) loopSlots.erase(it);
        }
        voice.active = voice.stopping = false; voice.serial = 0; voice.stopAt = 0; voice.name.clear(); voice.loopKey.clear();
    }
    void StopMusicVoice(MusicVoice& voice) {
        if (voice.initialized) { ma_sound_stop(&voice.sound); ma_sound_uninit(&voice.sound); voice.initialized = false; }
        if (voice.sourceInitialized) { ma_audio_buffer_ref_uninit(&voice.source); voice.sourceInitialized = false; }
        voice.pcm.clear();
        voice.active = false; voice.looping = true; voice.track.clear(); voice.stopAt = 0;
    }
    bool PlayMusicRiser(double duration) {
        constexpr double sampleRate = 48000.0, pi = 3.14159265358979323846;
        duration = std::clamp(std::isfinite(duration) ? duration : 0.0, 0.27, 1.70);
        const auto frames = static_cast<ma_uint64>(std::ceil((duration + 0.06) * sampleRate));
        StopMusicVoice(transitionRiser);
        auto& voice = transitionRiser;
        voice.pcm.resize(static_cast<std::size_t>(frames) * 2);
        const auto& noise = SourceWhiteNoise();
        std::uint32_t noiseState = 1234;
        const auto noiseOffset = static_cast<std::size_t>(MulberryUnit(noiseState) * 1.9f * sampleRate);
        double phase = 0;
        BiquadState bandpass, lowpass, highpass;
        const double hpW = 2.0 * pi * 30.0 / sampleRate, hpCos = std::cos(hpW), hpAlpha = std::sin(hpW) / (2.0 * 0.707);
        const double hpA0 = 1.0 + hpAlpha;
        const double hpB0 = (1.0 + hpCos) * 0.5 / hpA0, hpB1 = -(1.0 + hpCos) / hpA0, hpB2 = hpB0;
        const double hpA1 = -2.0 * hpCos / hpA0, hpA2 = (1.0 - hpAlpha) / hpA0;
        for (ma_uint64 i = 0; i < frames; ++i) {
            const double t = static_cast<double>(i) / sampleRate;
            const double sweepT = std::min(1.0, t / duration);
            const double noiseHz = 400.0 * std::pow(7000.0 / 400.0, sweepT);
            const double bpW = 2.0 * pi * noiseHz / sampleRate, bpCos = std::cos(bpW), bpAlpha = std::sin(bpW) / (2.0 * 2.5);
            const double bpA0 = 1.0 + bpAlpha, bpB0 = bpAlpha / bpA0, bpB2 = -bpB0;
            const double bpA1 = -2.0 * bpCos / bpA0, bpA2 = (1.0 - bpAlpha) / bpA0;
            const double noiseEnvelope = t <= duration * 0.9 ? t / (duration * 0.9) :
                t <= duration ? 1.0 - 2.0 * (t - duration * 0.9) / duration :
                std::max(0.0, 0.8 * (1.0 - (t - duration) / 0.02));
            const double noiseSample = noise[(noiseOffset + static_cast<std::size_t>(i)) % noise.size()];
            const double noiseValue = bandpass.Process(noiseSample, bpB0, 0, bpB2, bpA1, bpA2) * noiseEnvelope;

            const double oscillatorHz = 220.0 * std::pow(1400.0 / 220.0, sweepT);
            const double saw = 2.0 * (phase - std::floor(phase)) - 1.0;
            phase += oscillatorHz / sampleRate;
            const double sawEnvelope = t <= duration ? 0.08 * t / duration : std::max(0.0, 0.08 * (1.0 - (t - duration) / 0.02));
            const double lpW = 2.0 * pi * 3000.0 / sampleRate, lpCos = std::cos(lpW), lpAlpha = std::sin(lpW) / 2.0;
            const double lpA0 = 1.0 + lpAlpha, lpB0 = (1.0 - lpCos) * 0.5 / lpA0;
            const double lpB1 = (1.0 - lpCos) / lpA0, lpB2 = lpB0;
            const double lpA1 = -2.0 * lpCos / lpA0, lpA2 = (1.0 - lpAlpha) / lpA0;
            const double sawValue = lowpass.Process(saw, lpB0, lpB1, lpB2, lpA1, lpA2) * sawEnvelope;

            // The source sends both voices through the music mix, −18 dB/3:1 compressor,
            // 0.42 music output, then the app's glue/limiter master chain.
            double sample = highpass.Process((noiseValue + sawValue) * 0.22, hpB0, hpB1, hpB2, hpA1, hpA2);
            sample = CompressDb(sample, -18.0, 10.0, 3.0) * 0.42;
            sample = CompressDb(sample, -12.0, 8.0, 3.0);
            sample = CompressDb(sample, -2.0, 0.0, 20.0);
            const auto pcm = static_cast<std::int16_t>(std::clamp(sample, -1.0, 1.0) * 32767.0);
            voice.pcm[static_cast<std::size_t>(i) * 2] = pcm;
            voice.pcm[static_cast<std::size_t>(i) * 2 + 1] = pcm;
        }
        ma_result result = ma_audio_buffer_ref_init(ma_format_s16, 2, voice.pcm.data(), frames, &voice.source);
        if (result != MA_SUCCESS) { voice.pcm.clear(); error = MiniaudioError("ma_audio_buffer_ref_init(music riser)", result); return false; }
        voice.source.sampleRate = static_cast<ma_uint32>(sampleRate); voice.sourceInitialized = true;
        result = ma_sound_init_from_data_source(&engine, reinterpret_cast<ma_data_source*>(&voice.source),
            MA_SOUND_FLAG_NO_SPATIALIZATION, &music, &voice.sound);
        if (result != MA_SUCCESS) { error = MiniaudioError("ma_sound_init_from_data_source(music riser)", result); StopMusicVoice(voice); return false; }
        voice.initialized = voice.active = true;
        transitionRiser.looping = false;
        voice.track = "source-riser";
        voice.serial = ++serial;
        ma_sound_set_volume(&voice.sound, 1.0f);
        const auto startFrame = ma_engine_get_time_in_pcm_frames(&engine) +
            static_cast<ma_uint64>(ma_engine_get_sample_rate(&engine) * 0.02);
        ma_sound_set_start_time_in_pcm_frames(&voice.sound, startFrame);
        const ma_result startResult = ma_sound_start(&voice.sound);
        if (startResult != MA_SUCCESS) {
            error = MiniaudioError("ma_sound_start(music riser)", startResult);
            StopMusicVoice(voice);
            return false;
        }
        return true;
    }
    void Reap() {
        const double now = Now();
        for (auto& voice : voices) if (voice.active && (voice.stopping ? now >= voice.stopAt : ma_sound_at_end(&voice.sound))) StopVoice(voice);
        for (auto& voice : musicVoices) if (voice.active && voice.stopAt > 0 && now >= voice.stopAt) StopMusicVoice(voice);
        if (transitionRiser.active && ma_sound_at_end(&transitionRiser.sound)) StopMusicVoice(transitionRiser);
    }
    void Shutdown() {
        for (auto& voice : voices) StopVoice(voice);
        for (auto& voice : musicVoices) StopMusicVoice(voice);
        StopMusicVoice(transitionRiser);
        if (sfxInitialized) { ma_sound_group_uninit(&sfx); sfxInitialized = false; }
        if (musicInitialized) { ma_sound_group_uninit(&music); musicInitialized = false; }
        if (engineInitialized) { ma_engine_uninit(&engine); engineInitialized = false; }
        initialized = false; currentMusic = kMusicVoices; pendingMusic.reset(); atMusicBoundary = false; musicDuckUntil = 0; musicDucked = false;
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
    if (result != MA_SUCCESS) { impl_->error = MiniaudioError("ma_engine_init", result); impl_->Shutdown(); return false; }
    impl_->engineInitialized = true;
    result = ma_sound_group_init(&impl_->engine, 0, nullptr, &impl_->sfx);
    if (result != MA_SUCCESS) { impl_->error = MiniaudioError("ma_sound_group_init(SFX)", result); impl_->Shutdown(); return false; }
    impl_->sfxInitialized = true;
    result = ma_sound_group_init(&impl_->engine, 0, nullptr, &impl_->music);
    if (result != MA_SUCCESS) { impl_->error = MiniaudioError("ma_sound_group_init(music)", result); impl_->Shutdown(); return false; }
    impl_->musicInitialized = true; impl_->initialized = true;
    SetVolumes(impl_->volume);
    return true;
}

bool AudioEngine::Play(std::string_view name, double volume, AudioBus bus) {
    AudioPlayOptions options; options.volume = volume; options.bus = bus;
    return Play(name, options);
}

bool AudioEngine::Play(std::string_view name, const AudioPlayOptions& options) {
    auto* clip = impl_->FindClip(name);
    if (!clip) return false;
    if (!impl_->initialized || !std::isfinite(options.volume) || options.volume <= 0 ||
        !std::isfinite(options.pitch) || options.pitch <= 0 ||
        VolumeGain(impl_->volume.master) == 0 ||
        VolumeGain(options.bus == AudioBus::Music ? impl_->volume.music : impl_->volume.sfx) == 0) return false;
    impl_->Reap();
    const double now = impl_->Now();
    const auto& meta = impl_->catalog.at(std::string(name));
    if (const auto it = impl_->lastPlayed.find(std::string(name)); it != impl_->lastPlayed.end() && now - it->second < meta.minGap) return false;
    std::vector<Impl::Voice*> same;
    std::size_t active = 0;
    for (auto& voice : impl_->voices) if (voice.active && voice.loopKey.empty()) {
        ++active;
        if (voice.name == name) same.push_back(&voice);
    }
    if (same.size() >= meta.maxVoices) {
        auto oldest = *std::min_element(same.begin(), same.end(), [](const auto* a, const auto* b) { return a->serial < b->serial; });
        impl_->StopVoice(*oldest); ++impl_->stolen; --active;
    }
    Impl::Voice* slot = nullptr;
    if (active >= kVoiceLimit) {
        for (auto& voice : impl_->voices) if (voice.active && voice.loopKey.empty() && (!slot || voice.serial < slot->serial)) slot = &voice;
        if (slot) { impl_->StopVoice(*slot); ++impl_->stolen; }
    }
    if (!slot) for (std::size_t i = 0; i < kVoiceLimit; ++i) if (!impl_->voices[i].active) { slot = &impl_->voices[i]; break; }
    if (!slot) return false;
    ma_result result = ma_audio_buffer_ref_init(ma_format_s16, clip->metadata.channels, clip->pcm.data(), clip->metadata.frames, &slot->source);
    if (result != MA_SUCCESS) { impl_->error = MiniaudioError("ma_audio_buffer_ref_init", result); impl_->StopVoice(*slot); return false; }
    slot->source.sampleRate = clip->metadata.sampleRate; slot->sourceInitialized = true;
    const ma_uint32 flags = options.pos ? 0 : MA_SOUND_FLAG_NO_SPATIALIZATION;
    auto* group = options.bus == AudioBus::Music ? &impl_->music : &impl_->sfx;
    result = ma_sound_init_from_data_source(&impl_->engine, reinterpret_cast<ma_data_source*>(&slot->source), flags, group, &slot->sound);
    if (result != MA_SUCCESS) { impl_->error = MiniaudioError("ma_sound_init_from_data_source", result); impl_->StopVoice(*slot); return false; }
    slot->soundInitialized = true;
    const double jitter = (impl_->RandomUnit() * 2 - 1) * meta.jitter;
    ma_sound_set_volume(&slot->sound, static_cast<float>(std::clamp(options.volume, 0.0, 1.0)));
    ma_sound_set_pitch(&slot->sound, static_cast<float>(std::clamp(options.pitch * (1 + jitter), 0.05, 4.0)));
    if (options.pos) ma_sound_set_position(&slot->sound, static_cast<float>(options.pos->x), static_cast<float>(options.pos->y), static_cast<float>(options.pos->z));
    result = ma_sound_start(&slot->sound);
    if (result != MA_SUCCESS) { impl_->error = MiniaudioError("ma_sound_start", result); impl_->StopVoice(*slot); return false; }
    slot->active = true; slot->name = std::string(name); slot->serial = ++impl_->serial;
    impl_->lastPlayed[std::string(name)] = now;
    return true;
}

bool AudioEngine::StartLoop(std::string_view key, std::string_view name, const AudioLoopOptions& options) {
    auto* clip = impl_->FindClip(name, true);
    if (!clip) return false;
    if (!impl_->initialized || !std::isfinite(options.volume) || options.volume < 0 ||
        !std::isfinite(options.pitch) || options.pitch <= 0) return false;
    if (const auto it = impl_->loopSlots.find(std::string(key)); it != impl_->loopSlots.end()) {
        auto& existing = impl_->voices[it->second];
        if (existing.active && existing.name == name) return UpdateLoop(key, options);
        impl_->StopVoice(existing);
    }
    impl_->Reap();
    std::size_t loopCount = 0;
    for (std::size_t i = kVoiceLimit; i < impl_->voices.size(); ++i) loopCount += impl_->voices[i].active;
    if (loopCount >= kLoopLimit) {
        auto oldest = std::min_element(impl_->voices.begin() + kVoiceLimit, impl_->voices.end(), [](const auto& a, const auto& b) {
            if (!a.active) return false; if (!b.active) return true; return a.serial < b.serial;
        });
        if (oldest != impl_->voices.end() && oldest->active) impl_->StopVoice(*oldest);
    }
    auto slot = std::find_if(impl_->voices.begin() + kVoiceLimit, impl_->voices.end(), [](const auto& v) { return !v.active; });
    if (slot == impl_->voices.end()) return false;
    ma_result result = ma_audio_buffer_ref_init(ma_format_s16, clip->metadata.channels, clip->pcm.data(), clip->metadata.frames, &slot->source);
    if (result != MA_SUCCESS) { impl_->error = MiniaudioError("ma_audio_buffer_ref_init(loop)", result); impl_->StopVoice(*slot); return false; }
    slot->source.sampleRate = clip->metadata.sampleRate; slot->sourceInitialized = true;
    result = ma_sound_init_from_data_source(&impl_->engine, reinterpret_cast<ma_data_source*>(&slot->source),
        options.pos ? 0 : MA_SOUND_FLAG_NO_SPATIALIZATION, &impl_->sfx, &slot->sound);
    if (result != MA_SUCCESS) { impl_->error = MiniaudioError("ma_sound_init_from_data_source(loop)", result); impl_->StopVoice(*slot); return false; }
    slot->soundInitialized = true; ma_sound_set_looping(&slot->sound, MA_TRUE);
    ma_sound_set_volume(&slot->sound, static_cast<float>(std::clamp(options.volume, 0.0, 1.0)));
    ma_sound_set_pitch(&slot->sound, static_cast<float>(std::clamp(options.pitch, 0.05, 4.0)));
    if (options.pos) ma_sound_set_position(&slot->sound, static_cast<float>(options.pos->x), static_cast<float>(options.pos->y), static_cast<float>(options.pos->z));
    result = ma_sound_start(&slot->sound);
    if (result != MA_SUCCESS) { impl_->error = MiniaudioError("ma_sound_start(loop)", result); impl_->StopVoice(*slot); return false; }
    slot->active = true; slot->name = std::string(name); slot->loopKey = std::string(key); slot->serial = ++impl_->serial;
    impl_->loopSlots.emplace(slot->loopKey, static_cast<std::size_t>(std::distance(impl_->voices.begin(), slot)));
    return true;
}

bool AudioEngine::UpdateLoop(std::string_view key, const AudioLoopOptions& options) {
    // shortcut: source loop parameters are absent from native events, upgrade when the simulation exposes those controls.
    if (!std::isfinite(options.volume) || options.volume < 0 || !std::isfinite(options.pitch) || options.pitch <= 0) return false;
    const auto it = impl_->loopSlots.find(std::string(key));
    if (it == impl_->loopSlots.end()) return false;
    auto& voice = impl_->voices[it->second];
    if (!voice.active || !voice.soundInitialized) return false;
    voice.stopping = false; voice.stopAt = 0;
    ma_sound_set_volume(&voice.sound, static_cast<float>(std::clamp(options.volume, 0.0, 1.0)));
    ma_sound_set_pitch(&voice.sound, static_cast<float>(std::clamp(options.pitch, 0.05, 4.0)));
    if (options.pos) ma_sound_set_position(&voice.sound, static_cast<float>(options.pos->x), static_cast<float>(options.pos->y), static_cast<float>(options.pos->z));
    return true;
}

void AudioEngine::StopLoop(std::string_view key, double fadeSeconds) {
    const auto it = impl_->loopSlots.find(std::string(key));
    if (it == impl_->loopSlots.end()) return;
    auto& voice = impl_->voices[it->second];
    if (!voice.active || !voice.soundInitialized) { impl_->StopVoice(voice); return; }
    const double fade = std::clamp(std::isfinite(fadeSeconds) ? fadeSeconds : 0.0, 0.0, 10.0);
    if (fade == 0) { impl_->StopVoice(voice); return; }
    ma_sound_set_fade_in_milliseconds(&voice.sound, -1, 0, static_cast<ma_uint64>(std::ceil(fade * 1000)));
    voice.stopping = true; voice.stopAt = impl_->Now() + fade;
    impl_->loopSlots.erase(it);
}

void AudioEngine::SetListener(AudioPosition position, AudioPosition forward, AudioPosition up) {
    if (!impl_->engineInitialized) return;
    const auto finite = [](AudioPosition p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); };
    if (!finite(position)) return;
    if (!finite(forward) || (forward.x == 0 && forward.y == 0 && forward.z == 0)) forward = {0, 0, -1};
    if (!finite(up) || (up.x == 0 && up.y == 0 && up.z == 0)) up = {0, 1, 0};
    ma_engine_listener_set_position(&impl_->engine, 0, static_cast<float>(position.x), static_cast<float>(position.y), static_cast<float>(position.z));
    ma_engine_listener_set_direction(&impl_->engine, 0, static_cast<float>(forward.x), static_cast<float>(forward.y), static_cast<float>(forward.z));
    ma_engine_listener_set_world_up(&impl_->engine, 0, static_cast<float>(up.x), static_cast<float>(up.y), static_cast<float>(up.z));
}

bool AudioEngine::PlayMusic(std::string_view track, double fadeSeconds) {
    if (!impl_->initialized || !SafeName(track)) { impl_->error = "Unknown music track: " + std::string(track); return false; }
    const auto meta = impl_->musicCatalog.find(std::string(track));
    if (meta == impl_->musicCatalog.end()) { impl_->error = "Unknown music track: " + std::string(track); return false; }
    const double fade = std::clamp(std::isfinite(fadeSeconds) ? fadeSeconds : 0.0, 0.0, 10.0);
    if (impl_->pendingMusic && impl_->pendingMusic->track == track) return true;
    if (impl_->currentMusic < kMusicVoices && impl_->musicVoices[impl_->currentMusic].active && impl_->musicVoices[impl_->currentMusic].track == track) {
        impl_->pendingMusic.reset();
        impl_->StopMusicVoice(impl_->transitionRiser);
        return true;
    }
    impl_->pendingMusic.reset();
    impl_->StopMusicVoice(impl_->transitionRiser);
    if (!impl_->atMusicBoundary && fade > 0 && impl_->currentMusic < kMusicVoices) {
        auto& current = impl_->musicVoices[impl_->currentMusic];
        const auto currentMeta = impl_->musicCatalog.find(current.track);
        ma_uint64 cursor = 0;
        if (current.active && currentMeta != impl_->musicCatalog.end() &&
            currentMeta->second.bpm > 0 && currentMeta->second.bpm == meta->second.bpm &&
            ma_sound_get_cursor_in_pcm_frames(&current.sound, &cursor) == MA_SUCCESS) {
            const ma_uint64 sampleRate = ma_engine_get_sample_rate(&impl_->engine);
            const double barFrames = static_cast<double>(sampleRate) * 240.0 / meta->second.bpm;
            constexpr ma_uint64 sourceGridOffset = 2880; // music.js schedules its first bar at now + 60 ms.
            const double target = static_cast<double>(sourceGridOffset) +
                std::ceil((static_cast<double>(cursor) + sampleRate * 0.12 - sourceGridOffset) / barFrames) * barFrames;
            const double wait = std::max(0.0, (target - static_cast<double>(cursor)) / sampleRate);
            if (wait > 0.01) {
                if (wait > 0.3 && !impl_->PlayMusicRiser(wait - 0.03)) return false;
                impl_->pendingMusic = Impl::PendingMusic{std::string(track), fade, impl_->Now() + wait};
                return true;
            }
        }
    }
    const double fadeOut = impl_->atMusicBoundary ? std::min(0.25, fade) : fade;
    const double fadeIn = impl_->atMusicBoundary ? 0.008 : fade;
    std::size_t next = kMusicVoices;
    for (std::size_t i = 0; i < kMusicVoices; ++i) if (!impl_->musicVoices[i].active) { next = i; break; }
    if (next == kMusicVoices) next = impl_->currentMusic == 0 ? 1 : 0;
    auto& voice = impl_->musicVoices[next];
    impl_->StopMusicVoice(voice);
    auto path = impl_->assetDirectory / "music" / (std::string(track) + ".flac");
    const auto nativePath = path.wstring();
    ma_sound_config config = ma_sound_config_init_2(&impl_->engine);
    config.pFilePathW = nativePath.c_str(); config.pInitialAttachment = reinterpret_cast<ma_node*>(&impl_->music);
    config.flags = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION; config.isLooping = MA_TRUE;
    config.loopPointBegInPCMFrames = meta->second.loopBegin; config.loopPointEndInPCMFrames = meta->second.loopEnd;
    ma_result result = ma_sound_init_ex(&impl_->engine, &config, &voice.sound);
    if (result != MA_SUCCESS) { impl_->error = MiniaudioError("ma_sound_init_ex(music)", result); return false; }
    voice.initialized = true; voice.active = true; voice.track = std::string(track); voice.serial = ++impl_->serial;
    ma_sound_set_volume(&voice.sound, 1.0f);
    ma_sound_set_fade_in_milliseconds(&voice.sound, 0, 1, static_cast<ma_uint64>(std::ceil(fadeIn * 1000)));
    result = ma_sound_start(&voice.sound);
    if (result != MA_SUCCESS) { impl_->error = MiniaudioError("ma_sound_start(music)", result); impl_->StopMusicVoice(voice); return false; }
    if (impl_->currentMusic < kMusicVoices) {
        auto& old = impl_->musicVoices[impl_->currentMusic];
        ma_sound_set_fade_in_milliseconds(&old.sound, -1, 0, static_cast<ma_uint64>(std::ceil(fadeOut * 1000)));
        old.stopAt = impl_->Now() + fadeOut;
    }
    impl_->currentMusic = next;
    return true;
}

void AudioEngine::StopMusic(double fadeSeconds) {
    impl_->pendingMusic.reset();
    impl_->StopMusicVoice(impl_->transitionRiser);
    if (impl_->currentMusic >= kMusicVoices) return;
    auto& voice = impl_->musicVoices[impl_->currentMusic];
    if (!voice.active) { impl_->currentMusic = kMusicVoices; return; }
    const double fade = std::clamp(std::isfinite(fadeSeconds) ? fadeSeconds : 0.0, 0.0, 10.0);
    if (fade == 0) impl_->StopMusicVoice(voice);
    else { ma_sound_set_fade_in_milliseconds(&voice.sound, -1, 0, static_cast<ma_uint64>(std::ceil(fade * 1000))); voice.stopAt = impl_->Now() + fade; }
    impl_->currentMusic = kMusicVoices;
}

void AudioEngine::Duck(double amount, double seconds) {
    const auto target = static_cast<float>(VolumeGain(1.0 - ClampFinite(amount)));
    const double duration = std::clamp(std::isfinite(seconds) ? seconds : 0.0, 0.0, 10.0);
    if (impl_->musicInitialized) ma_sound_group_set_fade_in_milliseconds(&impl_->music, -1, target, 80);
    impl_->musicDucked = true; impl_->musicDuckUntil = impl_->Now() + 0.08 + duration;
}

void AudioEngine::Update() {
    if (impl_->pendingMusic && impl_->Now() >= impl_->pendingMusic->startAt) {
        auto pending = std::move(*impl_->pendingMusic);
        impl_->pendingMusic.reset();
        impl_->atMusicBoundary = true;
        PlayMusic(pending.track, pending.fade);
        impl_->atMusicBoundary = false;
    }
    impl_->Reap();
    if (impl_->musicDucked && impl_->Now() >= impl_->musicDuckUntil) {
        if (impl_->musicInitialized) ma_sound_group_set_fade_in_milliseconds(&impl_->music, -1, 1, 600);
        impl_->musicDucked = false;
    }
}

void AudioEngine::SetVolumes(double master, double music, double sfx) {
    impl_->volume.master = ClampFinite(master); impl_->volume.music = ClampFinite(music); impl_->volume.sfx = ClampFinite(sfx);
    if (impl_->engineInitialized) ma_engine_set_volume(&impl_->engine, static_cast<float>(VolumeGain(impl_->volume.master)));
    if (impl_->musicInitialized) ma_sound_group_set_volume(&impl_->music, static_cast<float>(VolumeGain(impl_->volume.music)));
    if (impl_->sfxInitialized) ma_sound_group_set_volume(&impl_->sfx, static_cast<float>(VolumeGain(impl_->volume.sfx)));
}

void AudioEngine::Clear() {
    impl_->pendingMusic.reset();
    for (auto& voice : impl_->voices) impl_->StopVoice(voice);
    for (auto& voice : impl_->musicVoices) impl_->StopMusicVoice(voice);
    impl_->StopMusicVoice(impl_->transitionRiser);
    impl_->currentMusic = kMusicVoices;
}

std::optional<AudioClipMetadata> AudioEngine::Metadata(std::string_view name) {
    auto* clip = impl_->FindClip(name);
    if (!clip) return std::nullopt;
    return clip->metadata;
}

std::vector<std::string> AudioEngine::Names() const {
    std::vector<std::string> names; names.reserve(impl_->catalog.size());
    for (const auto& [name, unused] : impl_->catalog) names.push_back(name);
    std::sort(names.begin(), names.end()); return names;
}

AudioVolumes AudioEngine::volumes() const { return impl_->volume; }
std::size_t AudioEngine::ActiveVoices() {
    impl_->Reap();
    return static_cast<std::size_t>(std::count_if(impl_->voices.begin(), impl_->voices.end(), [](const auto& voice) { return voice.active; }));
}
std::uint64_t AudioEngine::stolenVoices() const { return impl_->stolen; }
bool AudioEngine::ready() const { return impl_->initialized; }
const std::string& AudioEngine::lastError() const { return impl_->error; }

} // namespace restoration
