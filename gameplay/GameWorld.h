#pragma once
#include "../scene/Level.h"
#include "../splat/SplatMap.h"
#include "../splat/SplatPainter.h"
#include "Projectile.h"
#include "Player.h"

class GameWorld {
public:
    Level* level;
    SplatMap* splatMap;
    SplatPainter* painter;
    Player* localPlayer;

    std::vector<Projectile*> projectiles;
    std::vector<Player*> players;

    void Init(GameObject* mainCamera) {
        level = new Level();
        level->Load();

        splatMap = new SplatMap(1024, 1024);
        painter = new SplatPainter();
        localPlayer = new Player(glm::vec3(0, 0, 0), 1, splatMap, mainCamera);
    }

    void Update(float dt) {
        localPlayer->UpdateLogic(dt);

        // bullet collect
        for (const auto& spawn : localPlayer->weapon.pendingSpawns) {
            Projectile* p = new Projectile(spawn.dir * 20.0f, spawn.color, spawn.team);
            p->transform->position = spawn.pos;
            projectiles.push_back(p);
        }
        localPlayer->weapon.pendingSpawns.clear();

		// physics update & collision
        for (auto it = projectiles.begin(); it != projectiles.end(); ) {
            Projectile* p = *it;
            p->UpdatePhysics(dt);

            // 處理撞擊事件 (Gameplay -> Splat 溝通)
            if (p->hasHitFloor) {
                // 使用 SplatPhysics 計算 UV
                auto result = SplatPhysics::WorldToUV(p->hitPosition, level->floor);

                if (result.hit) {
                    // 呼叫畫家塗地
                    painter->Paint(splatMap, result.uv, 0.1f, p->inkColor, 0.0f);
                }

                delete p;
                it = projectiles.erase(it);
            }
            else {
                ++it;
            }
        }
    }

    // 用於 main.cpp 渲染迴圈
    void Render(Shader& shader) {
        // 綁定 SplatMap 到 shader
        shader.SetInt("inkMap", 1);
        splatMap->BindTexture(1);

        // 渲染場景
        // ... level->floor->Draw(shader)...
        // ... projectiles->Draw(shader)...

        localPlayer->GetVisualBody()->Draw(shader);
    }
};