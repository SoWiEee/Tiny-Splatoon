#pragma once

#include "CharacterArt.hpp"
#include "../game/PlayerProfile.hpp"
#include <array>
#include <memory>
#include <string>
#include <vector>

namespace restoration {

class PlayerShowcase {
public:
    struct Viewport { int x=0, y=0, width=0, height=0; };
    struct PodiumPlayer { std::string name; CharacterStyle style; std::string main, sub; };
    struct PodiumMotion {
        double drumOffset=0.0,playerOffset=0.0,velocityY=0.0,squash=1.0;
        bool visible=true,airborne=false,landed=false;
    };

    explicit PlayerShowcase(std::filesystem::path directory="assets/reference/characters");
    ~PlayerShowcase();
    PlayerShowcase(const PlayerShowcase&)=delete;
    PlayerShowcase& operator=(const PlayerShowcase&)=delete;

    void drawPlayer(const PlayerProfile& profile,const std::string& main,const std::string& sub,
                    Viewport viewport,double time,bool reducedMotion=false);
    void beginResults(double startTime);
    void drawPodium(const std::vector<PodiumPlayer>& roster,bool won,Viewport viewport,
                    double time,bool reducedMotion=false);
    static PodiumMotion podiumMotion(double elapsed,std::size_t slot,bool won,bool reducedMotion=false);

private:
    struct Actor;
    struct Stage;
    std::filesystem::path directory_;
    CharacterArt art_;
    std::vector<std::unique_ptr<Actor>> actors_;
    std::unique_ptr<Stage> stage_;
    double resultStartTime_=0.0;
    bool resultsBegun_=false;
    bool podiumWon_=false,hasPodiumKey_=false;
    std::vector<std::string> podiumNames_;
};

} // namespace restoration
