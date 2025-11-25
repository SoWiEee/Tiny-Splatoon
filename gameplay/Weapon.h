#pragma once
#include <vector>
#include <glm/glm.hpp>
#include <GLFW/glfw3.h>

class Weapon {
public:
    float fireRate = 0.1f;
    float timer = 0.0f;
    int teamID;
    glm::vec3 inkColor;

    struct SpawnInfo {
        glm::vec3 pos;
        glm::vec3 dir;
        glm::vec3 color;
        int team;
    };

    // 武器回傳「我要生成子彈」的請求，而不是直接 new GameObject
    std::vector<SpawnInfo> pendingSpawns;

    Weapon(int team, glm::vec3 color) : teamID(team), inkColor(color) {}

    void Trigger(float dt, glm::vec3 nozzlePos, glm::vec3 aimDir, bool isFiring) {
        if (isFiring) {
            float currentTime = (float)glfwGetTime(); // 或傳入 time
            if (currentTime - timer > fireRate) {

                SpawnInfo info;
                info.pos = nozzlePos;
                info.dir = aimDir;
                info.color = inkColor;
                info.team = teamID;

                pendingSpawns.push_back(info);
                timer = currentTime;
            }
        }
    }
};