#pragma once
#include "../engine/Component.h"
#include <iostream>

class Health : public Component {
public:
    float maxHP = 100.0f;
    float currentHP;
    int teamID; // 1=紅隊(玩家), 2=綠隊(AI)

    // 重生點 (死了要回這裡)
    glm::vec3 spawnPoint;

    Health(int team, glm::vec3 spawn) : teamID(team), spawnPoint(spawn) {
        currentHP = maxHP;
    }

    void TakeDamage(float amount) {
        currentHP -= amount;
        std::cout << "Unit took damage! HP: " << currentHP << std::endl;

        if (currentHP <= 0) {
            Die();
        }
    }

    void Die() {
        std::cout << "Unit Died! Respawning..." << std::endl;
        // 簡單重生機制：補滿血，回到出生點
        currentHP = maxHP;
        if (gameObject) {
            gameObject->transform->position = spawnPoint;
        }
    }

    // 簡單的回血機制 (例如潛水時可以回血，之後可擴充)
    void Heal(float amount) {
        currentHP += amount;
        if (currentHP > maxHP) currentHP = maxHP;
    }
};