cmake_minimum_required(VERSION 3.10)

project(Tiny-Splatoon VERSION 1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(glfw3 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED) 
find_package(imgui CONFIG REQUIRED)
find_package(GameNetworkingSockets CONFIG REQUIRED)

file(GLOB_RECURSE MY_SOURCE_FILES "components/*" "engine/*" "gameplay/*" "splat/*" "scene/*" "network/*" "gui/*")


add_executable(Tiny-Splatoon main.cpp ${MY_SOURCE_FILES})

target_link_libraries(Tiny-Splatoon PRIVATE 
    glad::glad
    glfw 
    OpenGL::GL
    fmt::fmt
    imgui::imgui
    glm::glm
    GameNetworkingSockets::shared
)

add_custom_command(TARGET Tiny-Splatoon POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    "$<TARGET_FILE_DIR:Tiny-Splatoon>/assets"
)