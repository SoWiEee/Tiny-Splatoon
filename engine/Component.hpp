#pragma once
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <iostream>

class GameObject; // Forward declaration
class Shader;     // Forward declaration

class Component {
public:
    GameObject* gameObject = nullptr;

    virtual ~Component() = default;
    virtual void Start() {}
    virtual void Update(float deltaTime) {}
    virtual void Draw(Shader& shader) {}
};