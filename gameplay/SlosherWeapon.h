#pragma once
#include "Weapon.h"

// 潑桶 (Slosher)
// 特色：中距離、高耗墨、拋物線軌跡、一次潑出一排
class SlosherWeapon : public Weapon {
public:
    // 射速慢 (0.45s)，耗墨高 (7.0f)
    SlosherWeapon(int team, glm::vec3 color)
        : Weapon(team, color, 1.0f, 0.25f) {
    }

protected:
    void FireLogic(glm::vec3 pos, glm::vec3 dir) override {

        // 潑桶一次會潑出 "一排" 墨水 (例如 6 顆)
        // 這 6 顆會在垂直方向上分佈：
        // - 最上面的一顆飛最遠 (Top)
        // - 最下面的一顆飛最近 (Bottom)
        // 這樣可以形成一道落下的墨水牆

        int blobCount = 6;

        // 基礎拋射角度：潑桶預設會往上丟，所以我們把瞄準方向的 Y 軸強制拉高
        // 這樣就算玩家看著地板，墨水也會先往上飛再掉下來
        glm::vec3 baseDir = dir;
        baseDir.y += 0.3f; // 強制上抬
        baseDir = glm::normalize(baseDir);

        for (int i = 0; i < blobCount; i++) {
            SpawnInfo info;
            info.pos = pos;
            info.color = inkColor;
            info.team = teamID;

            // 計算插值係數 (0.0 ~ 1.0)
            // 0.0 是最底下那顆(近)，1.0 是最上面那顆(遠)
            float t = (float)i / (float)(blobCount - 1);

            // --- 1. 速度變化 ---
            // 飛得遠的要快 (22.0)，飛得近的慢 (12.0)
            // 這樣會讓墨水在空中拉成一條線
            info.speed = 12.0f + (t * 10.0f);

            // --- 2. 角度變化 ---
            // 每一顆的角度稍微不同，製造垂直擴散
            // 越上面的顆粒，拋射角度越高
            glm::vec3 throwDir = baseDir;
            throwDir.y += t * 0.15f;

            // 稍微加一點點水平隨機擴散 (不要完全是一條直線，太死板)
            glm::vec3 spread = GetRandomSpread(0.02f);
            info.dir = glm::normalize(throwDir + spread);

            // --- 3. 大小變化 ---
            // 中間的墨水最大，頭尾稍微小一點 (像一個紡錘形)
            // 使用簡單的二次函數或是直接隨機
            if (i == blobCount - 1) {
                // 最頂端那顆最大 (攻擊判定核心)
                info.scale = 0.5f;
            }
            else {
                // 其他顆粒稍微小一點
                info.scale = 0.3f + (RandomFloat(0.0f, 0.1f));
            }

            pendingSpawns.push_back(info);
        }
    }
};