cmake_minimum_required(VERSION 3.10)

project(Tiny-Splatoon VERSION 1.0 LANGUAGES CXX C)

if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja")
    # MSVC emits UTF-8 here; a system-codepage probe can corrupt Ninja's include prefix.
    file(WRITE "${CMAKE_BINARY_DIR}/tiny-showincludes-probe.h" "")
    file(WRITE "${CMAKE_BINARY_DIR}/tiny-showincludes-probe.cpp" "#include \"tiny-showincludes-probe.h\"\n")
    execute_process(COMMAND "${CMAKE_CXX_COMPILER}" /nologo /showIncludes /Zs "${CMAKE_BINARY_DIR}/tiny-showincludes-probe.cpp"
        OUTPUT_VARIABLE TINY_INCLUDE_PROBE ENCODING UTF-8 RESULT_VARIABLE TINY_INCLUDE_PROBE_RESULT)
    string(REGEX MATCH "([^\r\n]*): [A-Za-z]:[^\r\n]*tiny-showincludes-probe.h" TINY_INCLUDE_LINE "${TINY_INCLUDE_PROBE}")
    if(NOT TINY_INCLUDE_PROBE_RESULT EQUAL 0 OR NOT TINY_INCLUDE_LINE)
        message(FATAL_ERROR "Could not detect MSVC's header dependency prefix")
    endif()
    set(CMAKE_CL_SHOWINCLUDES_PREFIX "${CMAKE_MATCH_1}: ")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(glfw3 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)

add_executable(Tiny-Splatoon main.cpp)

add_library(restoration_core STATIC
    restoration/ReferenceData.cpp
    restoration/StageGeometry.cpp
    restoration/FacePaint.cpp
    restoration/PlayerSimulation.cpp
    restoration/FollowCamera.cpp
    restoration/ArsenalData.cpp
    restoration/CombatWorld.cpp
    restoration/WeaponRunner.cpp
    restoration/NavGraph.cpp
    restoration/CharacterData.cpp
    restoration/CharacterPose.cpp
    restoration/BotSimulation.cpp
    restoration/MatchSimulation.cpp
    restoration/WorldSpecials.cpp
)
target_link_libraries(restoration_core PUBLIC glm::glm)
if(MSVC)
    target_compile_options(restoration_core PRIVATE /utf-8)
endif()
target_sources(Tiny-Splatoon PRIVATE restoration/StageViewer.cpp
    restoration/CharacterArt.cpp restoration/CharacterShader.cpp restoration/PerformanceCapture.cpp
    restoration/GpuPreference.cpp)
target_link_libraries(Tiny-Splatoon PRIVATE restoration_core)

target_link_libraries(Tiny-Splatoon PRIVATE 
    glad::glad
    glfw 
    OpenGL::GL
    imgui::imgui
    glm::glm
)

add_custom_command(TARGET Tiny-Splatoon POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/assets"
    "$<TARGET_FILE_DIR:Tiny-Splatoon>/assets"
)

include(CTest)
if(BUILD_TESTING)
    add_test(NAME reference_native_entry_default COMMAND Tiny-Splatoon --frames 3)
    set_tests_properties(reference_native_entry_default PROPERTIES
        PASS_REGULAR_EXPRESSION "NATIVE_MENU state=stage-select" TIMEOUT 30)

    add_executable(stage_geometry_test tests/stage_geometry_test.cpp)
    target_link_libraries(stage_geometry_test PRIVATE restoration_core)
    add_test(NAME reference_stage_geometry COMMAND stage_geometry_test)

    add_executable(face_paint_test tests/face_paint_test.cpp)
    target_link_libraries(face_paint_test PRIVATE restoration_core)
    add_test(NAME reference_face_paint COMMAND face_paint_test)

    add_executable(controller_physics_test tests/controller_physics_test.cpp)
    target_link_libraries(controller_physics_test PRIVATE restoration_core)
    add_test(NAME reference_controller_physics COMMAND controller_physics_test)

    add_executable(player_simulation_test tests/player_simulation_test.cpp)
    target_link_libraries(player_simulation_test PRIVATE restoration_core)
    add_test(NAME reference_player_simulation COMMAND player_simulation_test)

    add_executable(player_pose_bridge_test tests/player_pose_bridge_test.cpp)
    target_link_libraries(player_pose_bridge_test PRIVATE restoration_core)
    add_test(NAME reference_player_pose_bridge COMMAND player_pose_bridge_test)

    add_executable(follow_camera_test tests/follow_camera_test.cpp)
    target_link_libraries(follow_camera_test PRIVATE restoration_core)
    add_test(NAME reference_follow_camera COMMAND follow_camera_test)

    add_executable(ai_nav_test tests/ai_nav_test.cpp)
    target_link_libraries(ai_nav_test PRIVATE restoration_core)
    add_test(NAME reference_ai_nav COMMAND ai_nav_test)

    add_executable(match_simulation_test tests/match_simulation_test.cpp)
    target_link_libraries(match_simulation_test PRIVATE restoration_core)
    add_test(NAME reference_turf_match COMMAND match_simulation_test)

    add_executable(match_setup_test tests/match_setup_test.cpp)
    target_link_libraries(match_setup_test PRIVATE restoration_core)
    add_test(NAME reference_match_setup COMMAND match_setup_test)

    add_executable(world_specials_test tests/world_specials_test.cpp)
    target_link_libraries(world_specials_test PRIVATE restoration_core)
    add_test(NAME reference_world_specials COMMAND world_specials_test)

    add_executable(world_sonar_wail_test tests/world_sonar_wail_test.cpp)
    target_link_libraries(world_sonar_wail_test PRIVATE restoration_core)
    add_test(NAME reference_world_sonar_wail COMMAND world_sonar_wail_test)

    add_executable(world_kraken_test tests/world_kraken_test.cpp)
    target_link_libraries(world_kraken_test PRIVATE restoration_core)
    add_test(NAME reference_world_kraken COMMAND world_kraken_test)

    add_executable(world_remaining_specials_test tests/world_remaining_specials_test.cpp)
    target_link_libraries(world_remaining_specials_test PRIVATE restoration_core)
    add_test(NAME reference_world_remaining_specials COMMAND world_remaining_specials_test)

    add_executable(ai_bot_oracle_test tests/ai_bot_oracle_test.cpp)
    target_link_libraries(ai_bot_oracle_test PRIVATE restoration_core)
    add_test(NAME reference_ai_bot_oracle COMMAND ai_bot_oracle_test)

    add_executable(bot_special_control_test tests/bot_special_control_test.cpp)
    target_link_libraries(bot_special_control_test PRIVATE restoration_core)
    add_test(NAME reference_bot_special_control COMMAND bot_special_control_test)

    add_executable(arsenal_data_test tests/arsenal_data_test.cpp)
    target_link_libraries(arsenal_data_test PRIVATE restoration_core)
    add_test(NAME reference_arsenal_data COMMAND arsenal_data_test)

    add_executable(character_data_test tests/character_data_test.cpp)
    target_link_libraries(character_data_test PRIVATE restoration_core)
    add_test(NAME reference_character_data COMMAND character_data_test "${CMAKE_CURRENT_SOURCE_DIR}/assets/reference/characters")

    add_executable(character_pose_eval tests/character_pose_eval.cpp)
    target_link_libraries(character_pose_eval PRIVATE restoration_core)

    add_executable(character_weapon_animation_test tests/character_weapon_animation_test.cpp)
    target_link_libraries(character_weapon_animation_test PRIVATE restoration_core)
    add_test(NAME reference_character_weapon_animation COMMAND character_weapon_animation_test)

    add_executable(combat_runner_test tests/combat_runner_test.cpp)
    target_link_libraries(combat_runner_test PRIVATE restoration_core)
    add_test(NAME reference_combat_runner COMMAND combat_runner_test)

    add_executable(combat_kit_test tests/combat_kit_test.cpp)
    target_link_libraries(combat_kit_test PRIVATE restoration_core)
    add_test(NAME reference_combat_kits COMMAND combat_kit_test)

    add_executable(combat_world_test tests/combat_world_test.cpp)
    target_link_libraries(combat_world_test PRIVATE restoration_core)
    add_test(NAME reference_combat_world COMMAND combat_world_test)

    add_executable(combat_device_test tests/combat_device_test.cpp)
    target_link_libraries(combat_device_test PRIVATE restoration_core)
    add_test(NAME reference_combat_devices COMMAND combat_device_test)

    add_executable(character_pose_shader_test tests/character_pose_shader_test.cpp restoration/CharacterShader.cpp restoration/GpuPreference.cpp)
    target_link_libraries(character_pose_shader_test PRIVATE glm::glm glad::glad glfw OpenGL::GL)
    add_test(NAME reference_character_shaders COMMAND character_pose_shader_test "${CMAKE_CURRENT_SOURCE_DIR}/assets/reference/characters/shaders")

    add_executable(character_art_test tests/character_art_test.cpp restoration/CharacterArt.cpp restoration/CharacterShader.cpp restoration/GpuPreference.cpp)
    target_link_libraries(character_art_test PRIVATE restoration_core glad::glad glfw OpenGL::GL)
    add_test(NAME reference_character_art COMMAND character_art_test "${CMAKE_CURRENT_SOURCE_DIR}/assets/reference/characters" "${CMAKE_BINARY_DIR}/captures/character")

    find_program(REFERENCE_NODE_EXECUTABLE node)
    if(REFERENCE_NODE_EXECUTABLE)
        add_test(NAME reference_character_pose_native
            COMMAND "${REFERENCE_NODE_EXECUTABLE}" tests/character_pose_native.test.mjs "$<TARGET_FILE:character_pose_eval>")
        set_tests_properties(reference_character_pose_native PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        add_test(NAME reference_character_pose_variants
            COMMAND "${REFERENCE_NODE_EXECUTABLE}" tests/character_pose_native.test.mjs "$<TARGET_FILE:character_pose_eval>" tests/character_variant_fixtures.json)
        set_tests_properties(reference_character_pose_variants PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        add_test(NAME reference_native_entry_cli
            COMMAND "${REFERENCE_NODE_EXECUTABLE}" tests/native_entry_cli.test.mjs "$<TARGET_FILE:Tiny-Splatoon>")
        set_tests_properties(reference_native_entry_cli PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/splat-ref/src/config.js")
            add_test(NAME reference_export
                COMMAND "${REFERENCE_NODE_EXECUTABLE}" --test tests/reference-export.test.mjs)
            set_tests_properties(reference_export PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
            add_test(NAME reference_character_pose_export
                COMMAND "${REFERENCE_NODE_EXECUTABLE}" --test tests/character_pose_export.test.mjs)
            set_tests_properties(reference_character_pose_export PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")


            add_test(NAME reference_match_setup_export
                COMMAND "${REFERENCE_NODE_EXECUTABLE}" tools/export-match-setup-fixtures.mjs --check)
            set_tests_properties(reference_match_setup_export PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
            add_test(NAME reference_restoration_exports
                COMMAND "${REFERENCE_NODE_EXECUTABLE}" --test tests/restoration_exports.test.mjs)
            set_tests_properties(reference_restoration_exports PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

        endif()
    endif()
endif()
