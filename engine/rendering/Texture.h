#pragma once
#include <glad/glad.h>
#include <string>

class Texture {
public:
    unsigned int ID;
    int Width, Height;
    unsigned int InternalFormat;
    unsigned int ImageFormat;
    unsigned int WrapS, WrapT;
    unsigned int FilterMin, FilterMag;

    Texture();
    ~Texture();

    void Load(const std::string& path);
    void Generate(int width, int height, unsigned int internalFormat = GL_RGBA, unsigned int format = GL_RGBA, void* data = nullptr);

    void Bind(unsigned int slot = 0) const;
    void Unbind() const;
};