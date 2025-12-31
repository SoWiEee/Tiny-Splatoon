#pragma once
#include <vector>
#include <memory>
#include <algorithm>
#include <string>
#include <glm/gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include "Transform.h"

class GameObject {
public:
    Transform* transform;
    std::vector<std::unique_ptr<Component>> components;
    std::string name;
    bool active = true;

    // 紀錄父物件與子物件
    GameObject* parent = nullptr;
    std::vector<GameObject*> children;

    GameObject(std::string name = "GameObject") : name(name) {
        transform = AddComponent<Transform>();
    }

    // [新增] 設定父物件
    void SetParent(GameObject* newParent) {
        // 1. 如果原本有父物件，先從原本父物件的 children 移除自己 (防止重複參照)
        if (this->parent) {
            auto& kids = this->parent->children;
            kids.erase(std::remove(kids.begin(), kids.end(), this), kids.end());
        }

        // 2. 設定新的父物件
        this->parent = newParent;

        // 3. 更新 Transform 層級 (這是數學運算的關鍵)
        if (newParent) {
            newParent->children.push_back(this);
            this->transform->SetParent(newParent->transform);
        }
        else {
            this->transform->SetParent(nullptr);
        }
    }

    // [新增] 為了方便，也提供傳入 raw pointer 的版本 (如果需要的話)
    void SetParent(GameObject* newParent, bool keepWorldTransform) {
        // 簡單版先只呼叫上面的，進階引擎會在這裡處理座標轉換，讓物體視覺位置不變
        SetParent(newParent);
    }

    // [新增] 取得子組件 (例如用來改顏色)
    template <typename T>
    std::vector<T*> GetComponentsInChildren() {
        std::vector<T*> results;

        // 1. 找自己的
        T* myComp = GetComponent<T>();
        if (myComp) results.push_back(myComp);

        // 2. 找所有子物件的 (遞迴)
        for (auto* child : children) {
            auto childRes = child->GetComponentsInChildren<T>();
            results.insert(results.end(), childRes.begin(), childRes.end());
        }
        return results;
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
        if (!active) return;

        for (auto& comp : components) comp->Update(dt);
    }

    void Draw(Shader& shader) {
        if (!active) return;

        for (auto& comp : components) comp->Draw(shader);
    }
};