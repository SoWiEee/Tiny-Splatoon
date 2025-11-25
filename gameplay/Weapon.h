#pragma once
#include <vector>
#include <glm/glm.hpp>
#include <GLFW/glfw3.h>

class Weapon {
public:
    float fireRate = 0.1f;
    float lastFireTime = 0.0f;

    int teamID;
    glm::vec3 inkColor;

    struct SpawnInfo {
        glm::vec3 pos;
        glm::vec3 dir;
        glm::vec3 color;
        int team;
    };

    std::vector<SpawnInfo> pendingSpawns;

    Weapon(int team, glm::vec3 color) : teamID(team), inkColor(color) {}

    // ¹Á¸Õ¶}¤õ
    void Trigger(float dt, glm::vec3 nozzlePos, glm::vec3 aimDir, bool isFiring) {
        if (isFiring) {
            float currentTime = (float)glfwGetTime();
            if (currentTime - lastFireTime > fireRate) {

                SpawnInfo info;
                info.pos = nozzlePos;
                info.dir = aimDir;
                info.color = inkColor;
                info.team = teamID;

                pendingSpawns.push_back(info);

                lastFireTime = currentTime;
            }
        }
    }
};