#pragma once
#include <vector>
#include <memory>
#include <type_traits>
#include "Transform.h"
#include "Component.h"

class GameObject {
public:
    Transform* transform;
    std::vector<std::unique_ptr<Component>> components;

    GameObject() {
        // 每個物件出生就自帶 Transform
        auto t = std::make_unique<Transform>();
        transform = t.get();
        AddComponent(std::move(t));
    }

    // 類似 Unity 的 AddComponent<T>()
    template <typename T, typename... Args>
    T* AddComponent(Args&&... args) {
        auto comp = std::make_unique<T>(std::forward<Args>(args)...);
        comp->gameObject = this;
        T* ptr = comp.get();
        components.push_back(std::move(comp));
        ptr->Start(); // 當組件加入時執行 Start
        return ptr;
    }

    // 類似 Unity 的 GetComponent<T>()
    template <typename T>
    T* GetComponent() {
        for (auto& comp : components) {
            if (T* ptr = dynamic_cast<T*>(comp.get())) {
                return ptr;
            }
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