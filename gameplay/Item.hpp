#pragma once
#include "../engine/GameObject.h"
#include "../components/MeshRenderer.h"
#include <glm/glm.hpp>
#include <cmath>

enum class ItemType {
    BOMB
};

class Item : public GameObject {
public:
    ItemType type;
    float rotationSpeed = 90.0f; // 自轉速度
    float startY;
    float bobTimer = 0.0f;

    Item(glm::vec3 pos, ItemType t) : GameObject("Item") {
        transform->position = pos;
        startY = pos.y;
        type = t;

        // 使用黑色小方塊代表炸彈
        AddComponent<MeshRenderer>("Cube", glm::vec3(0.1f, 0.1f, 0.1f));
        transform->scale = glm::vec3(1.0f); // 大小
    }

    void Update(float dt) {
        // 1. 自轉
        transform->rotation.y += rotationSpeed * dt;

        // 2. 上下漂浮 (Bobbing)
        bobTimer += dt;
        transform->position.y = startY + sin(bobTimer * 2.0f) * 0.2f;
    }
};