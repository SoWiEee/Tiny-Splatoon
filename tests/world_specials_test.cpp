#include "../src/game/PlayerSimulation.hpp"
#include "../src/game/WorldSpecials.hpp"
#include "../src/game/ArsenalData.hpp"
#include "fixtures/world_specials_source_fixtures.hpp"

#include <cmath>
#include <algorithm>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
double horizontalLength(const glm::dvec3& value) { return std::hypot(value.x, value.z); }
const restoration::test::SpecialSourceFixture& fixture(std::string_view id) {
    for (const auto& row : restoration::test::specialSourceFixtures) if (row.id == id) return row;
    throw std::runtime_error("Missing locked source special fixture");
}
void ready(restoration::PlayerSimulation& actor, std::string special) {
    actor.setLoadout("shooter", "bomb", std::move(special));
    actor.state.invuln = 0.0;
    actor.state.specialCharge = actor.weapon().number("specialCost", 190.0);
}
}

int main() {
    using namespace restoration;
    try {
        const StageData* stage = nullptr;
        for (const auto& candidate : ReferenceStages())
            if (candidate.id == "tidewater" && candidate.mode == "turf") stage = &candidate;
        if (!stage) throw std::runtime_error("Missing source Turf stage");
        StageGeometry geometry(*stage);
        FacePaint paint(*stage, geometry);
        CombatWorld world(*stage, geometry, paint);
        WorldSpecials specials(world);
        check(!specials.shotHit(glm::dvec3(0.0), {0.0, 0.0, 1.0}, 1, 20.0, nullptr),
              "An unsupported WorldSpecials object incorrectly absorbed a shot");
        check(std::abs(specials.rayHit(glm::dvec3(0.0), {0.0, 0.0, 1.0}, 5.0, 1, 20.0, nullptr) - 5.0) < 1e-12,
              "An unsupported WorldSpecials object incorrectly shortened a ray");
        specials.areaHit(glm::dvec3(0.0), 2.0, 20.0, 1, nullptr);
        auto slam = std::make_unique<PlayerSimulation>(*stage, geometry, paint, 0, &world);
        auto enemy = std::make_unique<PlayerSimulation>(*stage, geometry, paint, 1, &world);
        ready(*slam, "slam");
        slam->state.pos = stage->spawnPads[0];
        enemy->state.pos = slam->state.pos + glm::dvec3(0.8, 0.0, 0.0);
        enemy->state.invuln = 0.0;
        const auto& slamSource = fixture("slam");
        check(slamSource.frames > 0 && slamSource.flag2 == 1
              && std::abs(slamSource.aux0 - GetEquipment(EquipmentCategory::Special, "slam").number("radius")) < 1e-12,
              "Slam fixture did not lock the live source behavior/config");
        check(specials.activate(*slam, "slam"), "A charged Slam did not activate");
        check(slam->state.specialCharge == 0.0 && slam->state.ink == 100.0, "Special activation did not spend the gauge and refill ink");
        check(specials.pose(*slam).active && specials.pose(*slam).phase == "rise", "Slam did not enter its source rise phase");
        check(std::abs(specials.filterDamage(*slam, enemy.get(), 100.0, "shooter") - 25.0) < 1e-12,
              "Active Slam did not apply its source armor multiplier before other filters");
        for (int frame = 0; frame < 180 && specials.pose(*slam).active; ++frame)
            specials.body(*slam, 1.0 / 60.0);
        check(!specials.pose(*slam).active && slam->state.invuln >= 0.29, "Slam did not finish on impact");
        check(!enemy->state.alive, "Slam shockwave did not damage an enemy in range");
        check(slam->state.specialCharge == 0.0, "Slam special paint recharged its gauge");

        auto bubbler = std::make_unique<PlayerSimulation>(*stage, geometry, paint, 0, &world);
        auto ally = std::make_unique<PlayerSimulation>(*stage, geometry, paint, 0, &world);
        auto attacker = std::make_unique<PlayerSimulation>(*stage, geometry, paint, 1, &world);
        ready(*bubbler, "bubbler");
        bubbler->state.pos = stage->spawnPads[0];
        ally->state.pos = bubbler->state.pos + glm::dvec3(0.5, 0.0, 0.0);
        attacker->state.pos = bubbler->state.pos + glm::dvec3(0.0, 0.0, 1.0);
        check(specials.activate(*bubbler, "bubbler"), "A charged Bubbler did not activate");
        check(std::abs(bubbler->state.shield - fixture("bubbler").duration) < 1e-12, "Bubbler duration differs from source");
        specials.tickWorld(1.0 / 60.0);
        check(ally->state.shield > 6.4, "Bubbler did not share its shield with a nearby teammate");
        const auto before = bubbler->state.vel;
        const double filtered = specials.filterDamage(*bubbler, attacker.get(), 36.0, "shooter");
        check(filtered == 0.0 && horizontalLength(bubbler->state.vel - before) > 0.0,
              "Bubbler did not replace damage with source-scaled knockback");

        auto zooka = std::make_unique<PlayerSimulation>(*stage, geometry, paint, 0, &world);
        auto zookaVictim = std::make_unique<PlayerSimulation>(*stage, geometry, paint, 1, &world);
        ready(*zooka, "zooka");
        zooka->state.pos = stage->spawnPads[0];
        zooka->input.aimYaw = 0.0;
        zooka->input.aimPitch = 0.0;
        zooka->input.hasAimPoint = false;
        zookaVictim->state.pos = zooka->state.pos + glm::dvec3(0.0, 0.0, 5.0);
        zookaVictim->state.invuln = 0.0;
        check(specials.activate(*zooka, "zooka"), "A charged Zooka did not activate");
        bool sub = false, subReleased = false;
        check(specials.weapon(*zooka, 0.25, true, true, sub, subReleased), "Zooka did not consume main-weapon input");
        specials.tickWorld(0.1);
        specials.tickWorld(0.1);
        check(!zookaVictim->state.alive, "Zooka twister did not hit a player in its vertical column");
        check(zooka->state.specialCharge == 0.0, "Zooka special paint recharged its gauge");

        auto storm = std::make_unique<PlayerSimulation>(*stage, geometry, paint, 0, &world);
        ready(*storm, "storm");
        storm->state.pos = stage->spawnPads[0];
        storm->state.vel = {1.0, 0.0, -0.5};
        storm->input.aimYaw = 0.3;
        storm->input.aimPitch = 0.1;
        const auto& stormSource = fixture("storm");
        const double pitch = std::clamp(storm->input.aimPitch + 0.28, -0.3, 1.1);
        const double speed = GetEquipment(EquipmentCategory::Special, "storm").number("throwSpeed");
        const glm::dvec3 expectedStormVelocity(std::sin(storm->input.aimYaw) * std::cos(pitch) * speed + storm->state.vel.x * 0.4,
            std::sin(pitch) * speed + 1.5, std::cos(storm->input.aimYaw) * std::cos(pitch) * speed + storm->state.vel.z * 0.4);
        check(std::abs(stormSource.vx - expectedStormVelocity.x) < 1e-12
              && std::abs(stormSource.vy - expectedStormVelocity.y) < 1e-12
              && std::abs(stormSource.vz - expectedStormVelocity.z) < 1e-12,
              "Storm fixture no longer matches source throw velocity");
        check(specials.activate(*storm, "storm") && specials.pose(*storm).thrown,
              "Storm did not create its thrown source state");
        specials.body(*storm, 0.36);
        check(!specials.pose(*storm).active, "Storm actor body window did not end after the throw");
        storm->state.specialCharge = 7.0;
        specials.splat(*storm);
        check(storm->state.specialCharge == 7.0,
              "WorldSpecials cleanup overwrote PlayerSimulation's COMBAT06 gauge retention");

        const std::vector<std::pair<std::string, std::string>> barrages{
            {"barrage", "bomb"}, {"barrage_sticky", "sticky"}, {"barrage_burst", "burst"},
            {"barrage_seeker", "seeker"}, {"barrage_mist", "mist"},
        };
        for (const auto& [id, bombKind] : barrages) {
            auto barrage = std::make_unique<PlayerSimulation>(*stage, geometry, paint, 0, &world);
            ready(*barrage, id);
            barrage->state.pos = {2.0, 3.0, -4.0};
            barrage->state.vel = {1.0, 0.0, -0.5};
            barrage->state.ink = 0.0;
            barrage->input.aimYaw = 0.3;
            barrage->input.aimPitch = 0.1;
            const auto& source = fixture(id);
            check(source.payload == bombKind, "Barrage fixture did not capture its source bomb variant");
            check(std::abs(source.duration - GetEquipment(EquipmentCategory::Special, id).number("duration")) < 1e-12
                  && std::abs(source.interval - GetEquipment(EquipmentCategory::Special, id).number("gap")) < 1e-12,
                  "Barrage fixture duration or throw gap differs from the source config");
            const auto& subDef = GetEquipment(EquipmentCategory::Sub, bombKind);
            check(source.flag0 == 1 && source.flag1 == 1 && source.flag2 == 1 && source.frames == 2,
                  "Source WeaponRunner did not accept the first throw and the throw at the source gap");
            check(std::abs(source.aux1 - subDef.number("throwSpeed")) < 1e-12
                  && std::abs(source.aux2 - subDef.number("radius")) < 1e-12,
                  "Barrage fixture did not lock its source sub-weapon throw tuning");
            check(specials.activate(*barrage, id), "Charged Barrage variant did not activate");
            check(barrage->state.specialCharge == 0.0 && barrage->state.ink == 100.0,
                  "Barrage activation did not spend its gauge and refill ink");

            const auto initialDeviceCount = world.devices().size();
            bool sub = false, subReleased = true;
            (void)world.takeVisualEvents();
            check(!specials.weapon(*barrage, 1.0 / 60.0, false, false, sub, subReleased),
                  "Barrage consumed main-weapon input");
            check(!sub && !subReleased && world.devices().size() == initialDeviceCount + 1,
                  "Barrage did not intercept the sub release and create one special device");
            auto throwEvents = world.takeVisualEvents();
            const auto throwCue = std::find_if(throwEvents.begin(), throwEvents.end(), [](const auto& event) {
                return event.kind == "special_audio" && event.source == "bomb_throw";
            });
            check(std::count_if(throwEvents.begin(), throwEvents.end(), [](const auto& event) {
                    return event.kind == "special_audio" && event.source == "bomb_throw";
                  }) == 1 && throwCue != throwEvents.end()
                  && glm::length(throwCue->pos - barrage->state.pos) < 1e-12
                  && throwCue->team == barrage->team()
                  && std::abs(throwCue->charge - (bombKind == "burst" ? 1.2 : 1.0)) < 1e-12,
                  "Barrage throw did not emit one positioned source cue with the subvariant pitch");
            const auto& first = world.devices().back();
            check(first.owner == barrage.get() && first.special && first.kind == bombKind && first.state == "fly",
                  "Barrage created a device with incorrect owner, kind, or special state");
            check(std::abs(first.radius - source.aux2) < 1e-12,
                  "Barrage device radius differs from the source sub weapon");
            check(std::abs(first.pos.x - source.x) < 1e-12 && std::abs(first.pos.y - source.y) < 1e-12
                  && std::abs(first.pos.z - source.z) < 1e-12
                  && std::abs(first.vel.x - source.vx) < 1e-12 && std::abs(first.vel.y - source.vy) < 1e-12
                  && std::abs(first.vel.z - source.vz) < 1e-12,
                  "Native Barrage throw position or velocity differs from the live source snapshot");
            check(barrage->state.ink == 100.0, "Barrage throw spent ink despite its source behavior");

            const auto afterFirst = world.devices().size();
            subReleased = true;
            specials.weapon(*barrage, source.interval * 0.5, false, false, sub, subReleased);
            check(world.devices().size() == afterFirst && !subReleased,
                  "Barrage accepted a second throw before the source gap elapsed");
            subReleased = true;
            specials.weapon(*barrage, source.interval * 0.5 + 1e-6, false, false, sub, subReleased);
            check(world.devices().size() == afterFirst + 1,
                  "Barrage did not accept another throw after the source gap elapsed");
            throwEvents = world.takeVisualEvents();
            check(std::count_if(throwEvents.begin(), throwEvents.end(), [](const auto& event) {
                    return event.kind == "special_audio" && event.source == "bomb_throw";
                  }) == 1,
                  "Barrage follow-up throw did not emit its own source cue");

            barrage->state.specialCharge = 13.0;
            specials.splat(*barrage);
            check(barrage->state.specialCharge == 13.0 && !specials.pose(*barrage).active,
                  "Splat cleanup altered gauge retention or left Barrage active");
            // COMBAT06 deliberately removes owner-owned native special devices on death; the source can leave thrown bombs alive.
            for (const auto& device : world.devices())
                if (device.owner == barrage.get() && device.special)
                    check(!device.live, "Native death cleanup left a Barrage device alive");
        }

        const std::vector<std::string> remaining{
            "strike", "blower", "jetpack", "stamp", "booyah", "zipcaster", "crab",
        };
        for (const auto& id : remaining) {
            PlayerSimulation actor(*stage, geometry, paint, 0, &world);
            ready(actor, id);
            actor.state.specialCharge = 0.0;
            actor.state.ink = 17.0;
            actor.state.squid = true;
            actor.state.submerged = true;
            actor.state.climbing = true;
            actor.state.vel = {1.0, 2.0, -3.0};
            const auto pos = actor.state.pos;
            const auto vel = actor.state.vel;
            const double charge = actor.state.specialCharge;
            check(!specials.activate(actor, id), "Uncharged special unexpectedly activated");
            check(actor.state.specialCharge == charge && actor.state.ink == 17.0 && actor.state.squid
                  && actor.state.submerged && actor.state.climbing && glm::length(actor.state.pos - pos) < 1e-12
                  && glm::length(actor.state.vel - vel) < 1e-12
                  && !specials.pose(actor).active,
                  "Uncharged special mutated actor state instead of failing without effects");
        }

        bool rejected = false;
        try { specials.activate(*slam, "not-a-special"); }
        catch (const std::invalid_argument&) { rejected = true; }
        check(rejected, "Unknown special ID did not fail explicitly");
        std::cout << "Core specials and Barrage source fixtures passed; uncharged remaining IDs fail without mutation\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
