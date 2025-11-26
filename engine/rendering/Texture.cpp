#include "Texture.h"
#include <iostream>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"


Texture::Texture() : Width(0), Height(0), InternalFormat(GL_RGBA), ImageFormat(GL_RGBA), WrapS(GL_REPEAT), WrapT(GL_REPEAT), FilterMin(GL_LINEAR_MIPMAP_LINEAR), FilterMag(GL_LINEAR) {
    glGenTextures(1, &ID);
}

Texture::~Texture() {
    glDeleteTextures(1, &ID);
}

void Texture::Load(const std::string& path) {
    glBindTexture(GL_TEXTURE_2D, ID);

    int width, height, nrChannels;
    // OpenGL 的 UV 原點在左下角，圖片通常在左上角，所以要翻轉
    stbi_set_flip_vertically_on_load(true);

    unsigned char* data = stbi_load(path.c_str(), &width, &height, &nrChannels, 0);

    if (data) {
        InternalFormat = GL_RGBA;
        ImageFormat = GL_RGBA;
        if (nrChannels == 3) {
            InternalFormat = GL_RGB;
            ImageFormat = GL_RGB;
        }

        Width = width;
        Height = height;

        glTexImage2D(GL_TEXTURE_2D, 0, InternalFormat, width, height, 0, ImageFormat, GL_UNSIGNED_BYTE, data);
        glGenerateMipmap(GL_TEXTURE_2D);

        // 設定重複模式 (讓地板紋理可以 tiling)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

        // 設定過濾模式 (Mipmap 讓遠處不閃爍)
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        stbi_image_free(data);
    }
    else {
        std::cout << "Texture failed to load at path: " << path << std::endl;
        stbi_image_free(data);
    }
}

// 支援建立空貼圖 (給 SplatMap 用)
void Texture::Generate(int width, int height, unsigned int internalFormat, unsigned int format, void* data) {
    Width = width;
    Height = height;
    InternalFormat = internalFormat;
    ImageFormat = format;

    glBindTexture(GL_TEXTURE_2D, ID);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, GL_UNSIGNED_BYTE, data);

    // 預設參數
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindTexture(GL_TEXTURE_2D, 0);
}

void Texture::Bind(unsigned int slot) const {
    glActiveTexture(GL_TEXTURE0 + slot);
    glBindTexture(GL_TEXTURE_2D, ID);
}

void Texture::Unbind() const {
    glBindTexture(GL_TEXTURE_2D, 0);
}