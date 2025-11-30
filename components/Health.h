#pragma once
#include "../engine/Component.h"
#include <iostream>

class Health : public Component {
public:
    float maxHP = 100.0f;
    float currentHP;
    int teamID; // 1=¬õ¶¤(ª±®a), 2=ºñ¶¤(AI)

    bool isDead = false;
    glm::vec3 spawnPoint;

    Health(int team, glm::vec3 spawn) : teamID(team), spawnPoint(spawn) {
        currentHP = maxHP;
    }

    void TakeDamage(float amount) {
        if (isDead) return;

        currentHP -= amount;

        if (currentHP <= 0) {
            currentHP = 0;
            if (!isDead) {
                Die();
            }
        }
    }

    void Die() {
        isDead = true;
        std::cout << "Unit Died! Waiting for respawn logic..." << std::endl;
    }

    // ­«¸mª¬ºA (·í¶W¯Å¸õÅD¶}©l®É©I¥s)
    void Reset() {
        isDead = false;
        currentHP = maxHP;
    }

    void Heal(float amount) {
        if (isDead) return;
        currentHP += amount;
        if (currentHP > maxHP) currentHP = maxHP;
    }
};