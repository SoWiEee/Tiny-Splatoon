#pragma once
#include <vector>
#include <glm/glm.hpp>
#include <GLFW/glfw3.h>
#include <cstdlib> // rand

struct SpawnInfo {
    glm::vec3 pos;
    glm::vec3 dir;
    glm::vec3 color;
    int team;
    float speed;
    float scale;
};

class Weapon {
public:
    int teamID;
    glm::vec3 inkColor;

    float fireRate;
    float inkCost;
    float lastFireTime = 0.0f;

    std::vector<SpawnInfo> pendingSpawns;

    Weapon(int team, glm::vec3 color, float rate, float cost)
        : teamID(team), inkColor(color), fireRate(rate), inkCost(cost) {
    }

    virtual ~Weapon() {}

    virtual bool Trigger(float dt, glm::vec3 nozzlePos, glm::vec3 aimDir, bool isFiring) {
        if (isFiring) {
            float currentTime = (float)glfwGetTime();
            if (currentTime - lastFireTime > fireRate) {
                FireLogic(nozzlePos, aimDir);
                lastFireTime = currentTime;
                return true;
            }
        }
        return false;
    }

protected:
    virtual void FireLogic(glm::vec3 pos, glm::vec3 dir) = 0;

    float RandomFloat(float min, float max) {
        return min + static_cast<float>(rand()) / (static_cast<float>(RAND_MAX / (max - min)));
    }

    glm::vec3 GetRandomSpread(float amount) {
        float x = RandomFloat(-1.0f, 1.0f);
        float y = RandomFloat(-1.0f, 1.0f);
        float z = RandomFloat(-1.0f, 1.0f);
        return glm::vec3(x, y, z) * amount;
    }
};