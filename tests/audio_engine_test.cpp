#include "../src/audio/AudioEngine.hpp"
#include "../src/audio/GameAudio.hpp"
#include "../src/game/MatchSimulation.hpp"
#include "../src/game/CombatWorld.hpp"
#include "../src/game/ArsenalData.hpp"
#include "../src/game/NavGraph.hpp"
#include "../src/world/FacePaint.hpp"
#include "../src/world/ReferenceData.hpp"
#include "../src/world/StageGeometry.hpp"
#include "fixtures/sonar_wail_source_fixtures.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <stdexcept>
#include <thread>
#include <tuple>
#include <utility>
#include <vector>

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
                "slosh_throw", "roller_flick", "bow_loose", "blade_swish", "crab_cannon", "boss_cannon_sweep", "xp_tick"})
            require(audio.Metadata(name).has_value(), "Weapon, active-special, Boss, and UI cues must resolve by source name");
        require(audio.Metadata("swim")->loopable && audio.Metadata("climb")->loopable &&
                audio.Metadata("enemy_ink_sizzle")->loopable && audio.Metadata("harbor_ambience")->loopable &&
                audio.Metadata("splatling_spin")->loopable && audio.Metadata("boss_dizzy")->loopable &&
                audio.Metadata("shaker_fizz")->loopable && audio.Metadata("wail_blast")->loopable,
            "Actor, weapon, world, Special and Boss loops must preserve their source loop declarations");
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

        std::string battleIndex;
        {
            std::ifstream musicIndex(assets / "music.tsv");
            for (std::string line; std::getline(musicIndex, line);)
                if (line.rfind("battle\t", 0) == 0) { battleIndex = std::move(line); break; }
        }
        require(!battleIndex.empty(), "Custom song tests need the packaged battle metadata row");
        const auto tempRoot = std::filesystem::temp_directory_path() /
            ("tiny-splatoon-audio-recordings-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        const auto makeRecordingRoot = [&](const std::string& name, bool customFile, bool packagedFallback, bool brokenFile) {
            const auto root = tempRoot / name;
            std::filesystem::create_directories(root / "music");
            std::filesystem::create_directories(root / "songs");
            { std::ofstream index(root / "music.tsv"); index << battleIndex << '\n'; }
            { std::ofstream index(root / "custom-music.tsv"); index << "battle\t-3\tsongs/recording.flac\n"; }
            if (customFile && brokenFile) {
                std::ofstream recording(root / "songs" / "recording.flac", std::ios::binary);
                recording << "broken FLAC fixture";
            } else if (customFile) {
                std::filesystem::copy_file(assets / "music" / "battle.flac", root / "songs" / "recording.flac");
            }
            if (packagedFallback)
                std::filesystem::copy_file(assets / "music" / "battle.flac", root / "music" / "battle.flac");
            return root;
        };
        try {
            {
                AudioEngine customMusic(makeRecordingRoot("valid", true, false, false));
                require(customMusic.Initialize(), "A valid optional recording should initialize miniaudio");
                require(customMusic.PlayMusic("battle", 0), "A valid optional recording should play when the packaged track is absent");
                require(customMusic.lastError().empty(), "A successful optional recording should leave no decoder error");
                customMusic.Clear();
            }
            for (const auto& [name, present, broken] : {
                    std::tuple{"missing", false, false}, std::tuple{"broken", true, true}}) {
                AudioEngine fallback(makeRecordingRoot(name, present, true, broken));
                require(fallback.Initialize(), "A packaged fallback test should initialize miniaudio");
                require(fallback.PlayMusic("battle", 0), "A missing or undecodable optional recording must fall back to the packaged track");
                require(fallback.lastError().empty(), "A successful packaged fallback should clear the failed optional decoder error");
                fallback.Clear();
            }
            std::filesystem::remove_all(tempRoot);
        } catch (...) {
            std::filesystem::remove_all(tempRoot);
            throw;
        }

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

        AudioEngine replacementMusic(assets);
        require(replacementMusic.Initialize(), "Bundled replacement tracks must initialize through miniaudio");
        std::uint64_t menuSerial = 0, battleSerial = 0;
        for (const auto& [track, file] : std::array<std::pair<std::string_view, std::string_view>, 7>{{
                {"title", "LifeWillChange_Instrumental.mp3"}, {"menu", "LifeWillChange_Instrumental.mp3"},
                {"battle", "Splattack.mp3"}, {"battle_final", "Splattack.mp3"}, {"boss", "Splattack.mp3"},
                {"boss_2", "Splattack.mp3"}, {"boss_3", "Splattack.mp3"}}}) {
            require(replacementMusic.PlayMusic(track, 0), "Each bundled replacement alias must be playable");
            std::filesystem::path source;
            bool looping = false;
            std::uint64_t loopBegin = 0, loopEnd = 0, serial = 0;
            double durationSeconds = 0;
            bool streamReady = false;
            for (int attempt = 0; attempt < 50 && !streamReady; ++attempt) {
                streamReady = AudioEngineMusicTestState(replacementMusic, source, looping, loopBegin, loopEnd, serial, durationSeconds);
                if (!streamReady) std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            require(streamReady,
                "Bundled music status must expose a decoded stream to the focused audio test");
            require(source == assets / file, "Each logical music name must use its exact bundled MP3 asset");
            require(looping && loopBegin == 0 && loopEnd == std::numeric_limits<std::uint64_t>::max(),
                "Bundled MP3s must loop from the first frame through the decoder's full-file end boundary");
            const double expectedDuration = file == "Splattack.mp3" ? 194.142 : 150.653;
            require(std::abs(durationSeconds - expectedDuration) < 0.5,
                "Bundled MP3 stream length must match its supplied asset without waiting for playback to end");
            if (file == "LifeWillChange_Instrumental.mp3") {
                if (!menuSerial) menuSerial = serial;
                else require(serial == menuSerial, "Title and match-menu aliases must keep one Life Will Change stream");
            } else {
                if (!battleSerial) battleSerial = serial;
                else require(serial == battleSerial, "Battle, final-minute and Boss aliases must keep one Splattack stream");
            }
        }
        require(AudioEngineMusicTestReadsAcrossEnd(replacementMusic),
            "The bundled Splattack stream must read all requested frames across its actual end loop boundary");
        replacementMusic.Clear();

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
        director.BeginMatch(match);
        match.world().deploy(actor,"boomerang");
        director.Update(1.0/60,match,{},listener,forward);
        require(directorEngine.ActiveVoices()>=2,"Source device loop must start alongside harbour ambience");
        const auto readyCue = directorEngine.Metadata("ready");
        require(readyCue.has_value(), "The device pause test needs the ready cue duration to settle intro audio");
        double introCueSeconds = readyCue->durationSeconds;
        if (match.phase() == MatchPhase::Playing) {
            const auto goCue = directorEngine.Metadata("go_horn");
            require(goCue.has_value(), "A playing match needs the go-horn duration to settle intro audio");
            introCueSeconds = std::max(introCueSeconds, goCue->durationSeconds);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>((introCueSeconds + 0.25) * 1000.0)));
        directorEngine.Update(); // Let BeginMatch's one-shot cues finish before counting managed loops.
        director.Pause();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        directorEngine.Update();
        require(directorEngine.ActiveVoices()==1,"Pause must stop device loops while preserving harbour ambience");
        director.Update(1.0/60,match,{},listener,forward);
        require(directorEngine.ActiveVoices()>=2,"The next update must restore live device loops after pause");
        director.EndMatch();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));directorEngine.Update();
        require(directorEngine.ActiveVoices()==0,"Device loops must fade and clear at match teardown");
        require(directorEngine.lastError().empty(),"Device transition routing requested an unknown source cue");

        MatchOptions bossOptions; bossOptions.mode = "boss"; bossOptions.duration = 180;
        MatchSimulation bossMatch(*stage, geometry, paint, nav, bossOptions);
        auto* boss = bossMatch.boss();
        require(boss != nullptr, "The Boss audio fallback test needs a live Boss simulation");
        for (int i = 0; i < 55; ++i) boss->tick(0.1, false);
        require(boss->state().visible && !boss->poseSocket("hatch"),
            "A Boss without a pose sink must expose no named socket and use audio's body-position fallback");
        bool positionedPlayer = false;
        for (const int cell : boss->nav().planIds()) {
            const auto [x, z] = boss->nav().xz(cell);
            if (boss->nav().inPad(x, z, 3.5)) continue;
            bossMatch.local().spawnAt({x, boss->nav().floorAt(x, z), z}, boss->state().yaw);
            positionedPlayer = true;
            break;
        }
        require(positionedPlayer, "The Boss audio test needs a target outside the spawn pads");
        for (const auto& actor : bossMatch.actors())
            if (actor.get() != &bossMatch.local()) actor->state.alive = false;
        bossMatch.startPlaying();
        boss->forceMove(BossMoveId::Crablets);
        const auto isCrabletTelegraph = [&] {
            return boss->move() && boss->move()->id == BossMoveId::Crablets &&
                boss->move()->phase(boss->state().clock) == BossMovePhase::Telegraph;
        };
        for (int i = 0; i < 60 && !isCrabletTelegraph(); ++i) boss->tick(1.0 / 60.0, true);
        require(boss->state().target == 0, "The Boss fallback setup must select its repositioned local target");
        require(isCrabletTelegraph(),
            "The Boss fallback test must route a telegraph that looks up its missing hatch socket");
        director.BeginMatch(bossMatch);
        const auto voicesBeforeBossCue = directorEngine.ActiveVoices();
        director.Update(1.0 / 60.0, bossMatch, {}, listener, forward);
        require(directorEngine.ActiveVoices() > voicesBeforeBossCue && directorEngine.lastError().empty(),
            "GameAudio must route a Boss socket cue through its optional body-position fallback");
        const auto advanceBossAudio = [&](double step) {
            boss->tick(step, true);
            director.Update(step, bossMatch, {}, listener, forward);
        };
        while (boss->move()->phase(boss->state().clock) == BossMovePhase::Telegraph) advanceBossAudio(0.1);
        require(boss->move()->phase(boss->state().clock) == BossMovePhase::Active,
            "The Crablet move should reach its active phase for delayed-cue timing checks");
        const auto voicesAtCrabletAct = directorEngine.ActiveVoices();
        advanceBossAudio(0.1); advanceBossAudio(0.1);
        require(directorEngine.ActiveVoices() == voicesAtCrabletAct,
            "The second Crablet chitter must not play before its 300 ms source delay");
        const auto chitterMetadata = audio.Metadata("crablet_chitter");
        require(chitterMetadata && chitterMetadata->durationSeconds > chitterMetadata->sourceMinGapSeconds,
            "The Crablet chitter must outlast its source repeat gap for a voice-count timing check");
        std::this_thread::sleep_for(std::chrono::duration<double>(chitterMetadata->sourceMinGapSeconds + 0.03));
        advanceBossAudio(0.1); advanceBossAudio(0.01);
        require(directorEngine.ActiveVoices() > voicesAtCrabletAct,
            "The second Crablet chitter must play after the 300 ms source delay");
        director.EndMatch();

        boss->reset();
        for (int i = 0; i < 55; ++i) boss->tick(0.1, false);
        boss->forceMove(BossMoveId::Barrage);
        const auto isBarrageTelegraph = [&] {
            return boss->move() && boss->move()->id == BossMoveId::Barrage &&
                boss->move()->phase(boss->state().clock) == BossMovePhase::Telegraph;
        };
        for (int i = 0; i < 60 && !isBarrageTelegraph(); ++i) boss->tick(1.0 / 60.0, true);
        require(boss->move() && boss->move()->id == BossMoveId::Barrage && !boss->move()->barrels.empty(),
            "The Barrage timing test needs an active source move with at least one barrel target");
        director.BeginMatch(bossMatch);
        director.Update(1.0 / 60.0, bossMatch, {}, listener, forward);
        while (boss->move()->phase(boss->state().clock) == BossMovePhase::Telegraph) advanceBossAudio(0.1);
        require(boss->move()->phase(boss->state().clock) == BossMovePhase::Active,
            "Barrage should reach its active phase for whistle and barrel timing checks");
        const auto voicesAtBarrageAct = directorEngine.ActiveVoices();
        for (int i = 0; i < 9; ++i) advanceBossAudio(0.1);
        const auto voicesBeforeFirstBarrel = directorEngine.ActiveVoices();
        require(voicesBeforeFirstBarrel >= voicesAtBarrageAct,
            "Staggered Barrage whistles should remain routed through the active phase");
        advanceBossAudio(0.1);
        require(directorEngine.ActiveVoices() > voicesBeforeFirstBarrel,
            "The first Barrage barrel cue must wait until approximately 950 ms after the first whistle");
        director.EndMatch();

        MatchOptions routeOptions; routeOptions.practice = true;
        MatchSimulation routeMatch(*stage, geometry, paint, nav, routeOptions);
        GameAudio routeDirector(directorEngine);
        routeDirector.BeginMatch(routeMatch);
        routeDirector.Update(1.0 / 60.0, routeMatch, {}, listener, forward);
        struct EventRouteCase {
            WorldVisualEvent event;
            std::size_t expectedVoices;
            std::vector<std::string_view> cues;
            std::string label;
        };
        std::vector<EventRouteCase> routes;
        const auto addRoute = [&](std::string label, const char* kind, const char* source,
                                  std::size_t expected, std::initializer_list<std::string_view> cues,
                                  double charge = 0.0, double radius = 2.0) {
            WorldVisualEvent event;
            event.kind = kind; event.source = source; event.pos = {2.0, 0.5, 1.0};
            event.radius = radius; event.charge = charge;
            routes.push_back({std::move(event), expected, std::vector<std::string_view>(cues), std::move(label)});
        };
        for (const char* source : {"mine", "beacon", "sticky", "sprinkler", "shaker"})
            addRoute(std::string("device_arm/") + source, "device_arm", source, 1, {"bomb_beep"});
        addRoute("device_arm/scan", "device_arm", "scan", 1, {"special_activate"});
        addRoute("device_arm/mist", "device_arm", "mist", 1, {"enemy_ink_sizzle"});
        addRoute("device_arm/curtain", "device_arm", "curtain", 1, {"swim_splash"});
        addRoute("device_arm/boomerang is silent", "device_arm", "boomerang", 0, {});
        for (const char* source : {"mine", "sticky", "waddle", "boomerang"})
            addRoute(std::string("device_warning/") + source, "device_warning", source, 1, {"bomb_beep"}, 0.6);
        addRoute("special_warning/booyah", "special_warning", "booyah", 1, {"bomb_beep"});
        addRoute("special_audio/bomb_throw", "special_audio", "bomb_throw", 1, {"bomb_throw"}, 1.0);
        addRoute("special_audio/bomb_throw burst pitch", "special_audio", "bomb_throw", 1, {"bomb_throw"}, 1.2);
        addRoute("special_audio/shaker fizzle", "special_audio", "shaker_fizzle", 1, {"shaker_fizzle"});
        addRoute("special_audio/shaker charge clink", "special_audio", "shaker_clink", 1, {"shaker_clink"});
        addRoute("special_audio/shaker max clink", "special_audio", "shaker_clink_max", 1, {"shaker_clink_max"});
        for (const char* source : {"sonar_ping", "sonar_mark", "strike_whistle", "zip_latch", "zip_pull",
                 "shield_hit", "shield_pop", "crab_hit", "crab_break", "booyah_cheer", "jet_end"})
            addRoute(std::string("special_audio/") + source, "special_audio", source, 1, {source});
        addRoute("special_audio/jet_boost is not a producer tag", "special_audio", "jet_boost", 0, {});
        addRoute("device_hit/tracer", "device_hit", "tracer", 1, {"tracer_hit"});
        addRoute("device_hit/tracer trail near camera stays silent", "device_hit", "tracer_trail", 0, {"tracer_hit"});
        addRoute("device_hit/tracer trail local owner", "device_hit", "tracer_trail", 1, {"tracer_hit"});
        routes.back().event.sourceLocal = true;
        routes.back().event.pos = {80.0, 0.0, 80.0};
        addRoute("device_hit/tracer trail local victim", "device_hit", "tracer_trail", 1, {"tracer_hit"});
        routes.back().event.sourceVictimLocal = true;
        routes.back().event.pos = {80.0, 0.0, 80.0};
        addRoute("device_hit/tracer direct remote near camera", "device_hit", "tracer", 1, {"tracer_hit"});
        addRoute("device_hit/tracer direct remote out of range", "device_hit", "tracer", 0, {"tracer_hit"});
        routes.back().event.pos = {80.0, 0.0, 80.0};
        addRoute("device_destroy/torpedo", "device_destroy", "torpedo", 2, {"splat_small", "torpedo_pop"});
        addRoute("device_destroy/waddle flight", "device_destroy", "waddle", 1, {"splat_small"}, 0.0);
        addRoute("device_destroy/waddle armed", "device_destroy", "waddle", 1, {"splat_small"}, 1.0);
        addRoute("device_destroy/beacon", "device_destroy", "beacon", 1, {"splat_small"});
        addRoute("device_destroy/sprinkler", "device_destroy", "sprinkler", 1, {"splat_small"});
        addRoute("bubble_pop/blower", "bubble_pop", "blower", 1, {"bubble_pop"});
        for (const char* source : {"bomb", "mine", "sticky", "seeker", "mist", "waddle"})
            addRoute(std::string("explosion/") + source, "explosion", source, 1, {"bomb_explode"}, 0.0, 3.0);
        addRoute("explosion/burst", "explosion", "burst", 1, {"blaster_boom"}, 0.0, 3.0);
        addRoute("explosion/boomerang small", "explosion", "boomerang", 1, {"boomerang_burst"}, 0.0, 3.0);
        addRoute("explosion/boomerang large", "explosion", "boomerang", 1, {"bomb_explode"}, 1.0, 3.0);
        addRoute("explosion/booyah", "explosion", "booyah", 1, {"booyah_blast"}, 0.0, 3.0);
        addRoute("explosion/strike", "explosion", "strike", 1, {"strike_impact"}, 0.0, 3.0);
        addRoute("explosion/stamp", "explosion", "stamp", 1, {"stamp_slam"}, 0.0, 3.0);
        addRoute("explosion/kraken", "explosion", "kraken", 1, {"kraken_slam"}, 0.0, 3.0);
        addRoute("explosion/blower", "explosion", "blower", 1, {"bubble_blast"}, 0.0, 3.0);
        addRoute("explosion/zipcaster", "explosion", "zipcaster", 1, {"blaster_boom"}, 0.0, 3.0);
        addRoute("explosion/shaker", "explosion", "shaker", 1, {"bomb_explode"}, 2.0, 3.0);
        addRoute("explosion/torpedo soft", "explosion", "torpedo", 2, {"bomb_explode", "torpedo_pop"}, 0.0, 3.0);
        addRoute("explosion/torpedo full", "explosion", "torpedo", 2, {"bomb_explode", "torpedo_pop"}, 1.0, 3.0);
        addRoute("explosion/crab is silent", "explosion", "crab", 0, {}, 0.0, 3.0);
        addRoute("explosion/jetpack is silent", "explosion", "jetpack", 0, {}, 0.0, 3.0);
        addRoute("bulletImpact", "bulletImpact", "", 1, {"ink_hit_wall"});
        addRoute("slamWave", "slamWave", "", 1, {"special_slam"});
        addRoute("stormStart", "stormStart", "", 1, {"storm_thunder"});
        addRoute("world burst", "burst", "", 1, {"splat_big"});
        addRoute("beam is silent without weapon identity", "beam", "", 0, {});

        const std::array<std::pair<std::string_view, std::string_view>, 5> barrageVariants{{
            {"barrage", "bomb"}, {"barrage_sticky", "sticky"}, {"barrage_burst", "burst"},
            {"barrage_seeker", "seeker"}, {"barrage_mist", "mist"}}};
        for (const auto& [specialId, subId] : barrageVariants) {
            const auto& special = GetEquipment(EquipmentCategory::Special, specialId);
            require(special.text("bomb") == subId, "Each Bomb Rush special must select its configured sub device");
            addRoute(std::string(specialId) + " throw", "special_audio", "bomb_throw", 1, {"bomb_throw"},
                subId == "burst" ? 1.2 : 1.0);
        }

        for (const auto& route : routes) {
            directorEngine.Clear();
            double gap = 0.0;
            for (const auto cue : route.cues) {
                const auto metadata = directorEngine.Metadata(cue);
                require(metadata.has_value(), "Every tested route cue must resolve from the source catalog");
                gap = std::max(gap, metadata->sourceMinGapSeconds);
            }
            if (gap > 0.0) std::this_thread::sleep_for(std::chrono::duration<double>(gap + 0.03));
            const auto before = directorEngine.ActiveVoices();
            routeDirector.Update(1.0 / 60.0, routeMatch, {route.event}, listener, forward);
            const auto after = directorEngine.ActiveVoices();
            if (after != before + route.expectedVoices)
                throw std::runtime_error("Isolated audio route " + route.label + " expected " +
                    std::to_string(route.expectedVoices) + " voices, observed " +
                    std::to_string(after >= before ? after - before : 0));
        }
        routeDirector.EndMatch();

        MatchOptions wailOptions; wailOptions.practice = true; wailOptions.special = "wail";
        MatchSimulation wailMatch(*stage, geometry, paint, nav, wailOptions);
        GameAudio wailDirector(directorEngine);
        const auto wailDt = 1.0 / 60.0;
        wailDirector.BeginMatch(wailMatch);
        wailDirector.Update(wailDt, wailMatch, {}, listener, forward);
        const auto wailReadyCue = directorEngine.Metadata("ready");
        require(wailReadyCue.has_value(), "Wail lifecycle test needs the match-ready cue duration");
        double wailIntroCueSeconds = wailReadyCue->durationSeconds;
        if (wailMatch.phase() == MatchPhase::Playing) {
            const auto goCue = directorEngine.Metadata("go_horn");
            require(goCue.has_value(), "A playing Wail match needs the go-horn cue duration");
            wailIntroCueSeconds = std::max(wailIntroCueSeconds, goCue->durationSeconds);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>((wailIntroCueSeconds + 0.25) * 1000.0)));
        directorEngine.Update();
        wailDirector.Update(wailDt, wailMatch, {}, listener, forward);
        const auto voicesBeforeWail = directorEngine.ActiveVoices();

        auto& wailActor = wailMatch.local();
        const double wailSpecialCost = wailActor.specialWeapon().number("specialCost", 190.0);
        const auto activateAndPlaceWail = [&] {
            wailActor.state.specialCharge = wailSpecialCost;
            wailActor.input.special = true;
            wailMatch.tick(wailDt);
            wailActor.input.special = false;
            for (int frame = 0; frame < test::wailSourceFixture.pressFrames; ++frame)
                wailMatch.tick(wailDt);
            wailActor.input.fire = true;
            wailMatch.tick(wailDt);
            wailActor.input.fire = false;
            const auto placed = wailMatch.world().visualObjects();
            const auto speaker = std::find_if(placed.begin(), placed.end(), [](const auto& object) {
                return object.kind == "speaker" && object.phase == "charge";
            });
            require(speaker != placed.end() && speaker->id != 0,
                    "Wail match input must place a real charging speaker with a stable identity");
            return speaker == placed.end() ? std::uint64_t{} : speaker->id;
        };
        const auto tickUntilBlast = [&](std::uint64_t speakerId) {
            for (int frame = 0; frame < test::wailSourceFixture.stepChargeFrames + 5; ++frame) {
                const auto objects = wailMatch.world().visualObjects();
                if (std::any_of(objects.begin(), objects.end(), [&](const auto& object) {
                        return object.kind == "speaker" && object.phase == "blast" && object.id == speakerId;
                    })) return;
                wailMatch.tick(wailDt);
            }
            require(false, "Placed Wail speaker must reach its source blast phase");
        };

        const auto firstWailId = activateAndPlaceWail();
        tickUntilBlast(firstWailId);
        auto wailEvents = wailMatch.world().takeVisualEvents();
        require(std::count_if(wailEvents.begin(), wailEvents.end(), [&](const auto& event) {
                    return event.kind == "special_audio" && event.source == "wail_blast_start" && event.id == firstWailId;
                }) == 1, "First real Wail speaker must emit one identified blast-start event");
        wailDirector.Update(wailDt, wailMatch, wailEvents, listener, forward);
        require(directorEngine.ActiveVoices() == voicesBeforeWail + 1,
                "Wail blast start must create one continuous source loop");
        wailDirector.Update(wailDt, wailMatch, {}, listener, forward);
        require(directorEngine.ActiveVoices() == voicesBeforeWail + 1,
                "Repeated snapshots must update the same Wail loop without duplicating it");

        wailMatch.world().clear();
        wailDirector.Update(wailDt, wailMatch, {}, listener, forward);
        std::this_thread::sleep_for(std::chrono::milliseconds(350));
        directorEngine.Update();
        require(directorEngine.ActiveVoices() == voicesBeforeWail,
                "World clear must reconcile away an active Wail loop without a stop event");

        const auto replacementWailId = activateAndPlaceWail();
        require(replacementWailId != firstWailId, "Wail speaker identity must not be reused after world clear");
        tickUntilBlast(replacementWailId);
        wailEvents = wailMatch.world().takeVisualEvents();
        require(std::count_if(wailEvents.begin(), wailEvents.end(), [&](const auto& event) {
                    return event.kind == "special_audio" && event.source == "wail_blast_start" && event.id == replacementWailId;
                }) == 1, "Replacement Wail speaker must start under its new identity after world clear");
        wailDirector.Update(wailDt, wailMatch, wailEvents, listener, forward);
        require(directorEngine.ActiveVoices() == voicesBeforeWail + 1,
                "A replacement Wail speaker must start one clean loop after world clear");

        const auto secondWailId = activateAndPlaceWail();
        require(secondWailId != replacementWailId,
                "Two Wail speakers placed by one owner must not share a loop identity");
        tickUntilBlast(secondWailId);
        wailEvents = wailMatch.world().takeVisualEvents();
        require(std::count_if(wailEvents.begin(), wailEvents.end(), [&](const auto& event) {
                    return event.kind == "special_audio" && event.source == "wail_blast_start" && event.id == secondWailId;
                }) == 1, "Second Wail speaker from the same owner must start under its own identity");
        wailDirector.Update(wailDt, wailMatch, wailEvents, listener, forward);
        require(directorEngine.ActiveVoices() == voicesBeforeWail + 2,
                "Two simultaneous Wail speakers from one owner must keep two independent loops");

        wailDirector.Pause();
        std::this_thread::sleep_for(std::chrono::milliseconds(350));
        directorEngine.Update();
        const auto voicesWhilePaused = directorEngine.ActiveVoices();
        require(voicesWhilePaused <= voicesBeforeWail,
                "Pause must stop both Wail loops while retaining only the non-Wail baseline");
        wailDirector.Update(wailDt, wailMatch, {}, listener, forward);
        std::this_thread::sleep_for(std::chrono::milliseconds(350));
        directorEngine.Update();
        require(directorEngine.ActiveVoices() == voicesBeforeWail + 2,
                "Wail snapshot reconciliation must restore each loop once after pause without replaying a start cue");

        wailActor.state.invuln = 0.0;
        require(wailActor.damage(1000.0), "A lethal hit must exercise Wail speaker teardown");
        wailEvents = wailMatch.world().takeVisualEvents();
        require(std::count_if(wailEvents.begin(), wailEvents.end(), [](const auto& event) {
                    return event.kind == "special_audio" && event.source == "wail_blast_stop";
                }) == 2, "Wail death cleanup must stop both active speaker loops");
        wailDirector.Update(wailDt, wailMatch, wailEvents, listener, forward);
        const auto splatCue = directorEngine.Metadata("splatted_self");
        require(splatCue.has_value(), "Wail death teardown needs the local splat cue duration");
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>((splatCue->durationSeconds + 0.25) * 1000.0)));
        directorEngine.Update();
        require(directorEngine.ActiveVoices() == voicesBeforeWail,
                "Death cleanup must fade every Wail loop after the local splat cue ends");
        wailDirector.EndMatch();
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        directorEngine.Update();
        require(directorEngine.ActiveVoices() == 0,
                "Match teardown must clear the Wail loops and world ambience without leaving dangling voices");

        MatchOptions shakerOptions; shakerOptions.practice = true; shakerOptions.sub = "shaker";
        MatchSimulation shakerMatch(*stage, geometry, paint, nav, shakerOptions);
        GameAudio shakerDirector(directorEngine);
        shakerDirector.BeginMatch(shakerMatch);
        shakerDirector.Update(wailDt, shakerMatch, {}, listener, forward);
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>((wailIntroCueSeconds + 0.25) * 1000.0)));
        directorEngine.Update();
        shakerDirector.Update(wailDt, shakerMatch, {}, listener, forward);
        const auto voicesBeforeShaker = directorEngine.ActiveVoices();
        auto& shakerActor = shakerMatch.local();
        shakerActor.input.sub = true;
        for (int frame = 0; frame < 240 && shakerActor.combatPose().shakerCharge < 1.0; ++frame) {
            shakerMatch.tick(wailDt);
            const auto events = shakerMatch.world().takeVisualEvents();
            shakerDirector.Update(wailDt, shakerMatch, events, listener, forward);
        }
        require(shakerActor.combatPose().shakerCharging && shakerActor.combatPose().shakerCharge >= 1.0,
                "Local Shaker gameplay must hold a fully charged state while its loop is active");
        const auto maxClink = directorEngine.Metadata("shaker_clink_max");
        require(maxClink.has_value(), "Shaker loop lifecycle test needs the max-charge clink metadata");
        // The audio device advances in wall time independently of simulation ticks.
        const auto halfClink = directorEngine.Metadata("shaker_clink");
        require(halfClink.has_value(), "Shaker loop lifecycle test needs the half-charge clink metadata");
        std::this_thread::sleep_for(std::chrono::duration<double>(std::max(maxClink->durationSeconds, halfClink->durationSeconds) + 0.15));
        directorEngine.Update();
        require(directorEngine.ActiveVoices() == voicesBeforeShaker + 1,
                "Held Shaker should keep one fizz loop after its threshold clinks finish");
        shakerDirector.Pause();
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        directorEngine.Update();
        require(directorEngine.ActiveVoices() == voicesBeforeShaker,
                "Pause must stop the held Shaker fizz loop");
        shakerDirector.Update(wailDt, shakerMatch, {}, listener, forward);
        require(directorEngine.ActiveVoices() == voicesBeforeShaker + 1,
                "The next update must restore one Shaker fizz loop without replaying a threshold clink");
        shakerDirector.Update(wailDt, shakerMatch, {}, listener, forward);
        require(directorEngine.ActiveVoices() == voicesBeforeShaker + 1,
                "Repeated held-charge snapshots must update one Shaker loop without duplicating sound");

        shakerActor.state.invuln = 0.0;
        require(shakerActor.damage(1000.0), "A lethal hit must exercise held Shaker audio cleanup");
        auto shakerDeathEvents = shakerMatch.world().takeVisualEvents();
        require(std::count_if(shakerDeathEvents.begin(), shakerDeathEvents.end(), [](const auto& event) {
                    return event.kind == "special_audio" && event.source == "shaker_fizzle";
                }) == 1, "A death between audio updates must preserve one Shaker fizzle event");
        shakerDirector.Update(wailDt, shakerMatch, shakerDeathEvents, listener, forward);
        const auto selfSplatCue = directorEngine.Metadata("splatted_self");
        const auto fizzleCue = directorEngine.Metadata("shaker_fizzle");
        require(selfSplatCue && fizzleCue, "Held Shaker death needs splat and fizzle cue metadata");
        const auto deathCueSeconds = std::max(selfSplatCue->durationSeconds, fizzleCue->durationSeconds);
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>((deathCueSeconds + 0.25) * 1000.0)));
        directorEngine.Update();
        require(directorEngine.ActiveVoices() == voicesBeforeShaker,
                "Death must stop the held Shaker fizz loop after its one-shot cues end");
        shakerDirector.EndMatch();
        std::this_thread::sleep_for(std::chrono::milliseconds(400));
        directorEngine.Update();
        require(directorEngine.ActiveVoices() == 0,
                "Shaker match teardown must stop the fizz loop and ambience without dangling voices");

        AudioLoopOptions zoneLoop;zoneLoop.volume=.1;zoneLoop.zoneHum=AudioLoopOptions::ZoneHumParameters{0,0};
        require(audio.StartLoop("zone-test","zone_hum",zoneLoop),"The procedural zone hum source must start on miniaudio");
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        zoneLoop.zoneHum=AudioLoopOptions::ZoneHumParameters{2,.9};
        require(audio.UpdateLoop("zone-test",zoneLoop),"Zone hum controls must update without restarting the source");
        audio.StopLoop("zone-test",0);audio.Clear();require(audio.ActiveVoices()==0,"Zone hum data sources must stop and clear safely");

        std::cout << "Audio source test passed: " << names.size()
                  << " cues, source loop metadata, repeat gaps, miniaudio loop/music lifecycle, GameAudio event routing and cleanup, 48-voice steal, and Clear.\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Audio engine test failed: " << e.what() << '\n';
        return 1;
    }
}
