#pragma once
#include "../engine/GameObject.h"

class Entity : public GameObject {
public:
    int teamID = 0;

    Entity(std::string name) : GameObject(name) {}

    virtual ~Entity() {}
};