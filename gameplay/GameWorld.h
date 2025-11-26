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
        float bulletRadius = bullet->transform->scale.x * 0.5f;

        float dist = glm::distance(posB, centerT);

        // 判定距離 = 人身半徑 (0.5) + 子彈半徑
        return dist < (0.5f + bulletRadius);
    }

    void Update(float dt) {
        // Update players
        if (localPlayer) {
            localPlayer->UpdateLogic(dt);
            if (localPlayer->weapon) {
                CollectProjectiles(*(localPlayer->weapon));
            }
        }
        if (enemyAI) {
            enemyAI->UpdateLogic(dt);
            if (enemyAI->weapon) {
                CollectProjectiles(*(enemyAI->weapon));
            }
        }

        // 3. 更新所有子彈 & 處理塗地
        for (auto it = projectiles.begin(); it != projectiles.end(); ) {
            Projectile* p = *it;
            p->UpdatePhysics(dt);

            bool hitSomething = false;

			// player collision
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
                    float size = p->transform->scale.x * 0.3f;

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

        // 畫陰影
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        // 關閉深度寫入，避免陰影遮擋其他東西
        glDepthMask(GL_FALSE);

        if (localPlayer) {
            // 陰影跟隨玩家，但貼地
            glm::vec3 shadowPos = localPlayer->transform->position;
            shadowPos.y = 0.02f; // 永遠在地板上

            // 根據玩家高度縮放陰影 (跳起來時陰影變小)
            float height = localPlayer->transform->position.y;
            float scale = 1.5f - (height * 0.3f);
            if (scale < 0) scale = 0;

            // 我們需要存取 shadow 物件，建議 Player 提供 GetShadow()
            // 假設 localPlayer->shadow 是 public 或者有 getter
            GameObject* s = localPlayer->shadow;
            s->transform->position = shadowPos;
            s->transform->scale = glm::vec3(scale, 1.0f, scale);
            s->Draw(shader);
        }

        if (enemyAI) {
            glm::vec3 shadowPos = enemyAI->transform->position;
            shadowPos.y = 0.02f;

            float height = enemyAI->transform->position.y;
            float scale = 1.5f - (height * 0.3f);
            if (scale < 0) scale = 0;

            GameObject* s = enemyAI->shadow;
            s->transform->position = shadowPos;
            s->transform->scale = glm::vec3(scale, 1.0f, scale);
            s->Draw(shader);

            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }
    }

    void CollectProjectiles(Weapon& weapon) {
        for (const auto& info : weapon.pendingSpawns) {
            glm::vec3 velocity = info.dir * info.speed;
            velocity.y += 2.0f;

            Projectile* p = new Projectile(velocity, info.color, info.team, info.scale);
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