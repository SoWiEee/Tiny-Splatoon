#include "../src/audio/AudioEngine.hpp"
#include "../src/audio/GameAudio.hpp"
#include "../src/game/MatchSimulation.hpp"
#include "../src/game/NavGraph.hpp"
#include "../src/world/FacePaint.hpp"
#include "../src/world/ReferenceData.hpp"
#include "../src/world/StageGeometry.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
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
        AudioEngine audio(assets);
        const auto names = audio.Names();
        require(names.size() == 201, "The native registry must expose the full 201-cue source catalog");
        for (const auto& name : names) {
            const auto clip = audio.Metadata(name);
            require(clip.has_value(), "Every source cue must have a readable WAV");
            require(clip->sampleRate == 48000 && clip->channels == 2 && clip->bitsPerSample == 16,
                "Source cue format must stay 48 kHz stereo PCM16");
            require(clip->frames > 0 && clip->durationSeconds > 0 && clip->peak > 0.01 && clip->rms > 0,
                "Source-rendered cue must be audible and have valid metadata");
        }
        for (const char* name : {"shoot_shooter", "shoot_blaster", "shoot_charger", "shoot_dualies", "shoot_splatling",
                "slosh_throw", "roller_flick", "bow_loose", "blade_swish", "crab_cannon", "boss_cannon_sweep"})
            require(audio.Metadata(name).has_value(), "Weapon, active-special, and Boss cues must resolve by source name");
        require(audio.Metadata("swim")->loopable && audio.Metadata("climb")->loopable &&
                audio.Metadata("enemy_ink_sizzle")->loopable && audio.Metadata("harbor_ambience")->loopable &&
                audio.Metadata("splatling_spin")->loopable && audio.Metadata("boss_dizzy")->loopable,
            "Actor, world and Boss loop beds must preserve their source loop declarations");
        require(!audio.Metadata("crab_gatling")->loopable, "One-shot source cues must not be promoted to loop beds");
        require(!audio.Metadata("not_a_source_clip").has_value(), "Unknown names must not silently resolve to substitutes");
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
        require(audio.ActiveVoices() == 0, "A muted SFX play must leave no active voice");
        audio.SetVolumes(1, 0, 1);
        require(!audio.Play("ui_click", 1, AudioBus::Music), "A muted music bus must not start a sound");
        require(audio.ActiveVoices() == 0, "A muted music play must leave no active voice");

        audio.SetVolumes(1, 1, 1);
        require(audio.Play("victory_fanfare"), "An audible source cue must start on miniaudio");
        require(audio.ActiveVoices() == 1, "One played cue must occupy one voice");
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        require(audio.ActiveVoices() == 1, "A long source cue must remain active during playback");
        audio.Clear();

        require(audio.Play("bomb_beep"), "A cue with an authored minimum gap must start");
        require(!audio.Play("bomb_beep"), "A repeated source cue inside its authored minimum gap must be rejected");
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        require(audio.Play("bomb_beep"), "The source minimum gap must expire while the earlier voice is still active");
        audio.Clear();
        std::this_thread::sleep_for(std::chrono::milliseconds(60));

        AudioPlayOptions voiceCapOptions;
        voiceCapOptions.pitch = 0.05;
        for (std::size_t i = 0; i < 48; ++i) {
            if (!audio.Play(names[i], voiceCapOptions))
                throw std::runtime_error("A distinct source cue failed to enter the global voice pool: " + names[i] + " (" + audio.lastError() + ")");
        }
        require(audio.ActiveVoices() == 48, "Distinct source cues must fill the bounded 48-voice pool");
        require(audio.Play(names[48]), "A cue over the global voice limit must steal an older source voice");
        require(audio.ActiveVoices() == 48 && audio.stolenVoices() > 0,
            "The source-compatible voice cap must steal only when its 48-voice bound is reached");
        audio.Clear();
        require(audio.ActiveVoices() == 0, "Clear must stop every live sound and release its source");

        require(audio.StartLoop("test:swim", "swim"), "A registered source loop must start");
        require(audio.UpdateLoop("test:swim", AudioLoopOptions{0.4, 1.1, AudioPosition{1, 2, 3}}),
            "A managed loop must accept live volume, pitch, and position updates");
        audio.StopLoop("test:swim", 0);
        audio.Update();
        require(audio.ActiveVoices() == 0, "A stopped loop must be reaped after its fade");
        require(audio.StartLoop("test:replace", "swim"), "A source loop should start under a managed key");
        require(audio.StartLoop("test:replace", "jet_loop"), "A managed key should replace its loop when the requested source cue changes");
        require(audio.ActiveVoices() == 1, "Changing a loop key's source cue must not duplicate loop voices");
        audio.StopLoop("test:replace", 0);
        audio.Update();
        require(audio.ActiveVoices() == 0, "A replaced source loop must retain its normal stop lifecycle");
        require(audio.StartLoop("test:restart", "swim"), "A source loop should start under a restartable key");
        audio.StopLoop("test:restart", 0.15);
        require(audio.StartLoop("test:restart", "jet_loop"), "A fading loop key should be reusable immediately");
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        audio.Update();
        require(audio.UpdateLoop("test:restart", AudioLoopOptions{}), "Reaping an earlier fading loop must preserve the restarted key");
        audio.StopLoop("test:restart", 0);
        audio.Update();
        require(audio.ActiveVoices() == 0, "A restarted managed loop must still stop through its key");

        const char* tracks[] = {"title", "menu", "battle", "battle_final", "results_win", "results_lose", "boss", "boss_2", "boss_3"};
        for (const char* track : tracks)
            require(audio.PlayMusic(track, 0), "Every exported source music track must open and play through miniaudio");
        require(audio.PlayMusic("battle", 0), "A source track must be restartable");
        require(audio.PlayMusic("battle_final", 0.4), "Same-tempo source tracks must accept a bar-aligned handoff");
        audio.StopMusic(0);
        audio.Update();
        require(!audio.PlayMusic("not_a_source_track"), "Unknown music names must not resolve to a fallback track");
        audio.Clear();

        const auto& stages = ReferenceStages();
        const auto stage = std::find_if(stages.begin(), stages.end(), [](const StageData& candidate) {
            return candidate.id == "halyard" && candidate.mode == "turf" && !candidate.onlineOnly && !candidate.noBoss;
        });
        require(stage != stages.end(), "GameAudio integration needs the offline Halyard Turf stage");
        StageGeometry geometry(*stage);
        FacePaint paint(*stage, geometry);
        NavGraph nav(*stage, geometry);
        MatchOptions matchOptions; matchOptions.practice = true; matchOptions.special = "jetpack";
        MatchSimulation match(*stage, geometry, paint, nav, matchOptions);
        AudioEngine directorEngine(assets);
        if (!directorEngine.Initialize()) throw std::runtime_error("GameAudio integration could not initialize miniaudio: " + directorEngine.lastError());
        GameAudio director(directorEngine);
        const AudioPosition listener{0, 2, 0}, forward{0, 0, -1};
        director.Menu(true);
        director.BeginMatch(match);
        match.tick(1.0 / 60.0);
        director.Update(1.0 / 60.0, match, {}, listener, forward);
        auto& actor = match.local();
        actor.state.squid = false; actor.state.groundTeam = 1;
        director.Footstep(actor, AudioPosition{actor.state.pos.x, actor.state.pos.y, actor.state.pos.z}, 4.0);
        director.Update(5.0, match, {}, listener, forward); // First harbour gull and Halyard rigging burst.
        require(directorEngine.lastError().empty(), "Footstep and stage ambience cues must resolve from source assets");
        actor.input.squid = true; actor.input.move = {0, 0, 1};
        CharacterTrigger shoot; shoot.name = "shoot";
        CharacterTrigger throwSub; throwSub.name = "throw";
        director.ActorTrigger(actor, shoot);
        director.ActorTrigger(actor, throwSub);
        std::vector<WorldVisualEvent> events(5);
        events[0].kind = "explosion"; events[1].kind = "bulletImpact"; events[2].kind = "slamWave";
        events[3].kind = "stormStart"; events[4].kind = "burst";
        match.tick(1.0 / 60.0);
        director.Update(1.0 / 60.0, match, events, listener, forward);
        require(actor.specialId() == "jetpack", "The integration actor must use the jetpack special");
        actor.state.specialCharge = 500; actor.input.special = true;
        match.tick(1.0 / 60.0); actor.input.special = false;
        director.Update(1.0 / 60.0, match, {}, listener, forward);
        require(actor.specialPose().active, "The integration actor's source special must activate");
        director.ActorTrigger(actor, shoot); // Active-special shots must route to jet_fire, not the main shooter.
        require(directorEngine.PlayMusic("battle_final", 0.75), "The director's battle music must accept a same-tempo phase transition");
        require(directorEngine.lastError().empty(), "Playable GameAudio cues, tracks, and the transition riser must resolve without an error");
        require(directorEngine.ActiveVoices() >= 1, "GameAudio should have live source SFX while the match runs");
        std::this_thread::sleep_for(std::chrono::milliseconds(3300));
        director.Update(1.0 / 60.0, match, {}, listener, forward);
        require(directorEngine.ActiveVoices() == 2, "Finished one-shots should leave the managed jetpack and harbour loops active");
        director.Pause();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        directorEngine.Update();
        require(directorEngine.ActiveVoices() == 1, "Pause must fade managed actor loops while preserving harbour ambience");
        director.Update(1.0 / 60.0, match, {}, listener, forward);
        require(directorEngine.ActiveVoices() == 2, "The next GameAudio update must restore actor loops without restarting the match");
        director.EndMatch();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        directorEngine.Update();
        require(directorEngine.ActiveVoices() == 0, "EndMatch must stop managed actor loops and clear faded voices");
        require(directorEngine.lastError().empty(), "A complete GameAudio match lifecycle must not request missing source assets");

        std::cout << "Audio source test passed: " << names.size()
                  << " cues, source loop metadata, repeat gaps, miniaudio loop/music lifecycle, GameAudio event routing and cleanup, 48-voice steal, and Clear.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Audio engine test failed: " << e.what() << '\n';
        return 1;
    }
}
