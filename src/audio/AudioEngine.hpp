#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace restoration {

enum class AudioBus { Sfx, Music };

struct AudioPosition { double x = 0, y = 0, z = 0; };

struct AudioPlayOptions {
    double volume = 1.0;
    double pitch = 1.0;
    AudioBus bus = AudioBus::Sfx;
    std::optional<AudioPosition> pos;
};

struct AudioLoopOptions {
    double volume = 1.0;
    double pitch = 1.0;
    std::optional<AudioPosition> pos;
    struct ZoneHumParameters { int mode=0; double tension=0; };
    std::optional<ZoneHumParameters> zoneHum;
};

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
    std::uint32_t sourceMaxVoices = 0;
    double sourcePitchJitter = 0;
    double sourceMinGapSeconds = 0;
    bool loopable = false;
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
    bool Play(std::string_view name, const AudioPlayOptions& options);
    bool StartLoop(std::string_view key, std::string_view name, const AudioLoopOptions& options = {});
    bool UpdateLoop(std::string_view key, const AudioLoopOptions& options);
    void StopLoop(std::string_view key, double fadeSeconds = 0.15);
    void SetListener(AudioPosition position, AudioPosition forward, AudioPosition up = {0, 1, 0});
    bool PlayMusic(std::string_view track, double fadeSeconds = 0.75);
    void StopMusic(double fadeSeconds = 0.5);
    void Duck(double amount = 0.5, double seconds = 1.2);
    void Update();
    void SetVolumes(double master, double music, double sfx);
    void SetVolumes(const AudioVolumes& volumes) { SetVolumes(volumes.master, volumes.music, volumes.sfx); }
    void Clear();

    std::optional<AudioClipMetadata> Metadata(std::string_view name);
    std::vector<std::string> Names() const;
    AudioVolumes volumes() const;
    std::size_t ActiveVoices();
    std::uint64_t stolenVoices() const;
    bool ready() const;
    const std::string& lastError() const;

    static double VolumeGain(double value);

private:
    friend bool AudioEngineMusicTestState(const AudioEngine&, std::filesystem::path&, bool&,
        std::uint64_t&, std::uint64_t&, std::uint64_t&, double&);
    friend bool AudioEngineMusicTestReadsAcrossEnd(AudioEngine&);
    friend double AudioEngineTestTime(const AudioEngine&);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace restoration
