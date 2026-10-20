#include "../src/audio/AudioEngine.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>

namespace {
void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
}

int main(int argc, char** argv) {
    using namespace restoration;
    try {
        const std::filesystem::path assets = argc > 1 ? argv[1] : "assets/audio";
        constexpr std::array<const char*, 20> names = {
            "ui_hover", "ui_click", "ui_back", "ui_confirm", "ui_toggle", "ui_slider", "ui_error",
            "shoot_shooter", "hit_marker", "hurt", "squid_in", "squid_out", "jump", "land",
            "ready", "go_horn", "final_count", "times_up", "victory_fanfare", "defeat_jingle",
        };

        AudioEngine audio(assets);
        for (const char* name : names) {
            const auto clip = audio.Metadata(name);
            require(clip.has_value(), "Every selected source cue must have a readable WAV");
            require(clip->sampleRate == 48000 && clip->channels == 2 && clip->bitsPerSample == 16,
                "Source cue format must stay 48 kHz stereo PCM16");
            require(clip->frames > 0 && clip->durationSeconds > 0 && clip->peak > 0.01 && clip->rms > 0,
                "Source-rendered cue must be audible and have valid metadata");
        }
        require(!audio.Metadata("not_a_source_clip").has_value(), "Unknown names must not silently resolve to a substitute");
        require(AudioEngine::VolumeGain(0) == 0, "Zero volume must produce digital silence");
        require(std::abs(AudioEngine::VolumeGain(0.5) - std::pow(0.5, 1.5)) < 1e-12,
            "Volume must use the source perceptual pow(v, 1.5) taper");
        require(AudioEngine::VolumeGain(1.5) == 1 && AudioEngine::VolumeGain(-1) == 0,
            "Volume taper must clamp its input to [0, 1]");

        audio.SetVolumes(0.5, 2.0, std::numeric_limits<double>::quiet_NaN());
        const auto volumes = audio.volumes();
        require(std::abs(volumes.master - 0.5) < 1e-12 && volumes.music == 1 && volumes.sfx == 0,
            "Bus volume updates must clamp values and treat non-finite input as mute");
        require(audio.ActiveVoices() == 0, "A new engine must start without active voices");
        audio.Clear();
        audio.Clear();
        require(audio.ActiveVoices() == 0, "Clear must be idempotent before initialization");

        if (!audio.Initialize()) throw std::runtime_error("miniaudio engine initialization failed: " + audio.lastError());
        require(audio.ready(), "The miniaudio engine should report ready after initialization");

        audio.SetVolumes(1, 1, 0);
        require(!audio.Play("ui_click"), "A muted SFX bus must not start a sound");
        require(audio.ActiveVoices() == 0, "A muted SFX play must leave no active sound");
        audio.SetVolumes(1, 0, 1);
        require(!audio.Play("ui_click", 1, AudioBus::Music), "A muted music bus must not start a sound");
        require(audio.ActiveVoices() == 0, "A muted music play must leave no active sound");

        audio.SetVolumes(1, 1, 1);
        require(audio.Play("victory_fanfare"), "An audible source cue must start on miniaudio");
        require(audio.ActiveVoices() == 1, "One played cue must occupy one voice");
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        require(audio.ActiveVoices() == 1, "A long source cue must remain active during playback");

        for (int i = 0; i < 55; ++i) require(audio.Play("victory_fanfare"), "Voice-pool stress playback failed");
        require(audio.ActiveVoices() == 48 && audio.stolenVoices() > 0,
            "The source-compatible 48 voice cap must steal the oldest overlapping cue");
        audio.Clear();
        require(audio.ActiveVoices() == 0, "Clear must stop every live sound and release its source");
        audio.Clear();
        require(audio.ActiveVoices() == 0, "Clear must remain idempotent after playback");

        std::cout << "Audio source slice passed: " << names.size()
                  << " clips, source volume taper, miniaudio init, mute, 48-voice steal, and Clear lifecycle.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Audio engine test failed: " << e.what() << '\n';
        return 1;
    }
}
