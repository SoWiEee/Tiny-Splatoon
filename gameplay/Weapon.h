#pragma once
#include <vector>
#include <glm/glm.hpp>
#include <GLFW/glfw3.h>
#include <cstdlib>

struct SpawnInfo {
    glm::vec3 pos;
    glm::vec3 dir;
    glm::vec3 color;
    int team;
    float speed;
    float scale;
};

// --- 基底武器類別 ---
class Weapon {
public:
    int teamID;
    glm::vec3 inkColor;

    float fireRate;      // 射速 (秒/發)
    float inkCost;       // 墨水消耗 (0.0 ~ 1.0)
    float lastFireTime = 0.0f;

    std::vector<SpawnInfo> pendingSpawns;

    Weapon(int team, glm::vec3 color, float rate, float cost)
        : teamID(team), inkColor(color), fireRate(rate), inkCost(cost) {
    }

    virtual ~Weapon() {}

    // 虛擬函式：每個武器自己決定怎麼生成子彈
    virtual bool Trigger(float dt, glm::vec3 nozzlePos, glm::vec3 aimDir, bool isFiring) {
        if (isFiring) {
            float currentTime = (float)glfwGetTime();
            if (currentTime - lastFireTime > fireRate) {
                FireLogic(nozzlePos, aimDir);
                lastFireTime = currentTime;
                return true; // 開火成功
            }
        }
        return false;
    }

protected:
    virtual void FireLogic(glm::vec3 pos, glm::vec3 dir) = 0;
};

// --- 1. 標準步槍 (Shooter) ---
// 射速快，耗墨少，單發直線
class ShooterWeapon : public Weapon {
public:
    ShooterWeapon(int team, glm::vec3 color)
        : Weapon(team, color, 0.1f, 0.02f) {
    } // 0.1秒一發，耗墨 2% (50發)

protected:
    void FireLogic(glm::vec3 pos, glm::vec3 dir) override {
        SpawnInfo info;
        info.pos = pos;
        info.dir = dir;
        info.color = inkColor;
        info.team = teamID;
        info.speed = 25.0f;
        info.scale = 0.15f; // 標準大小
        pendingSpawns.push_back(info);
    }
};

// --- 2. 霰彈槍 (Shotgun) ---
// 射速慢，耗墨高，一次射多發，有擴散
class ShotgunWeapon : public Weapon {
public:
    ShotgunWeapon(int team, glm::vec3 color)
        : Weapon(team, color, 0.8f, 0.25f) {
    } // 0.8秒一發，耗墨 25% (4發)

protected:
    void FireLogic(glm::vec3 pos, glm::vec3 dir) override {
        int pelletCount = 8; // 一次射 8 顆

        for (int i = 0; i < pelletCount; i++) {
            SpawnInfo info;
            info.pos = pos;

            // 計算隨機擴散 (Spread)
            // 產生一個隨機偏移向量
            float spreadX = ((rand() % 100) / 100.0f - 0.5f) * 0.4f; // 水平擴散大
            float spreadY = ((rand() % 100) / 100.0f - 0.5f) * 0.2f; // 垂直擴散小

            glm::vec3 spreadDir = dir + glm::vec3(spreadX, spreadY, 0.0f) * 0.5f + glm::vec3(0, spreadY, 0);
            // 注意：簡單加法可能會影響長度，正規化一下
            info.dir = glm::normalize(spreadDir);

            info.color = inkColor;
            info.team = teamID;
            info.speed = 20.0f + (rand() % 5); // 速度有些微差異
            info.scale = 0.1f; // 子彈較小

            pendingSpawns.push_back(info);
        }
    }
};

// --- 3. 弓箭 (Bow) ---
// 射速中等，耗墨極高，一次射 3 發 (左中右)，受重力影響大
class BowWeapon : public Weapon {
public:
    BowWeapon(int team, glm::vec3 color)
        : Weapon(team, color, 1.2f, 0.40f) {
    } // 1.2秒一發，耗墨 40% (2-3發)

protected:
    void FireLogic(glm::vec3 pos, glm::vec3 dir) override {
        // 射出三支箭：中心、左偏、右偏
        glm::vec3 dirs[] = {
            dir,
            glm::normalize(dir + glm::cross(dir, glm::vec3(0,1,0)) * 0.2f), // 右
            glm::normalize(dir - glm::cross(dir, glm::vec3(0,1,0)) * 0.2f)  // 左
        };

        for (int i = 0; i < 3; i++) {
            SpawnInfo info;
            info.pos = pos;
            info.dir = dirs[i];
            info.color = inkColor;
            info.team = teamID;
            info.speed = 35.0f; // 箭速很快
            info.scale = 0.2f;  // 箭比較大

            pendingSpawns.push_back(info);
        }
    }
};