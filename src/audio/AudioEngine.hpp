#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace restoration {

enum class AudioBus { Sfx, Music };

struct AudioClipMetadata {
    std::string name;
    std::uint32_t sampleRate = 0;
    std::uint16_t channels = 0;
    std::uint16_t bitsPerSample = 0;
    std::uint32_t frames = 0;
    std::uint64_t dataBytes = 0;
    double durationSeconds = 0;
    double peak = 0;
    double rms = 0;
};

struct AudioVolumes {
    double master = 0.8;
    double music = 0.6;
    double sfx = 0.85;
};

class AudioEngine {
public:
    explicit AudioEngine(std::filesystem::path assetDirectory = "assets/audio");
    ~AudioEngine();
    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;

    bool Initialize();
    bool Play(std::string_view name, double volume = 1.0, AudioBus bus = AudioBus::Sfx);
    void SetVolumes(double master, double music, double sfx);
    void SetVolumes(const AudioVolumes& volumes) { SetVolumes(volumes.master, volumes.music, volumes.sfx); }
    void Clear();

    std::optional<AudioClipMetadata> Metadata(std::string_view name);
    AudioVolumes volumes() const;
    std::size_t ActiveVoices();
    std::uint64_t stolenVoices() const;
    bool ready() const;
    const std::string& lastError() const;

    static double VolumeGain(double value);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace restoration
