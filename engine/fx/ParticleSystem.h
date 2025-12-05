#pragma once
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glad/glad.h>
#include <vector>
#include <algorithm>
#include "../rendering/Shader.h"

struct Particle {
    glm::vec3 position;
    glm::vec3 velocity;
    glm::vec3 color;
    float life;      // 剩餘壽命
    float startLife; // 總壽命 (用來計算淡出)
    float size;
};

class ParticleSystem {
private:
    // 粒子池
    std::vector<Particle> particles;
    unsigned int vao, vbo, instanceVBO;
    Shader* shader;

    // 用來傳給 GPU 的實例資料
    struct InstanceData {
        glm::vec3 offset;
        glm::vec3 color;
        float scale;
    };

public:
    ParticleSystem() {
        shader = new Shader("assets/shaders/particle.vert", "assets/shaders/particle.frag");
        InitRenderData();
    }

    ~ParticleSystem() {
        delete shader;
        glDeleteVertexArrays(1, &vao);
        glDeleteBuffers(1, &vbo);
        glDeleteBuffers(1, &instanceVBO);
    }

    // 發射粒子 (爆炸效果)
    void Emit(glm::vec3 center, glm::vec3 color, int count, float speed) {
        for (int i = 0; i < count; i++) {
            Particle p;
            p.position = center;

            // 隨機擴散速度
            float rx = ((rand() % 100) / 50.0f) - 1.0f; // -1 ~ 1
            float ry = ((rand() % 100) / 50.0f) + 1.0f; // 0 ~ 2 (稍微向上)
            float rz = ((rand() % 100) / 50.0f) - 1.0f; // -1 ~ 1

            p.velocity = glm::normalize(glm::vec3(rx, ry, rz)) * speed;
            // 加入一點隨機性
            p.velocity *= (0.5f + (rand() % 100) / 100.0f);

            p.color = color;
            p.life = 0.5f + ((rand() % 100) / 200.0f); // 0.5 ~ 1.0 秒
            p.startLife = p.life;
            p.size = 0.15f; // 粒子大小

            particles.push_back(p);
        }
    }

    void Update(float dt) {
        for (auto it = particles.begin(); it != particles.end(); ) {
            // 物理更新
            it->life -= dt;
            if (it->life <= 0) {
                it = particles.erase(it);
                continue;
            }

            // 重力
            it->velocity.y -= 9.8f * dt;
            it->position += it->velocity * dt;

            // 隨時間變小
            // it->size = 0.15f * (it->life / it->startLife);

            ++it;
        }
    }

    void Draw(const glm::mat4& view, const glm::mat4& projection) {
        if (particles.empty()) return;

        shader->Bind();
        shader->SetMat4("view", view);
        shader->SetMat4("projection", projection);

        // 準備實例資料
        std::vector<InstanceData> data;
        data.reserve(particles.size());
        for (const auto& p : particles) {
            data.push_back({ p.position, p.color, p.size });
        }

        // 更新 Instance VBO
        glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
        glBufferData(GL_ARRAY_BUFFER, data.size() * sizeof(InstanceData), data.data(), GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        // 繪製
        glBindVertexArray(vao);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 36, (GLsizei)particles.size());
        glBindVertexArray(0);
    }

private:
    void InitRenderData() {
        // 簡單的 Cube 頂點
        float vertices[] = {
            // 背面
            -0.5f, -0.5f, -0.5f,  0.5f, -0.5f, -0.5f,  0.5f,  0.5f, -0.5f,
             0.5f,  0.5f, -0.5f, -0.5f,  0.5f, -0.5f, -0.5f, -0.5f, -0.5f,
             // 正面
             -0.5f, -0.5f,  0.5f,  0.5f, -0.5f,  0.5f,  0.5f,  0.5f,  0.5f,
              0.5f,  0.5f,  0.5f, -0.5f,  0.5f,  0.5f, -0.5f, -0.5f,  0.5f,
              // 左面
              -0.5f,  0.5f,  0.5f, -0.5f,  0.5f, -0.5f, -0.5f, -0.5f, -0.5f,
              -0.5f, -0.5f, -0.5f, -0.5f, -0.5f,  0.5f, -0.5f,  0.5f,  0.5f,
              // 右面
               0.5f,  0.5f,  0.5f,  0.5f,  0.5f, -0.5f,  0.5f, -0.5f, -0.5f,
               0.5f, -0.5f, -0.5f,  0.5f, -0.5f,  0.5f,  0.5f,  0.5f,  0.5f,
               // 下面
               -0.5f, -0.5f, -0.5f,  0.5f, -0.5f, -0.5f,  0.5f, -0.5f,  0.5f,
                0.5f, -0.5f,  0.5f, -0.5f, -0.5f,  0.5f, -0.5f, -0.5f, -0.5f,
                // 上面
                -0.5f,  0.5f, -0.5f,  0.5f,  0.5f, -0.5f,  0.5f,  0.5f,  0.5f,
                 0.5f,  0.5f,  0.5f, -0.5f,  0.5f,  0.5f, -0.5f,  0.5f, -0.5f
        };

        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &instanceVBO);

        glBindVertexArray(vao);

        // VBO: 頂點資料
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

        // Instance VBO: 實例資料 (Offset, Color, Scale)
        glBindBuffer(GL_ARRAY_BUFFER, instanceVBO);
        // 先預分配一點空間，反正之後會 glBufferData 覆蓋
        glBufferData(GL_ARRAY_BUFFER, 1000 * sizeof(InstanceData), nullptr, GL_DYNAMIC_DRAW);

        // 設定 Instance Attribute (位置 1, 2, 3)
        // Offset (Vec3)
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(InstanceData), (void*)0);
        glVertexAttribDivisor(1, 1); // 告訴 OpenGL 這是 Instance 屬性

        // Color (Vec3)
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(InstanceData), (void*)(offsetof(InstanceData, color)));
        glVertexAttribDivisor(2, 1);

        // Scale (Float)
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(InstanceData), (void*)(offsetof(InstanceData, scale)));
        glVertexAttribDivisor(3, 1);

        glBindVertexArray(0);
    }
};