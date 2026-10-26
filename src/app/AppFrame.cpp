#include "AppFrame.hpp"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>
#include <chrono>
#include <thread>
#include <filesystem>
#include <vector>
#include <stdexcept>
#include <iostream>
namespace restoration {
void LimitFrame(double started,int cap){
    if(cap<=0)return;
    const double remaining=1.0/cap-(glfwGetTime()-started);
    if(remaining>0)std::this_thread::sleep_for(std::chrono::duration<double>(remaining));
}
void Capture(const std::string& path, int width, int height) {
    std::vector<unsigned char> pixels(static_cast<std::size_t>(width) * height * 3);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadBuffer(GL_BACK);
    glReadPixels(0, 0, width, height, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    const std::filesystem::path target(path);
    if (target.has_parent_path()) std::filesystem::create_directories(target.parent_path());
    stbi_flip_vertically_on_write(1);
    if (!stbi_write_png(path.c_str(), width, height, 3, pixels.data(), width * 3))
        throw std::runtime_error("Cannot write capture: " + path);
    std::cout << "CAPTURE " << path << " " << width << "x" << height << '\n';
}

}
