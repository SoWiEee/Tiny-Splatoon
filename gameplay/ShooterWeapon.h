#pragma once
#include <cstdlib>
#include "Weapon.h"

class ShooterWeapon : public Weapon {
public:
    ShooterWeapon(int team, glm::vec3 color)
        : Weapon(team, color, 0.1f, 0.04f) {
    }

protected:
    void FireLogic(glm::vec3 pos, glm::vec3 dir) override {

        // --- 1. 主墨水 (Main Blob) ---
        // 稍微有一點點擴散，不要像雷射那麼直
        glm::vec3 mainSpread = GetRandomSpread(0.02f);
        glm::vec3 mainDir = glm::normalize(dir + mainSpread);

        SpawnInfo mainInfo;
        mainInfo.pos = pos;
        mainInfo.dir = mainDir;
        mainInfo.color = inkColor;
        mainInfo.team = teamID;
        mainInfo.speed = 25.0f; // 主墨水速度快

        // 隨機大小：讓主墨水忽大忽小 (0.3 為基礎，倍率 0.9~1.2)
        mainInfo.scale = 0.3f * RandomFloat(0.9f, 1.2f);

        pendingSpawns.push_back(mainInfo);


        // --- 2. 小水花 (Droplets) - 營造體積感 ---
        // 每次發射額外產生 1~3 顆小水珠
        int dropletCount = 1 + (rand() % 3);

        for (int i = 0; i < dropletCount; i++) {
            SpawnInfo dropInfo;
            dropInfo.pos = pos;

            // 水花的擴散角度要大一點 (0.15f)
            glm::vec3 dropSpread = GetRandomSpread(0.15f);
            dropInfo.dir = glm::normalize(dir + dropSpread);

            dropInfo.color = inkColor;
            dropInfo.team = teamID;

            // 水花速度變化大一點 (18 ~ 30)，製造前後脫節的灑水感
            dropInfo.speed = RandomFloat(18.0f, 30.0f);

            // 水花比較小 (主墨水的 40% ~ 70%)
            dropInfo.scale = 0.3f * RandomFloat(0.4f, 0.7f);

            pendingSpawns.push_back(dropInfo);
        }
    }
};