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

# The source layers share public domain types without a second forwarding-header tree.
include_directories(src/world src/game src/render src/gui src/app src/audio)
if(MSVC)
    add_compile_options(/utf-8)
endif()

find_package(glfw3 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(glad CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(RmlUi CONFIG REQUIRED)
find_path(MINIAUDIO_INCLUDE_DIR miniaudio.h)
if(NOT MINIAUDIO_INCLUDE_DIR)
    message(FATAL_ERROR "miniaudio headers were not found")
endif()

add_executable(Tiny-Splatoon main.cpp)

add_library(restoration_core STATIC
    src/world/ReferenceData.cpp
    src/world/StageGeometry.cpp
    src/world/FacePaint.cpp
    src/game/PlayerSimulation.cpp
    src/game/FollowCamera.cpp
    src/game/ArsenalData.cpp
    src/game/CombatWorld.cpp
    src/game/WeaponRunner.cpp
    src/game/NavGraph.cpp
    src/game/CharacterData.cpp
    src/game/CharacterPose.cpp
    src/game/BotSimulation.cpp
    src/game/BossNav.cpp
    src/game/BossModelData.cpp
    src/game/BossPose.cpp
    src/game/BossSimulation.cpp
    src/game/MatchSimulation.cpp
    src/game/WorldSpecials.cpp
)
target_link_libraries(restoration_core PUBLIC glm::glm)
if(MSVC)
    target_compile_options(restoration_core PRIVATE /utf-8)
endif()
add_library(player_ui STATIC src/gui/GameUi.cpp src/gui/RmlRenderer.cpp)
target_link_libraries(player_ui PUBLIC restoration_core RmlUi::RmlUi glad::glad glfw OpenGL::GL)
add_library(game_audio STATIC src/audio/AudioEngine.cpp)
target_include_directories(game_audio PRIVATE "${MINIAUDIO_INCLUDE_DIR}")
find_package(Threads REQUIRED)
target_link_libraries(game_audio PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
target_sources(Tiny-Splatoon PRIVATE src/app/StageViewer.cpp
    src/render/CharacterArt.cpp src/render/CharacterLod.cpp src/render/CharacterShader.cpp src/render/PerformanceCapture.cpp src/render/StageRenderer.cpp src/render/RenderPipeline.cpp src/render/DirectionalShadow.cpp
    src/render/WorldEffects.cpp src/render/ScreenEffectsController.cpp src/render/LensInk.cpp src/render/LensField.cpp src/render/BossRenderer.cpp
    src/render/WorldFxRecipes.cpp
    src/app/GpuPreference.cpp)
target_link_libraries(Tiny-Splatoon PRIVATE restoration_core player_ui game_audio)

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
# Asset edits must deploy even when only RML, RCSS, fonts or sound data changed.
file(GLOB_RECURSE TINY_RUNTIME_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/assets/*")
set_property(TARGET Tiny-Splatoon APPEND PROPERTY LINK_DEPENDS ${TINY_RUNTIME_ASSETS})

include(CTest)
if(BUILD_TESTING)
    add_executable(rml_renderer_test tests/rml_renderer_test.cpp src/app/GpuPreference.cpp)
    target_link_libraries(rml_renderer_test PRIVATE player_ui)
    add_test(NAME reference_player_ui_renderer COMMAND rml_renderer_test "${CMAKE_CURRENT_SOURCE_DIR}")

    add_executable(game_ui_test tests/game_ui_test.cpp src/app/GpuPreference.cpp)
    target_link_libraries(game_ui_test PRIVATE player_ui)
    add_test(NAME reference_player_ui_interactions COMMAND game_ui_test "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/captures/ui")

    add_executable(audio_engine_test tests/audio_engine_test.cpp)
    target_link_libraries(audio_engine_test PRIVATE game_audio)
    add_test(NAME reference_audio_engine COMMAND audio_engine_test "${CMAKE_CURRENT_SOURCE_DIR}/assets/audio")
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

    add_executable(boss_lifecycle_test tests/boss_lifecycle_test.cpp)
    target_link_libraries(boss_lifecycle_test PRIVATE restoration_core)
    add_test(NAME reference_boss_lifecycle COMMAND boss_lifecycle_test)

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

    add_executable(character_lod_eval tests/character_lod_eval.cpp src/render/CharacterLod.cpp)
    target_link_libraries(character_lod_eval PRIVATE glm::glm)

    add_executable(boss_nav_eval tests/boss_nav_eval.cpp)
    target_link_libraries(boss_nav_eval PRIVATE restoration_core)

    add_executable(boss_pose_eval tests/boss_pose_eval.cpp)
    target_link_libraries(boss_pose_eval PRIVATE restoration_core)

    add_executable(boss_model_asset_test tests/boss_model_asset_test.cpp)
    target_link_libraries(boss_model_asset_test PRIVATE restoration_core)
    add_test(NAME reference_boss_model_asset COMMAND boss_model_asset_test "${CMAKE_CURRENT_SOURCE_DIR}/assets/reference/boss/model.bin")

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

    add_executable(character_pose_shader_test tests/character_pose_shader_test.cpp src/render/CharacterShader.cpp src/app/GpuPreference.cpp)
    target_link_libraries(character_pose_shader_test PRIVATE glm::glm glad::glad glfw OpenGL::GL)
    add_test(NAME reference_character_shaders COMMAND character_pose_shader_test "${CMAKE_CURRENT_SOURCE_DIR}/assets/reference/characters/shaders")

    add_executable(character_art_test tests/character_art_test.cpp src/render/CharacterArt.cpp src/render/CharacterShader.cpp src/app/GpuPreference.cpp)
    target_link_libraries(character_art_test PRIVATE restoration_core glad::glad glfw OpenGL::GL)
    add_test(NAME reference_character_art COMMAND character_art_test "${CMAKE_CURRENT_SOURCE_DIR}/assets/reference/characters" "${CMAKE_BINARY_DIR}/captures/character")

    add_executable(render_pipeline_test tests/render_pipeline_test.cpp src/render/RenderPipeline.cpp src/render/DirectionalShadow.cpp src/render/LensInk.cpp src/render/LensField.cpp src/render/CharacterShader.cpp src/app/GpuPreference.cpp)
    target_link_libraries(render_pipeline_test PRIVATE glad::glad glfw OpenGL::GL glm::glm)
    add_test(NAME reference_render_pipeline COMMAND render_pipeline_test)
    set_tests_properties(reference_render_pipeline PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    add_executable(screen_effects_eval tests/screen_effects_eval.cpp src/render/ScreenEffectsController.cpp src/render/LensInk.cpp)
    target_link_libraries(screen_effects_eval PRIVATE glm::glm)

    add_executable(worldfx_eval tests/worldfx_eval.cpp src/render/WorldFxRecipes.cpp)
    target_link_libraries(worldfx_eval PRIVATE glm::glm)

    add_executable(worldfx_gpu_test tests/worldfx_gpu_test.cpp src/render/WorldEffects.cpp src/render/WorldFxRecipes.cpp src/app/GpuPreference.cpp)
    target_link_libraries(worldfx_gpu_test PRIVATE restoration_core glad::glad glfw OpenGL::GL)
    add_test(NAME reference_worldfx_gpu COMMAND worldfx_gpu_test)
    set_tests_properties(reference_worldfx_gpu PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    find_program(REFERENCE_NODE_EXECUTABLE node)
    if(REFERENCE_NODE_EXECUTABLE)
        add_test(NAME reference_character_lod
            COMMAND "${REFERENCE_NODE_EXECUTABLE}" tests/character_lod_oracle.test.mjs "$<TARGET_FILE:character_lod_eval>")
        set_tests_properties(reference_character_lod PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        add_test(NAME reference_boss_nav
            COMMAND "${REFERENCE_NODE_EXECUTABLE}" tests/boss_nav_oracle.test.mjs "$<TARGET_FILE:boss_nav_eval>")
        set_tests_properties(reference_boss_nav PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        add_test(NAME reference_screen_effects
            COMMAND "${REFERENCE_NODE_EXECUTABLE}" tests/screen_effects_oracle.test.mjs "$<TARGET_FILE:screen_effects_eval>")
        set_tests_properties(reference_screen_effects PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        add_test(NAME reference_worldfx_oracle
            COMMAND "${REFERENCE_NODE_EXECUTABLE}" tests/worldfx_oracle.test.mjs "$<TARGET_FILE:worldfx_eval>")
        set_tests_properties(reference_worldfx_oracle PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
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
            add_test(NAME reference_audio_export COMMAND "${REFERENCE_NODE_EXECUTABLE}" tests/audio_source_render.test.mjs)
            set_tests_properties(reference_audio_export PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" TIMEOUT 180)
            add_test(NAME reference_boss_nav_export COMMAND "${REFERENCE_NODE_EXECUTABLE}" tools/export-boss-nav-fixtures.mjs --check)
            set_tests_properties(reference_boss_nav_export PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
            add_test(NAME reference_boss_pose_export COMMAND "${REFERENCE_NODE_EXECUTABLE}" tools/export-boss-pose-fixtures.mjs --check)
            set_tests_properties(reference_boss_pose_export PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
            add_test(NAME reference_boss_model_export_data COMMAND "${REFERENCE_NODE_EXECUTABLE}" tools/export-boss-model.mjs --check)
            set_tests_properties(reference_boss_model_export_data PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
            add_test(NAME reference_screen_effects_export COMMAND "${REFERENCE_NODE_EXECUTABLE}" tools/export-screen-effects-fixtures.mjs --check)
            set_tests_properties(reference_screen_effects_export PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
            add_test(NAME reference_worldfx_export COMMAND "${REFERENCE_NODE_EXECUTABLE}" tools/export-worldfx-source-fixtures.mjs --check)
            set_tests_properties(reference_worldfx_export PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
            add_test(NAME reference_worldfx_assets COMMAND "${REFERENCE_NODE_EXECUTABLE}" tools/export-worldfx-assets.mjs --check)
            set_tests_properties(reference_worldfx_assets PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
            add_test(NAME reference_post_export COMMAND "${REFERENCE_NODE_EXECUTABLE}" --test tests/post_export.test.mjs)
            set_tests_properties(reference_post_export PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
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
