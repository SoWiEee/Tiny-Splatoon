#pragma once
#include <vector>
#include <memory>
#include <algorithm>
#include <string>
#include "Transform.h"

class GameObject {
public:
    Transform* transform;
    std::vector<std::unique_ptr<Component>> components;
    std::string name;

    GameObject(std::string name = "GameObject") : name(name) {
        transform = AddComponent<Transform>();
    }

    template <typename T, typename... Args>
    T* AddComponent(Args&&... args) {
        auto comp = std::make_unique<T>(std::forward<Args>(args)...);
        comp->gameObject = this;
        T* ptr = comp.get();
        components.push_back(std::move(comp));
        ptr->Start();
        return ptr;
    }

    template <typename T>
    T* GetComponent() {
        for (auto& comp : components) {
            if (dynamic_cast<T*>(comp.get()))
                return static_cast<T*>(comp.get());
        }
        return nullptr;
    }

    void Update(float dt) {
        for (auto& comp : components) comp->Update(dt);
    }

    void Draw(Shader& shader) {
        for (auto& comp : components) comp->Draw(shader);
    }
};