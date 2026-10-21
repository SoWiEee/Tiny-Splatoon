#pragma once

#include "CharacterArt.hpp"
#include "../game/PlayerProfile.hpp"
#include <memory>
#include <vector>

namespace restoration {

class PlayerShowcase {
public:
    struct Viewport { int x=0, y=0, width=0, height=0; };
    struct PodiumPlayer { std::string name; CharacterStyle style; std::string main, sub; };

    explicit PlayerShowcase(std::filesystem::path directory="assets/reference/characters");
    ~PlayerShowcase();
    PlayerShowcase(const PlayerShowcase&)=delete;
    PlayerShowcase& operator=(const PlayerShowcase&)=delete;

    void drawPlayer(const PlayerProfile& profile,const std::string& main,const std::string& sub,
                    Viewport viewport,double time,bool reducedMotion=false);
    void drawPodium(const std::vector<PodiumPlayer>& roster,bool won,Viewport viewport,
                    double time,bool reducedMotion=false);

private:
    struct Actor;
    CharacterArt art_;
    std::vector<std::unique_ptr<Actor>> actors_;
};

} // namespace restoration
