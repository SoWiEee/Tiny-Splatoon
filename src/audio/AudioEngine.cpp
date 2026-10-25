#include "AudioEngine.hpp"
#include "NativeReverb.hpp"
#include "ZoneHum.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <random>
#include <sstream>
#include <system_error>
#include <thread>
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
double Distance(AudioPosition a, AudioPosition b) {
    const double x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
    return std::sqrt(x * x + y * y + z * z);
}
double ReverbSendScale(double distance) {
    return std::sqrt(3.0 / (3.0 + 1.2 * std::max(0.0, distance - 3.0)));
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
struct HumSource {
    ma_data_source_base base{};
    ZoneHum synth;
    ma_uint64 cursor=0;
    static ma_result Read(ma_data_source* source,void* output,ma_uint64 frames,ma_uint64* read){
        auto& self=*reinterpret_cast<HumSource*>(source);self.synth.Render(static_cast<float*>(output),static_cast<std::size_t>(frames));self.cursor+=frames;if(read)*read=frames;return MA_SUCCESS;
    }
    static ma_result Seek(ma_data_source*,ma_uint64){return MA_NOT_IMPLEMENTED;}
    static ma_result Format(ma_data_source*,ma_format* format,ma_uint32* channels,ma_uint32* rate,ma_channel* map,size_t capacity){
        if(format)*format=ma_format_f32;if(channels)*channels=1;if(rate)*rate=48000;if(map&&capacity)map[0]=MA_CHANNEL_MONO;return MA_SUCCESS;
    }
    static ma_result Cursor(ma_data_source* source,ma_uint64* cursor){if(cursor)*cursor=reinterpret_cast<HumSource*>(source)->cursor;return MA_SUCCESS;}
    static ma_result Length(ma_data_source*,ma_uint64* length){if(length)*length=0;return MA_NOT_IMPLEMENTED;}
    HumSource(){
        static const ma_data_source_vtable table={Read,Seek,Format,Cursor,Length,nullptr,0};
        auto config=ma_data_source_config_init();config.vtable=&table;
        const auto result=ma_data_source_init(&config,&base);if(result!=MA_SUCCESS)throw std::runtime_error(MiniaudioError("zone hum source",result));
    }
    ~HumSource(){ma_data_source_uninit(&base);}
};
}

struct AudioEngine::Impl {
    struct SourceMetadata { std::uint32_t maxVoices = 6; double jitter = 0.06, minGap = 0.018; bool loopable = false; };
    struct MusicMetadata { std::uint64_t frames = 0, loopBegin = 0, loopEnd = 0; double bpm = 0; };
    struct PendingMusic { std::string track; double fade = 0, startAt = 0; };
    struct Clip { AudioClipMetadata metadata; std::vector<std::int16_t> pcm; };
    struct Voice {
        ma_audio_buffer_ref source{};
        ma_sound sound{};
        NativeReverbSend reverbSend;
        bool sourceInitialized = false, soundInitialized = false, active = false, stopping = false;
        std::string name, loopKey;
        std::uint64_t serial = 0;
        double stopAt = 0;
        int reverbSlot = -1;
        double reverbBaseSend = 0;
        AudioPosition position{};
        bool hasPosition = false;
        std::unique_ptr<HumSource> hum;
    };
    struct MusicVoice {
        ma_sound sound{};
        ma_audio_buffer_ref source{};
        std::vector<std::int16_t> pcm;
        bool initialized = false, active = false, looping = true;
        bool sourceInitialized = false, recording = false;
        std::string track;
        std::filesystem::path recordingPath;
        double recordingGain = 1;
        ma_uint64 loopBegin = MA_DATA_SOURCE_DEFAULT_LOOP_POINT_BEG;
        ma_uint64 loopEnd = MA_DATA_SOURCE_DEFAULT_LOOP_POINT_END;
        std::uint64_t serial = 0;
        double stopAt = 0;
    };

    explicit Impl(std::filesystem::path root) : assetDirectory(std::move(root)) { LoadCatalog(); }
    ~Impl() { Shutdown(); }

    std::filesystem::path assetDirectory;
    std::unordered_map<std::string, SourceMetadata> catalog;
    std::unordered_map<std::string, MusicMetadata> musicCatalog;
    struct Recording {std::filesystem::path path;double gain=1;bool loop=false;};
    std::unordered_map<std::string,std::vector<Recording>> recordings;
    std::unordered_map<std::string,std::size_t> recordingNext;
    std::unordered_map<std::string,std::filesystem::path> recordingLast;
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
    NativeReverb reverb;
    bool reverbReady = false;
    std::array<bool, NativeReverb::kSendCount> reverbSendUsed{};
    AudioPosition listenerPosition{};
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
        std::ifstream files(assetDirectory/"custom-music.tsv");
        std::unordered_map<std::string,std::vector<Recording>> customRecordings;
        for(std::string line;std::getline(files,line);){
            std::istringstream row(line);std::string track,gain,path;
            if(!std::getline(row,track,'\t')||(track!="battle"&&track!="battle_final")||!std::getline(row,gain,'\t')||!std::getline(row,path))continue;
            const auto p=std::filesystem::u8path(path);
            const auto extension=p.extension().string();
            if(path.find_first_of("\\\t\r\n")!=std::string::npos||p.is_absolute()||p.has_root_name()||p.has_root_directory()||
                p.parent_path()!=std::filesystem::path("songs")||!SafeName(p.stem().string())||
                (extension!=".wav"&&extension!=".flac"&&extension!=".mp3"))continue;
            try{
                std::size_t consumed=0;const double db=std::stod(gain,&consumed);
                if(consumed==gain.size()&&std::isfinite(db)&&db>=-60&&db<=24)
                    customRecordings[track].push_back({assetDirectory/p,std::pow(10.,db/20.),false});
            }catch(...){}
        }
        const std::array<std::pair<std::string_view, std::string_view>, 7> bundledTracks{{
                {"title", "LifeWillChange_Instrumental.mp3"}, {"menu", "LifeWillChange_Instrumental.mp3"},
                {"battle", "Splattack.mp3"}, {"battle_final", "Splattack.mp3"}, {"boss", "Splattack.mp3"},
                {"boss_2", "Splattack.mp3"}, {"boss_3", "Splattack.mp3"}}};
        std::ifstream bundled(assetDirectory / "bundled-music.tsv");
        for (std::string line; std::getline(bundled, line);) {
            std::istringstream row(line);
            std::string track, gain, filename;
            if (!std::getline(row, track, '\t') || !std::getline(row, gain, '\t') || !std::getline(row, filename)) continue;
            const auto mapping = std::find_if(bundledTracks.begin(), bundledTracks.end(), [&](const auto& item) {
                return item.first == track && item.second == filename;
            });
            if (mapping == bundledTracks.end() || !musicCatalog.count(track)) continue;
            const auto p = std::filesystem::u8path(filename);
            if (filename.find_first_of("\\\t\r\n") != std::string::npos || p.is_absolute() || p.has_root_name() ||
                p.has_root_directory() || !p.parent_path().empty() || !SafeName(p.stem().string()) || p.extension() != ".mp3") continue;
            double db = 0;
            try {
                std::size_t consumed = 0;
                db = std::stod(gain, &consumed);
                if (consumed != gain.size() || !std::isfinite(db) || db < -60 || db > 24) continue;
            } catch (...) { continue; }
            const auto path = assetDirectory / p;
            std::error_code ec;
            const auto root = std::filesystem::weakly_canonical(assetDirectory, ec);
            if (ec) continue;
            const auto resolved = std::filesystem::weakly_canonical(path, ec);
            if (ec) continue;
            const auto relative = resolved.lexically_relative(root);
            if (relative.empty() || relative.is_absolute() ||
                std::any_of(relative.begin(), relative.end(), [](const auto& part) { return part == ".."; })) continue;
            recordings[track].push_back({path, std::pow(10., db / 20.), true});
        }
        for (auto& [track, entries] : customRecordings) recordings[track] = std::move(entries);
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
        if (voice.reverbSlot >= 0) {
            const auto send = static_cast<std::size_t>(voice.reverbSlot);
            reverb.SetSendGain(send, 0.0f);
            voice.reverbSend.Shutdown();
            if (send < reverbSendUsed.size()) reverbSendUsed[send] = false;
            voice.reverbSlot = -1;
        } else {
            voice.reverbSend.Shutdown();
        }
        voice.hum.reset();
        if (!voice.loopKey.empty()) {
            const auto it = loopSlots.find(voice.loopKey);
            if (it != loopSlots.end() && it->second == static_cast<std::size_t>(&voice - voices.data())) loopSlots.erase(it);
        }
        voice.active = voice.stopping = false; voice.serial = 0; voice.stopAt = 0; voice.name.clear(); voice.loopKey.clear();
        voice.reverbBaseSend = 0; voice.position = {}; voice.hasPosition = false;
    }
    void StopMusicVoice(MusicVoice& voice) {
        if (voice.initialized) { ma_sound_stop(&voice.sound); ma_sound_uninit(&voice.sound); voice.initialized = false; }
        if (voice.sourceInitialized) { ma_audio_buffer_ref_uninit(&voice.source); voice.sourceInitialized = false; }
        voice.pcm.clear();
        voice.active = false; voice.looping = true; voice.recording=false;voice.track.clear();
        voice.recordingPath.clear(); voice.recordingGain = 1;
        voice.loopBegin = MA_DATA_SOURCE_DEFAULT_LOOP_POINT_BEG;
        voice.loopEnd = MA_DATA_SOURCE_DEFAULT_LOOP_POINT_END;
        voice.stopAt = 0;
    }
    bool PlayMusicRiser(double duration) {
        constexpr ma_uint32 sampleRate = 48000;
        constexpr double pi = 3.14159265358979323846;
        if (transitionRiser.active && !ma_sound_at_end(&transitionRiser.sound)) return true;
        const auto startFrame = ma_engine_get_time_in_pcm_frames(&engine) + static_cast<ma_uint64>(sampleRate * 0.02);
        duration = std::clamp(std::isfinite(duration) ? duration : 0.0, 0.27, 1.70);
        const auto synthFrames = static_cast<ma_uint64>(std::ceil((duration + 0.06) * sampleRate));
        StopMusicVoice(transitionRiser);
        auto& voice = transitionRiser;
        std::vector<float> monoInput(static_cast<std::size_t>(synthFrames), 0.0f);
        const auto& noise = SourceWhiteNoise();
        std::uint32_t noiseState = 1234;
        const auto noiseOffset = static_cast<std::size_t>(MulberryUnit(noiseState) * 1.9f * sampleRate);
        double phase = 0;
        BiquadState bandpass, lowpass;
        const double hpW = 2.0 * pi * 30.0 / sampleRate, hpCos = std::cos(hpW), hpAlpha = std::sin(hpW) / (2.0 * 0.707);
        const double hpA0 = 1.0 + hpAlpha;
        const double hpB0 = (1.0 + hpCos) * 0.5 / hpA0, hpB1 = -(1.0 + hpCos) / hpA0, hpB2 = hpB0;
        const double hpA1 = -2.0 * hpCos / hpA0, hpA2 = (1.0 - hpAlpha) / hpA0;
        for (ma_uint64 i = 0; i < synthFrames; ++i) {
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

            monoInput[static_cast<std::size_t>(i)] = static_cast<float>((noiseValue + sawValue) * 0.22);
        }

        const auto wet = NativeReverb::ConvolveMusicRiser(monoInput, sampleRate);
        if (wet.empty() || (wet.size() & 1u) != 0) {
            error = "Source music riser reverb render failed";
            return false;
        }
        const auto renderFrames = static_cast<ma_uint64>(wet.size() / 2);
        voice.pcm.resize(static_cast<std::size_t>(renderFrames) * 2);
        BiquadState highpassLeft, highpassRight;
        for (ma_uint64 i = 0; i < renderFrames; ++i) {
            const double dry = i < synthFrames ? monoInput[static_cast<std::size_t>(i)] : 0.0;
            double left = highpassLeft.Process(dry + wet[static_cast<std::size_t>(i) * 2], hpB0, hpB1, hpB2, hpA1, hpA2);
            double right = highpassRight.Process(dry + wet[static_cast<std::size_t>(i) * 2 + 1], hpB0, hpB1, hpB2, hpA1, hpA2);
            // The source sends the dry and wet mix through the music compressor,
            // music level, and then the app's glue/limiter master chain.
            left = CompressDb(left, -18.0, 10.0, 3.0) * 0.42;
            right = CompressDb(right, -18.0, 10.0, 3.0) * 0.42;
            left = CompressDb(left, -12.0, 8.0, 3.0);
            right = CompressDb(right, -12.0, 8.0, 3.0);
            left = CompressDb(left, -2.0, 0.0, 20.0);
            right = CompressDb(right, -2.0, 0.0, 20.0);
            voice.pcm[static_cast<std::size_t>(i) * 2] = static_cast<std::int16_t>(std::clamp(left, -1.0, 1.0) * 32767.0);
            voice.pcm[static_cast<std::size_t>(i) * 2 + 1] = static_cast<std::int16_t>(std::clamp(right, -1.0, 1.0) * 32767.0);
        }
        ma_result result = ma_audio_buffer_ref_init(ma_format_s16, 2, voice.pcm.data(), renderFrames, &voice.source);
        if (result != MA_SUCCESS) { voice.pcm.clear(); error = MiniaudioError("ma_audio_buffer_ref_init(music riser)", result); return false; }
        voice.source.sampleRate = sampleRate; voice.sourceInitialized = true;
        result = ma_sound_init_from_data_source(&engine, reinterpret_cast<ma_data_source*>(&voice.source),
            MA_SOUND_FLAG_NO_SPATIALIZATION, &music, &voice.sound);
        if (result != MA_SUCCESS) { error = MiniaudioError("ma_sound_init_from_data_source(music riser)", result); StopMusicVoice(voice); return false; }
        voice.initialized = voice.active = true;
        transitionRiser.looping = false;
        voice.track = "source-riser";
        voice.serial = ++serial;
        ma_sound_set_volume(&voice.sound, 1.0f);
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
        reverb.Shutdown();
        reverbReady = false;
        if (sfxInitialized) { ma_sound_group_uninit(&sfx); sfxInitialized = false; }
        if (musicInitialized) { ma_sound_group_uninit(&music); musicInitialized = false; }
        if (engineInitialized) { ma_engine_uninit(&engine); engineInitialized = false; }
        initialized = false; currentMusic = kMusicVoices; pendingMusic.reset(); atMusicBoundary = false; musicDuckUntil = 0; musicDucked = false;
    }
};

AudioEngine::AudioEngine(std::filesystem::path assetDirectory) : impl_(std::make_unique<Impl>(std::move(assetDirectory))) {}
AudioEngine::~AudioEngine() = default;

double AudioEngineTestTime(const AudioEngine& engine) { return engine.impl_->Now(); }

bool AudioEngineMusicTestState(const AudioEngine& engine, std::filesystem::path& path, bool& looping,
    std::uint64_t& loopBegin, std::uint64_t& loopEnd, std::uint64_t& serial, double& durationSeconds) {
    const auto& impl = *engine.impl_;
    if (impl.currentMusic >= kMusicVoices) return false;
    const auto& voice = impl.musicVoices[impl.currentMusic];
    if (!voice.active || !voice.recording || voice.recordingPath.empty()) return false;
    auto& sound = const_cast<ma_sound&>(voice.sound);
    float duration = 0;
    if (ma_sound_get_length_in_seconds(&sound, &duration) != MA_SUCCESS) return false;
    path = voice.recordingPath;
    looping = ma_sound_is_looping(&voice.sound) == MA_TRUE;
    loopBegin = voice.loopBegin;
    loopEnd = voice.loopEnd;
    serial = voice.serial;
    durationSeconds = duration;
    return true;
}

bool AudioEngineMusicTestReadsAcrossEnd(AudioEngine& engine) {
    auto& impl = *engine.impl_;
    if (impl.currentMusic >= kMusicVoices) return false;
    auto& voice = impl.musicVoices[impl.currentMusic];
    if (!voice.active || !voice.recording || !voice.looping || !voice.initialized) return false;

    // Stop the device as well so no in-flight callback can consume the test stream.
    if (ma_sound_stop(&voice.sound) != MA_SUCCESS || ma_engine_stop(&impl.engine) != MA_SUCCESS) return false;
    auto* source = ma_sound_get_data_source(&voice.sound);
    if (source == nullptr || ma_data_source_is_looping(source) != MA_TRUE) return false;

    ma_uint64 length = 0;
    if (ma_data_source_get_length_in_pcm_frames(source, &length) != MA_SUCCESS) return false;
    const ma_uint64 loopBegin = voice.loopBegin;
    const ma_uint64 loopEnd = voice.loopEnd == MA_DATA_SOURCE_DEFAULT_LOOP_POINT_END
        ? length
        : std::min<ma_uint64>(voice.loopEnd, length);
    constexpr ma_uint64 framesBeforeEnd = 8;
    constexpr ma_uint64 framesToRead = 32;
    if (loopEnd <= loopBegin || loopEnd - loopBegin <= framesBeforeEnd) return false;
    ma_format format{};ma_uint32 channels=0;
    if(ma_data_source_get_data_format(source,&format,&channels,nullptr,nullptr,0)!=MA_SUCCESS||channels==0||channels>32)return false;
    const auto bytesPerFrame=ma_get_bytes_per_sample(format)*channels;
    if(bytesPerFrame==0||bytesPerFrame>32*8)return false;
    std::array<ma_uint8,32*32*8> pcm{};

    // A streaming MP3 may need its end page loaded after the seek. Bound the wait,
    // preserve partial reads, and pass only after all requested frames cross the loop.
    bool positioned = false;
    ma_uint64 totalFramesRead = 0;
    for (int attempt = 0; attempt < 50; ++attempt) {
        if (!positioned) {
            const ma_result seekResult = ma_data_source_seek_to_pcm_frame(source, loopEnd - framesBeforeEnd);
            if (seekResult == MA_BUSY) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            if (seekResult != MA_SUCCESS) return false;
            positioned = true;
        }
        ma_uint64 framesRead = 0;
        const ma_result readResult = ma_data_source_read_pcm_frames(
            source, pcm.data()+totalFramesRead*bytesPerFrame, framesToRead - totalFramesRead, &framesRead);
        totalFramesRead += framesRead;
        if (totalFramesRead == framesToRead) {
            if (readResult != MA_SUCCESS) return false;
            ma_uint64 cursor = 0;
            return ma_data_source_get_cursor_in_pcm_frames(source, &cursor) == MA_SUCCESS &&
                cursor == loopBegin + framesToRead - framesBeforeEnd;
        }
        if (readResult != MA_BUSY && readResult != MA_SUCCESS) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return false;
}

double AudioEngine::VolumeGain(double value) {
    if (!std::isfinite(value)) return 0.0;
    return std::pow(std::clamp(value, 0.0, 1.0), 1.5);
}

bool AudioEngine::Initialize() {
    if (impl_->initialized) return true;
    impl_->error.clear();
    auto engineConfig = ma_engine_config_init();
    engineConfig.sampleRate = 48000;
    engineConfig.channels = 2;
    engineConfig.periodSizeInFrames = 256;
    ma_result result = ma_engine_init(&engineConfig, &impl_->engine);
    if (result != MA_SUCCESS) { impl_->error = MiniaudioError("ma_engine_init", result); impl_->Shutdown(); return false; }
    impl_->engineInitialized = true;
    result = ma_sound_group_init(&impl_->engine, 0, nullptr, &impl_->sfx);
    if (result != MA_SUCCESS) { impl_->error = MiniaudioError("ma_sound_group_init(SFX)", result); impl_->Shutdown(); return false; }
    impl_->sfxInitialized = true;
    result = impl_->reverb.Initialize(ma_engine_get_node_graph(&impl_->engine),
        reinterpret_cast<ma_node*>(&impl_->sfx), ma_engine_get_sample_rate(&impl_->engine));
    if (result == MA_SUCCESS) {
        impl_->reverbReady = true;
        impl_->reverbSendUsed.fill(false);
    } else {
        impl_->error = MiniaudioError("NativeReverb::Initialize", result);
    }
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
    ma_result result=MA_SUCCESS;ma_data_source* dataSource=nullptr;
    if(name=="zone_hum"){
        slot->hum=std::make_unique<HumSource>();
        if(options.zoneHum)slot->hum->synth.Set(options.zoneHum->mode,options.zoneHum->tension);
        dataSource=reinterpret_cast<ma_data_source*>(&slot->hum->base);
    }else{
        result=ma_audio_buffer_ref_init(ma_format_s16,clip->metadata.channels,clip->pcm.data(),clip->metadata.frames,&slot->source);
        if(result!=MA_SUCCESS){impl_->error=MiniaudioError("ma_audio_buffer_ref_init(loop)",result);impl_->StopVoice(*slot);return false;}
        slot->source.sampleRate=clip->metadata.sampleRate;slot->sourceInitialized=true;dataSource=reinterpret_cast<ma_data_source*>(&slot->source);
    }
    bool useReverbSend = false;
    if (name == "zone_hum" && impl_->reverbReady) {
        for (std::size_t send = 0; send < impl_->reverbSendUsed.size(); ++send) {
            if (!impl_->reverbSendUsed[send]) {
                impl_->reverbSendUsed[send] = true;
                slot->reverbSlot = static_cast<int>(send);
                break;
            }
        }
        if (slot->reverbSlot >= 0) {
            const AudioPosition position = options.pos.value_or(AudioPosition{});
            const ma_vec3f nativePosition{static_cast<float>(position.x), static_cast<float>(position.y), static_cast<float>(position.z)};
            result = slot->reverbSend.Initialize(ma_engine_get_node_graph(&impl_->engine), impl_->reverb.node(),
                static_cast<std::size_t>(slot->reverbSlot), reinterpret_cast<ma_node*>(&impl_->sfx),
                &impl_->engine.listeners[0], ma_engine_get_channels(&impl_->engine), impl_->engine.monoExpansionMode,
                ma_engine_get_sample_rate(&impl_->engine), options.pos ? MA_TRUE : MA_FALSE, nativePosition);
            if (result == MA_SUCCESS) {
                useReverbSend = true;
                slot->position = position;
                slot->hasPosition = options.pos.has_value();
                const double distance = options.pos ? Distance(position, impl_->listenerPosition) : 0.0;
                slot->reverbBaseSend = 0.12 + std::min(0.45, std::max(0.0, (distance - 4.0) * 0.025));
                impl_->reverb.SetSendGain(static_cast<std::size_t>(slot->reverbSlot),
                    static_cast<float>(slot->reverbBaseSend * ReverbSendScale(distance)));
            } else {
                slot->reverbSend.Shutdown();
                impl_->reverbSendUsed[static_cast<std::size_t>(slot->reverbSlot)] = false;
                slot->reverbSlot = -1;
                impl_->error = MiniaudioError("NativeReverbSend::Initialize", result);
            }
        }
    }
    if (useReverbSend) {
        auto config = ma_sound_config_init_2(&impl_->engine);
        config.pDataSource = dataSource;
        config.pInitialAttachment = slot->reverbSend.inputNode();
        config.flags = MA_SOUND_FLAG_NO_SPATIALIZATION;
        config.channelsOut = MA_SOUND_SOURCE_CHANNEL_COUNT;
        result = ma_sound_init_ex(&impl_->engine, &config, &slot->sound);
    } else {
        result = ma_sound_init_from_data_source(&impl_->engine, dataSource,
            options.pos ? 0 : MA_SOUND_FLAG_NO_SPATIALIZATION, &impl_->sfx, &slot->sound);
    }
    if (result != MA_SUCCESS) { impl_->error = MiniaudioError("ma_sound_init_from_data_source(loop)", result); impl_->StopVoice(*slot); return false; }
    slot->soundInitialized = true; ma_sound_set_looping(&slot->sound, MA_TRUE);
    ma_sound_set_volume(&slot->sound, static_cast<float>(std::clamp(options.volume, 0.0, 1.0)));
    ma_sound_set_pitch(&slot->sound, static_cast<float>(std::clamp(options.pitch, 0.05, 4.0)));
    if (options.pos && !useReverbSend) ma_sound_set_position(&slot->sound, static_cast<float>(options.pos->x), static_cast<float>(options.pos->y), static_cast<float>(options.pos->z));
    result = ma_sound_start(&slot->sound);
    if (result != MA_SUCCESS) { impl_->error = MiniaudioError("ma_sound_start(loop)", result); impl_->StopVoice(*slot); return false; }
    slot->active = true; slot->name = std::string(name); slot->loopKey = std::string(key); slot->serial = ++impl_->serial;
    impl_->loopSlots.emplace(slot->loopKey, static_cast<std::size_t>(std::distance(impl_->voices.begin(), slot)));
    return true;
}

bool AudioEngine::UpdateLoop(std::string_view key, const AudioLoopOptions& options) {
    if (!std::isfinite(options.volume) || options.volume < 0 || !std::isfinite(options.pitch) || options.pitch <= 0) return false;
    const auto it = impl_->loopSlots.find(std::string(key));
    if (it == impl_->loopSlots.end()) return false;
    auto& voice = impl_->voices[it->second];
    if (!voice.active || !voice.soundInitialized) return false;
    if(voice.hum&&options.zoneHum)voice.hum->synth.Set(options.zoneHum->mode,options.zoneHum->tension);
    voice.stopping = false; voice.stopAt = 0;
    ma_sound_set_volume(&voice.sound, static_cast<float>(std::clamp(options.volume, 0.0, 1.0)));
    ma_sound_set_pitch(&voice.sound, static_cast<float>(std::clamp(options.pitch, 0.05, 4.0)));
    if (options.pos) {
        if (voice.reverbSlot >= 0) {
            voice.position = *options.pos;
            voice.hasPosition = true;
            voice.reverbSend.SetPosition({static_cast<float>(options.pos->x), static_cast<float>(options.pos->y), static_cast<float>(options.pos->z)});
            voice.reverbSend.SetSpatialized(MA_TRUE);
            const double distance = Distance(voice.position, impl_->listenerPosition);
            impl_->reverb.SetSendGain(static_cast<std::size_t>(voice.reverbSlot),
                static_cast<float>(voice.reverbBaseSend * ReverbSendScale(distance)));
        } else {
            ma_sound_set_position(&voice.sound, static_cast<float>(options.pos->x), static_cast<float>(options.pos->y), static_cast<float>(options.pos->z));
        }
    }
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
    impl_->listenerPosition = position;
    ma_engine_listener_set_position(&impl_->engine, 0, static_cast<float>(position.x), static_cast<float>(position.y), static_cast<float>(position.z));
    ma_engine_listener_set_direction(&impl_->engine, 0, static_cast<float>(forward.x), static_cast<float>(forward.y), static_cast<float>(forward.z));
    ma_engine_listener_set_world_up(&impl_->engine, 0, static_cast<float>(up.x), static_cast<float>(up.y), static_cast<float>(up.z));
    for (auto& voice : impl_->voices) {
        if (!voice.active || voice.reverbSlot < 0) continue;
        const double distance = voice.hasPosition ? Distance(voice.position, position) : 0.0;
        impl_->reverb.SetSendGain(static_cast<std::size_t>(voice.reverbSlot),
            static_cast<float>(voice.reverbBaseSend * ReverbSendScale(distance)));
    }
}

bool AudioEngine::PlayMusic(std::string_view track, double fadeSeconds) {
    if (!impl_->initialized || !SafeName(track)) { impl_->error = "Unknown music track: " + std::string(track); return false; }
    const auto meta = impl_->musicCatalog.find(std::string(track));
    if (meta == impl_->musicCatalog.end()) { impl_->error = "Unknown music track: " + std::string(track); return false; }
    const double fade = std::clamp(std::isfinite(fadeSeconds) ? fadeSeconds : 0.0, 0.0, 10.0);
    if (impl_->pendingMusic && impl_->pendingMusic->track == track) return true;
    if (impl_->currentMusic < kMusicVoices && impl_->musicVoices[impl_->currentMusic].active && impl_->musicVoices[impl_->currentMusic].track == track) {
        impl_->pendingMusic.reset();
        return true;
    }
    const auto recordings = impl_->recordings.find(std::string(track));
    if (impl_->currentMusic < kMusicVoices) {
        const auto& current = impl_->musicVoices[impl_->currentMusic];
        if (current.active && current.recording && recordings != impl_->recordings.end() &&
            std::any_of(recordings->second.begin(), recordings->second.end(), [&](const auto& recording) {
                return recording.path == current.recordingPath && recording.gain == current.recordingGain &&
                    recording.loop && current.looping;
            })) {
            impl_->pendingMusic.reset();
            return true;
        }
    }
    impl_->pendingMusic.reset();
    if (!impl_->atMusicBoundary && fade > 0 && impl_->currentMusic < kMusicVoices) {
        auto& current = impl_->musicVoices[impl_->currentMusic];
        const auto currentMeta = impl_->musicCatalog.find(current.track);
        ma_uint64 cursor = 0;
        if (current.active && currentMeta != impl_->musicCatalog.end() &&
            !current.recording&&!impl_->recordings.count(std::string(track))&&currentMeta->second.bpm > 0 && currentMeta->second.bpm == meta->second.bpm &&
            ma_sound_get_cursor_in_pcm_frames(&current.sound, &cursor) == MA_SUCCESS) {
            const ma_uint64 sampleRate = ma_engine_get_sample_rate(&impl_->engine);
            const double barFrames = static_cast<double>(sampleRate) * 240.0 / meta->second.bpm;
            constexpr ma_uint64 sourceGridOffset = 2880; // music.js schedules its first bar at now + 60 ms.
            const double target = static_cast<double>(sourceGridOffset) +
                std::ceil((static_cast<double>(cursor) + sampleRate * 0.12 - sourceGridOffset) / barFrames) * barFrames;
            const double wait = std::max(0.0, (target - static_cast<double>(cursor)) / sampleRate);
            if (wait > 0.01) {
                const double transitionStartAt = impl_->Now() + wait;
                if (wait > 0.3 && !impl_->PlayMusicRiser(wait - 0.03)) return false;
                impl_->pendingMusic = Impl::PendingMusic{std::string(track), fade, transitionStartAt};
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
    double recordingGain=1; bool recordingLooping = true, recordingStarted = false;
    auto recordingPath = std::filesystem::path{};
    ma_uint64 loopBegin = meta->second.loopBegin, loopEnd = meta->second.loopEnd;
    if(recordings!=impl_->recordings.end()&&!recordings->second.empty()){
        if(result==MA_SUCCESS)ma_sound_uninit(&voice.sound);
        auto& index=impl_->recordingNext[std::string(track)];
        if(index%recordings->second.size()==0&&recordings->second.size()>1){
            // Source recordings use a shuffled bag; rotate its first entry to avoid immediate repeats.
            for(std::size_t i=recordings->second.size()-1;i>0;--i){impl_->randomState^=impl_->randomState<<13;impl_->randomState^=impl_->randomState>>7;impl_->randomState^=impl_->randomState<<17;std::swap(recordings->second[i],recordings->second[impl_->randomState%(i+1)]);}
            if(recordings->second.front().path==impl_->recordingLast[std::string(track)])std::swap(recordings->second[0],recordings->second[1]);
        }
        const auto& recording=recordings->second[index++%recordings->second.size()];const auto customPath=recording.path.wstring();config.pFilePathW=customPath.c_str();config.isLooping=recording.loop?MA_TRUE:MA_FALSE;
        config.loopPointBegInPCMFrames=MA_DATA_SOURCE_DEFAULT_LOOP_POINT_BEG;
        config.loopPointEndInPCMFrames=MA_DATA_SOURCE_DEFAULT_LOOP_POINT_END;
        impl_->recordingLast[std::string(track)]=recording.path;
        result=ma_sound_init_ex(&impl_->engine,&config,&voice.sound);
        if(result==MA_SUCCESS){recordingStarted=true;recordingGain=recording.gain;recordingLooping=recording.loop;recordingPath=recording.path;loopBegin=config.loopPointBegInPCMFrames;loopEnd=config.loopPointEndInPCMFrames;}
        else {config.pFilePathW=nativePath.c_str();config.isLooping=MA_TRUE;config.loopPointBegInPCMFrames=meta->second.loopBegin;config.loopPointEndInPCMFrames=meta->second.loopEnd;result=ma_sound_init_ex(&impl_->engine,&config,&voice.sound);}
    }
    if (result != MA_SUCCESS) { impl_->error = MiniaudioError("ma_sound_init_ex(music)", result); return false; }
    voice.initialized = true; voice.active = true; voice.looping = recordingLooping; voice.recording = recordingStarted;
    voice.track = std::string(track); voice.recordingPath = std::move(recordingPath); voice.recordingGain = recordingGain;
    voice.loopBegin = loopBegin; voice.loopEnd = loopEnd; voice.serial = ++impl_->serial;
    ma_sound_set_volume(&voice.sound, 1.0f);
    ma_sound_set_fade_in_milliseconds(&voice.sound, 0, static_cast<float>(recordingGain), static_cast<ma_uint64>(std::ceil(fadeIn * 1000)));
    result = ma_sound_start(&voice.sound);
    if (result != MA_SUCCESS) { impl_->error = MiniaudioError("ma_sound_start(music)", result); impl_->StopMusicVoice(voice); return false; }
    impl_->error.clear();
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
    if(impl_->currentMusic<kMusicVoices){auto& voice=impl_->musicVoices[impl_->currentMusic];
        if(voice.active&&voice.recording&&!voice.looping&&ma_sound_at_end(&voice.sound)){
            const auto track=voice.track;impl_->StopMusicVoice(voice);impl_->currentMusic=kMusicVoices;
            if(track=="battle")PlayMusic(track,.15);
        }
    }
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
    if (impl_->reverbReady) impl_->reverb.Reset();
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
