#pragma once
#include "../engine/GameObject.h"

// 基本上 Entity 就是 GameObject
class Entity : public GameObject {
public:
    Entity(std::string name) : GameObject(name) {}
    // 可以加入 GetType(), GetTeam() 等通用介面
};