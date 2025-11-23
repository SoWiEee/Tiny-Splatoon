#pragma once
#include <string>

class GameObject;

class Component {
public:
    GameObject* gameObject;

    virtual ~Component() {}

    virtual void Start() {}
    virtual void Update(float deltaTime) {}
    virtual void Draw(class Shader& shader) {}
};