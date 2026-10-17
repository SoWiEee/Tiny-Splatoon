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

file(GLOB_RECURSE MY_SOURCE_FILES CONFIGURE_DEPENDS
    "components/*"
    "engine/*"
    "gameplay/*"
    "splat/*"
    "scene/*"
    "network/*"
    "gui/*"
)


add_executable(Tiny-Splatoon main.cpp ${MY_SOURCE_FILES})

add_library(reference_geometry STATIC
    restoration/ReferenceData.cpp
    restoration/StageGeometry.cpp
    restoration/FacePaint.cpp
)
target_link_libraries(reference_geometry PUBLIC glm::glm)
target_sources(Tiny-Splatoon PRIVATE restoration/StageViewer.cpp)
target_link_libraries(Tiny-Splatoon PRIVATE reference_geometry)

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

include(CTest)
if(BUILD_TESTING)
    add_executable(stage_geometry_test tests/stage_geometry_test.cpp)
    target_link_libraries(stage_geometry_test PRIVATE reference_geometry)
    add_test(NAME reference_stage_geometry COMMAND stage_geometry_test)

    add_executable(face_paint_test tests/face_paint_test.cpp)
    target_link_libraries(face_paint_test PRIVATE reference_geometry)
    add_test(NAME reference_face_paint COMMAND face_paint_test)

    find_program(REFERENCE_NODE_EXECUTABLE node)
    if(REFERENCE_NODE_EXECUTABLE AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/splat-ref/src/config.js")
        add_test(NAME reference_export
            COMMAND "${REFERENCE_NODE_EXECUTABLE}" --test tests/reference-export.test.mjs)
        set_tests_properties(reference_export PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    endif()
endif()
