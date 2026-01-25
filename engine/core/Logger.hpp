#pragma once
#include <string>
#include <iostream>

class Logger {
public:
    static void Log(const std::string& message) {
        std::cout << "[INFO] " << message << std::endl;
    }

    static void Warn(const std::string& message) {
        std::cout << "\033[33m[WARN] " << message << "\033[0m" << std::endl; // ¶À¦â
    }

    static void Error(const std::string& message) {
        std::cerr << "\033[31m[ERROR] " << message << "\033[0m" << std::endl; // ¬õ¦â
    }
};