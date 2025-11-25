#pragma once
#include "SplatMap.h"
#include "../engine/rendering/Shader.h"

class SplatPainter {
private:
    Shader* paintShader;
    unsigned int quadVAO, quadVBO;

public:
    SplatPainter() {
        paintShader = new Shader("assets/shaders/paint.vert", "assets/shaders/paint.frag");
        InitQuad();
    }

    ~SplatPainter() {
        delete paintShader;
        glDeleteVertexArrays(1, &quadVAO);
        glDeleteBuffers(1, &quadVBO);
    }

    void Paint(SplatMap* map, const glm::vec2& uv, float size, const glm::vec3& color, float rotation, int teamID) {
        glBindFramebuffer(GL_FRAMEBUFFER, map->fbo);
        glViewport(0, 0, map->width, map->height);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        paintShader->Bind();
        paintShader->SetVec2("hitUV", uv);
        paintShader->SetFloat("brushSize", size);
        paintShader->SetVec3("paintColor", color);
        paintShader->SetFloat("rotation", rotation);

        glBindVertexArray(quadVAO);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);

        glDisable(GL_BLEND);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        map->UpdateCPUData(uv.x, uv.y, teamID);
    }

private:
    void InitQuad() {
        // 全螢幕/任意大小 Quad (-1 ~ 1)
        float quadVertices[] = {
            // positions   // texCoords
            -1.0f,  1.0f, 0.0f,  0.0f, 1.0f,
            -1.0f, -1.0f, 0.0f,  0.0f, 0.0f,
             1.0f, -1.0f, 0.0f,  1.0f, 0.0f,

            -1.0f,  1.0f, 0.0f,  0.0f, 1.0f,
             1.0f, -1.0f, 0.0f,  1.0f, 0.0f,
             1.0f,  1.0f, 0.0f,  1.0f, 1.0f
        };

        glGenVertexArrays(1, &quadVAO);
        glGenBuffers(1, &quadVBO);
        glBindVertexArray(quadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));

        glBindVertexArray(0);
    }
};