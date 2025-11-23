#pragma once
#include <glad/glad.h>
#include <iostream>
#include <vector>

class InkMap {
public:
    unsigned int FBO;
    unsigned int textureID;
    int width, height;

    InkMap(int w, int h) : width(w), height(h) {
        Init();
    }

    ~InkMap() {
        glDeleteFramebuffers(1, &FBO);
        glDeleteTextures(1, &textureID);
    }

    void Init() {
        // 1. 建立 FBO
        glGenFramebuffers(1, &FBO);
        glBindFramebuffer(GL_FRAMEBUFFER, FBO);

        // 2. 建立 Texture (用來存顏色)
        glGenTextures(1, &textureID);
        glBindTexture(GL_TEXTURE_2D, textureID);

        // RGBA 格式，浮點數 (為了精確混合，雖然 unsigned byte 也可以)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        // 避免 UV 超出 0-1 時重複
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        // 3. 把 Texture 綁定到 FBO 的顏色輸出
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textureID, 0);

        // 檢查 FBO 是否完整
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            std::cout << "ERROR::FRAMEBUFFER:: Framebuffer is not complete!" << std::endl;

        // 4. 清空畫布為黑色 (完全透明/無墨水)
        glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void BindTexture(int slot = 0) {
        glActiveTexture(GL_TEXTURE0 + slot);
        glBindTexture(GL_TEXTURE_2D, textureID);
    }

    void EnableWriting() {
        glBindFramebuffer(GL_FRAMEBUFFER, FBO);
        glViewport(0, 0, width, height); // Viewport 必須設為貼圖大小
    }

    void DisableWriting(int screenW, int screenH) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, screenW, screenH); // Viewport 改回螢幕大小
    }
};