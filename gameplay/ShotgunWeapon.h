#pragma once
#include "Weapon.h"
#include <cstdlib>

class ShotgunWeapon : public Weapon {
public:
    ShotgunWeapon(int team, glm::vec3 color)
        : Weapon(team, color, 0.8f, 0.25f) {
    }

protected:
    void FireLogic(glm::vec3 pos, glm::vec3 dir) override {
        int pelletCount = 8;

        for (int i = 0; i < pelletCount; i++) {
            SpawnInfo info;
            info.pos = pos;

            float spreadX = ((rand() % 100) / 100.0f - 0.5f) * 0.4f;
            float spreadY = ((rand() % 100) / 100.0f - 0.5f) * 0.2f;

            glm::vec3 spreadDir = dir + glm::vec3(spreadX, spreadY, 0.0f) * 0.5f + glm::vec3(0, spreadY, 0);
            info.dir = glm::normalize(spreadDir);

            info.color = inkColor;
            info.team = teamID;
            info.speed = 20.0f + (rand() % 5);
            info.scale = 0.1f;

            pendingSpawns.push_back(info);
        }
    }
};