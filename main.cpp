#include "src/app/StageViewer.hpp"
#include <filesystem>
#include <string>
#include <system_error>

int main(int argc, char** argv) {
#if defined(__linux__)
    std::error_code error;
    const auto executable = std::filesystem::read_symlink("/proc/self/exe", error);
    auto executablePath = error ? std::string{} : executable.string();
    if (!executablePath.empty()) argv[0] = executablePath.data();
#endif
    return restoration::RunStageViewer(argc, argv);
}
