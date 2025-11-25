#pragma once

#include "../scene/Level.h"
#include "../splat/SplatMap.h"
#include "../splat/SplatPainter.h"
#include "../splat/SplatPhysics.h"
#include "../splat/SplatRenderer.h"
#include "Player.h"
#include "Projectile.h"
#include "../components/Scoreboard.h"

class GameWorld {
public:
    // --- 系統 ---
    Level* level;
    SplatMap* splatMap;
    SplatPainter* painter;

    // --- 實體清單 ---
    Player* localPlayer;
    std::vector<Projectile*> projectiles;

    // --- 初始化 ---
    void Init(GameObject* mainCamera, HUD* hud) {
        level = new Level();
        level->Load();

        splatMap = new SplatMap(1024, 1024);
        painter = new SplatPainter();

        // create player
        localPlayer = new Player(glm::vec3(-5, 2.0f, -5), 1, splatMap, mainCamera, hud);
    }

    // --- 主邏輯迴圈 ---
    void Update(float dt) {
        // 1. 更新玩家
        localPlayer->UpdateLogic(dt);

        // 2. 處理玩家武器生成的子彈
        // 將 pendingSpawns 轉換為 Projectile 物件
        for (const auto& info : localPlayer->weapon.pendingSpawns) {
            // 計算初速度 (拋物線)
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

            // 更新物理
            p->UpdatePhysics(dt);

            // 檢查是否撞到地板
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

                    // 執行塗地
                    painter->Paint(splatMap, result.uv, size, p->inkColor, rot, p->ownerTeam);
                }

                // 任務完成，刪除子彈
                delete p;
                it = projectiles.erase(it);
            }
            else if (p->isDead) {
                // 其他原因死亡 (掉出邊界)
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
    }

    void CleanUp() {
        delete level;
        delete splatMap;
        delete painter;
        delete localPlayer;
        for (auto p : projectiles) delete p;
    }
};