#pragma once
#include "Weapon.h"

class ShooterWeapon : public Weapon {
public:
    ShooterWeapon(int team, glm::vec3 color)
        : Weapon(team, color, 0.1f, 0.02f) {
    }

protected:
    void FireLogic(glm::vec3 pos, glm::vec3 dir) override {
        SpawnInfo info;
        info.pos = pos;
        info.dir = dir;
        info.color = inkColor;
        info.team = teamID;
        info.speed = 25.0f;
        info.scale = 0.15f;
        pendingSpawns.push_back(info);
    }
};