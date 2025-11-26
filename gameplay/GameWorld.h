#pragma once

#include "../scene/Level.h"
#include "../splat/SplatMap.h"
#include "../splat/SplatPainter.h"
#include "../splat/SplatPhysics.h"
#include "../splat/SplatRenderer.h"
#include "Player.h"
#include "Enemy.h"
#include "Projectile.h"
#include "../components/Scoreboard.h"
#include "../components/Health.h"

class GameWorld {
public:
    // settings
    Level* level;
    SplatMap* splatMap;
    SplatPainter* painter;

    // instance lists
    Player* localPlayer;
    Enemy* enemyAI;
    std::vector<Projectile*> projectiles;

    void Init(GameObject* mainCamera, HUD* hud) {
        level = new Level();
        level->Load();

        splatMap = new SplatMap(1024, 1024);
        painter = new SplatPainter();

        // create player
        localPlayer = new Player(glm::vec3(-5, 2.0f, -5), 1, splatMap, mainCamera, hud);
        enemyAI = new Enemy(glm::vec3(5, 0, 5), 2);
    }

	// AABB collision check
    bool CheckCollision(GameObject* bullet, GameObject* target) {
        glm::vec3 posB = bullet->transform->position;
        glm::vec3 posT = target->transform->position;

        glm::vec3 centerT = posT + glm::vec3(0, 1.0f, 0);

        float dist = glm::distance(posB, centerT);
        return dist < 1.0f;
    }

    void Update(float dt) {
        localPlayer->UpdateLogic(dt);
        CollectProjectiles(localPlayer->weapon);
        enemyAI->UpdateLogic(dt);
        CollectProjectiles(enemyAI->weapon);

		// bullet spawning
        for (const auto& info : localPlayer->weapon.pendingSpawns) {
            glm::vec3 velocity = info.dir * 25.0f; // 速度快一點
            velocity.y += 3.0f; // 稍微上拋

            Projectile* p = new Projectile(velocity, info.color, info.team);
            p->transform->position = info.pos;
            projectiles.push_back(p);
        }
        localPlayer->weapon.pendingSpawns.clear();

        // 3. 更新所有子彈 & 處理塗地
        for (auto it = projectiles.begin(); it != projectiles.end(); ) {
            Projectile* p = *it;
            p->UpdatePhysics(dt);

            bool hitSomething = false;

			// player collision
            // 建立一個目標清單 (目前只有兩個，以後可以用 vector 存所有 entity)
            Entity* targets[] = { localPlayer, enemyAI };

            for (Entity* target : targets) {
                // non-PVP
                Health* hp = target->GetComponent<Health>();
                if (hp && hp->teamID != p->ownerTeam) {
                    if (CheckCollision(p, target)) {
                        hp->TakeDamage(10.0f);
                        hitSomething = true;
                        break;
                    }
                }
            }

            if (hitSomething) {
                delete p;
                it = projectiles.erase(it);
                continue;
            }

            if (p->hasHitFloor) {
                // 使用物理系統計算 UV
                auto result = SplatPhysics::WorldToUV(
                    p->hitPosition,
                    level->floor->transform->position,
                    level->floor->width,
                    level->floor->depth
                );

                if (result.hit) {
                    // 隨機旋轉墨漬
                    float rot = (float)(rand() % 360) * 3.14159f / 180.0f;
                    float size = 0.08f + ((rand() % 100) / 500.0f); // 隨機大小

                    painter->Paint(splatMap, result.uv, size, p->inkColor, rot, p->ownerTeam);
                }

                delete p;
                it = projectiles.erase(it);
            }
            else if (p->isDead) {
                delete p;
                it = projectiles.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    void Render(Shader& shader) {
        SplatRenderer::RenderFloor(shader, level->floor, splatMap);

        shader.SetInt("useInk", 0);
        for (auto wall : level->walls) wall->Draw(shader);
        for (auto obs : level->obstacles) obs->Draw(shader);

        for (auto p : projectiles) p->Draw(shader);

        if (localPlayer->GetVisualBody()) {
            localPlayer->GetVisualBody()->Draw(shader);
        }
        if (enemyAI->GetVisualBody()) {
            enemyAI->GetVisualBody()->Draw(shader);
        }
    }

    void CollectProjectiles(Weapon& weapon) {
        for (const auto& info : weapon.pendingSpawns) {
            glm::vec3 velocity = info.dir * 25.0f;
            velocity.y += 3.0f;
            Projectile* p = new Projectile(velocity, info.color, info.team);
            p->transform->position = info.pos;
            projectiles.push_back(p);
        }
        weapon.pendingSpawns.clear();
    }

    void CleanUp() {
        delete level;
        delete splatMap;
        delete painter;
        delete localPlayer;
		delete enemyAI;
        for (auto p : projectiles) delete p;
    }
};