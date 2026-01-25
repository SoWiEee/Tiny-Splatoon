#pragma once
#include "Weapon.hpp"
#include <algorithm>

// 潑桶 (Slosher)
// 特色：中距離、高耗墨、拋物線軌跡、一次潑出一排
class SlosherWeapon : public Weapon {
public:
    // 射速慢，耗墨高
    SlosherWeapon(int team, glm::vec3 color)
        : Weapon(team, color, 0.5f, 0.2f) {
    }

protected:
    void FireLogic(glm::vec3 pos, glm::vec3 dir) override {

        int blobCount = 10;

        // 1. 基礎拋射角度：潑桶需要往上「甩」，所以 Y 軸抬升要明顯
        glm::vec3 baseDir = dir;
        baseDir.y += 0.25f; // 抬高角度，製造拋物線
        baseDir = glm::normalize(baseDir);

        // 2. 左右隨機偏移的基礎向量 (用來讓墨水不要呈一直線，稍微有點寬度)
        glm::vec3 right = glm::cross(baseDir, glm::vec3(0, 1, 0));

        for (int i = 0; i < blobCount; i++) {
            SpawnInfo info;
            info.color = inkColor;
            info.team = teamID;

            // t 代表從「尾端(腳邊)」到「頂端(最遠)」的進度 (0.0 ~ 1.0)
            float t = (float)i / (float)(blobCount - 1);

            // --- A. 速度與位置 ---
            // 尾端很慢(10.0)會直接落地，頂端很快(26.0)會飛很遠
            info.speed = 10.0f + (t * 16.0f);

            // --- B. 角度微調 ---
            // 頂端的子彈飛得直一點，底端的子彈稍微往下壓一點
            glm::vec3 finalDir = baseDir;
            finalDir.y += (t * 0.1f) - 0.05f;

            // 加入極微小的左右隨機 (潑桶還是比較準的，不要散太開)
            float spreadAmount = 0.05f;
            glm::vec3 spread = GetRandomSpread(spreadAmount);
            info.dir = glm::normalize(finalDir + spread);

            // --- C. 起始位置拉伸 ---
            // 飛得快的子彈，生成位置稍微往前挪一點
            float spawnOffset = t * 1.5f;
            info.pos = pos + (info.dir * spawnOffset);

            // --- D. 大小變化 ---
            // 頂端(Head)最大，中間稍粗，尾端(Tail)最小
            // 這樣最遠的那顆打人最痛、判定最大
            if (i == blobCount - 1) {
                info.scale = 0.85f; // 核心攻擊判定，最大
            }
            else {
                // 漸變大小：0.35 -> 0.6
                info.scale = 0.35f + (t * 0.25f);
                // 加入一點隨機讓形狀自然
                info.scale += RandomFloat(-0.05f, 0.05f);
            }

            pendingSpawns.push_back(info);
        }
    }
};