#pragma once
#include "Weapon.h"

class BowWeapon : public Weapon {
public:
    BowWeapon(int team, glm::vec3 color)
        : Weapon(team, color, 1.2f, 0.40f) {
    }

protected:
    void FireLogic(glm::vec3 pos, glm::vec3 dir) override {
        glm::vec3 dirs[] = {
            dir,
            glm::normalize(dir + glm::cross(dir, glm::vec3(0,1,0)) * 0.2f), // еk
            glm::normalize(dir - glm::cross(dir, glm::vec3(0,1,0)) * 0.2f)  // ек
        };

        for (int i = 0; i < 3; i++) {
            SpawnInfo info;
            info.pos = pos;
            info.dir = dirs[i];
            info.color = inkColor;
            info.team = teamID;
            info.speed = 35.0f;
            info.scale = 0.2f;

            pendingSpawns.push_back(info);
        }
    }
};