cmake_minimum_required(VERSION 3.10)

project(Tiny-Splatoon VERSION 0.8.0 LANGUAGES CXX C)

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
find_package(RmlUi CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(GameNetworkingSockets CONFIG REQUIRED)
find_path(MINIAUDIO_INCLUDE_DIR miniaudio.h)
if(NOT MINIAUDIO_INCLUDE_DIR)
    message(FATAL_ERROR "miniaudio headers were not found")
endif()

add_executable(Tiny-Splatoon main.cpp)

add_library(game_network STATIC src/network/GnsTransport.cpp src/network/RoomServer.cpp src/network/RoomSession.cpp src/network/MatchSync.cpp)
target_link_libraries(game_network PUBLIC GameNetworkingSockets::GameNetworkingSockets)
target_include_directories(game_network PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src/network")

add_library(restoration_core STATIC
    src/world/ReferenceData.cpp
    src/world/StageGeometry.cpp
    src/world/FacePaint.cpp
    src/game/PlayerSimulation.cpp
    src/game/PlayerProfile.cpp
    src/game/PlayerController.cpp
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
    src/game/ZoneControl.cpp
    src/game/ZoneData.cpp
    src/game/ZonePlan.cpp
    src/game/WorldSpecials.cpp
    src/network/AuthorityState.cpp
)
target_link_libraries(restoration_core PUBLIC glm::glm)
target_link_libraries(game_network PUBLIC restoration_core)
add_executable(Tiny-Splatoon-Server src/network/RoomServerMain.cpp)
target_link_libraries(Tiny-Splatoon-Server PRIVATE game_network)
add_dependencies(Tiny-Splatoon Tiny-Splatoon-Server)
if(MSVC)
    target_compile_options(restoration_core PRIVATE /utf-8)
endif()
add_library(player_ui STATIC src/gui/GameUi.cpp src/gui/InkTankElement.cpp src/gui/WeaponReticleElement.cpp src/gui/SpecialGaugeElement.cpp src/gui/BattleFeedbackElement.cpp src/gui/RmlRenderer.cpp src/gui/UiText.cpp)
target_link_libraries(player_ui PUBLIC restoration_core RmlUi::RmlUi glad::glad glfw OpenGL::GL)
add_library(game_audio STATIC src/audio/AudioEngine.cpp src/audio/GameAudio.cpp src/audio/ZoneHum.cpp src/audio/NativeReverb.cpp)
target_include_directories(game_audio PRIVATE "${MINIAUDIO_INCLUDE_DIR}")
find_package(Threads REQUIRED)
target_link_libraries(game_audio PUBLIC restoration_core Threads::Threads ${CMAKE_DL_LIBS})
add_library(projectile_visuals STATIC src/render/ProjectileVisuals.cpp)
target_link_libraries(projectile_visuals PUBLIC restoration_core)
target_sources(Tiny-Splatoon PRIVATE src/app/StageViewer.cpp src/app/OnlinePlay.cpp
    src/render/CharacterArt.cpp src/render/CharacterLod.cpp src/render/CharacterShader.cpp src/render/PerformanceCapture.cpp src/render/StageRenderer.cpp src/render/RenderPipeline.cpp src/render/DirectionalShadow.cpp
    src/render/WorldEffects.cpp src/render/WorldFxParticleRenderer.cpp src/render/ScreenEffectsController.cpp src/render/LensInk.cpp src/render/LensField.cpp src/render/BossRenderer.cpp
    src/render/WorldFxRecipes.cpp src/render/StageMapView.cpp src/render/StageMapCamera.cpp
    src/app/GpuPreference.cpp src/render/ProjectileRenderer.cpp src/render/EnvironmentRenderer.cpp src/render/EnvironmentScene.cpp src/render/DeviceRenderer.cpp src/render/PlayerShowcase.cpp src/render/ZoneRenderer.cpp)
target_link_libraries(Tiny-Splatoon PRIVATE restoration_core player_ui game_audio projectile_visuals game_network)

target_link_libraries(Tiny-Splatoon PRIVATE
    glad::glad
    glfw
    OpenGL::GL
    glm::glm
    ZLIB::ZLIB
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
    add_executable(network_transport_test tests/network_transport_test.cpp)
    target_link_libraries(network_transport_test PRIVATE game_network)
    foreach(NETWORK_CLIENTS 2 3 8)
        add_test(NAME native_network_transport_${NETWORK_CLIENTS} COMMAND network_transport_test ${NETWORK_CLIENTS})
        set_tests_properties(native_network_transport_${NETWORK_CLIENTS} PROPERTIES TIMEOUT 60)
    endforeach()
    add_executable(network_room_test tests/network_room_test.cpp)
    target_link_libraries(network_room_test PRIVATE game_network)
    foreach(NETWORK_CLIENTS 2 3 8)
        add_test(NAME native_network_room_${NETWORK_CLIENTS} COMMAND network_room_test ${NETWORK_CLIENTS})
        set_tests_properties(native_network_room_${NETWORK_CLIENTS} PROPERTIES TIMEOUT 90)
    endforeach()
    add_executable(network_match_test tests/network_match_test.cpp)
    target_link_libraries(network_match_test PRIVATE game_network)
    add_test(NAME native_network_match COMMAND network_match_test)
    set_tests_properties(native_network_match PROPERTIES TIMEOUT 180)
    add_executable(network_sync_test tests/network_sync_test.cpp)
    target_link_libraries(network_sync_test PRIVATE game_network)
    add_test(NAME native_network_sync COMMAND network_sync_test)
    set_tests_properties(native_network_sync PROPERTIES TIMEOUT 90)
    add_executable(network_authority_test tests/network_authority_test.cpp)
    target_link_libraries(network_authority_test PRIVATE restoration_core)
    add_test(NAME native_network_authority COMMAND network_authority_test)
    add_executable(online_play_test tests/online_play_test.cpp src/app/OnlinePlay.cpp)
    target_link_libraries(online_play_test PRIVATE game_network)
    add_executable(zone_match_test tests/zone_match_test.cpp)
    target_link_libraries(zone_match_test PRIVATE restoration_core)
    add_test(NAME native_zone_matches COMMAND zone_match_test)
    set_tests_properties(native_zone_matches PROPERTIES TIMEOUT 180)
    add_executable(zone_control_test tests/zone_control_test.cpp)
    target_link_libraries(zone_control_test PRIVATE restoration_core)
    add_test(NAME native_zone_control COMMAND zone_control_test)
    add_executable(zone_ai_test tests/zone_ai_test.cpp)
    target_link_libraries(zone_ai_test PRIVATE restoration_core)
    add_test(NAME native_zone_ai COMMAND zone_ai_test)
    add_executable(zone_hum_test tests/zone_hum_test.cpp src/audio/ZoneHum.cpp)
    target_link_libraries(zone_hum_test PRIVATE Threads::Threads)
    add_test(NAME native_zone_hum COMMAND zone_hum_test)
    add_executable(zone_visual_gpu_test tests/zone_visual_gpu_test.cpp src/render/ZoneRenderer.cpp src/render/StageRenderer.cpp src/render/CharacterShader.cpp src/render/DirectionalShadow.cpp src/app/GpuPreference.cpp)
    target_link_libraries(zone_visual_gpu_test PRIVATE restoration_core glad::glad glfw OpenGL::GL ZLIB::ZLIB)
    add_test(NAME native_zone_visual_gpu COMMAND zone_visual_gpu_test "${CMAKE_CURRENT_SOURCE_DIR}/assets/reference" "${CMAKE_BINARY_DIR}/captures/zones")
    add_executable(player_profile_test tests/player_profile_test.cpp)
    target_link_libraries(player_profile_test PRIVATE restoration_core)
    add_test(NAME native_player_profile COMMAND player_profile_test)
    add_executable(player_showcase_test tests/player_showcase_test.cpp src/render/PlayerShowcase.cpp src/render/CharacterArt.cpp src/render/CharacterShader.cpp src/app/GpuPreference.cpp)
    target_link_libraries(player_showcase_test PRIVATE restoration_core glad::glad glfw OpenGL::GL)
    add_test(NAME native_player_showcase COMMAND player_showcase_test "${CMAKE_CURRENT_SOURCE_DIR}/assets/reference/characters")
    add_executable(team_palette_test tests/team_palette_test.cpp)
    target_link_libraries(team_palette_test PRIVATE glm::glm)
    add_test(NAME native_team_palette COMMAND team_palette_test)
    add_executable(player_controller_test tests/player_controller_test.cpp)
    target_link_libraries(player_controller_test PRIVATE restoration_core)
    add_test(NAME native_player_controller COMMAND player_controller_test)
    add_executable(ui_text_test tests/ui_text_test.cpp src/gui/UiText.cpp)
    target_link_libraries(ui_text_test PRIVATE restoration_core)
    add_test(NAME native_ui_translations COMMAND ui_text_test)
    add_executable(stage_prop_animation_test tests/stage_prop_animation_test.cpp)
    target_link_libraries(stage_prop_animation_test PRIVATE glm::glm)
    add_test(NAME reference_stage_prop_animation COMMAND stage_prop_animation_test)
    add_executable(swim_wake_test tests/swim_wake_test.cpp)
    target_link_libraries(swim_wake_test PRIVATE restoration_core)
    add_test(NAME reference_swim_wake_state COMMAND swim_wake_test)
    add_executable(worldfx_transition_test tests/worldfx_transition_test.cpp)
    target_link_libraries(worldfx_transition_test PRIVATE glm::glm)
    add_test(NAME reference_worldfx_transitions COMMAND worldfx_transition_test)
    add_executable(environment_gpu_test tests/environment_gpu_test.cpp src/render/EnvironmentRenderer.cpp src/render/EnvironmentScene.cpp src/render/CharacterShader.cpp src/app/GpuPreference.cpp)
    target_link_libraries(environment_gpu_test PRIVATE restoration_core glad::glad glfw OpenGL::GL ZLIB::ZLIB)
    add_test(NAME reference_environment_gpu COMMAND environment_gpu_test "${CMAKE_CURRENT_SOURCE_DIR}/assets/reference/environment" "${CMAKE_BINARY_DIR}/captures/environment")
    add_executable(device_visual_gpu_test tests/device_visual_gpu_test.cpp src/render/DeviceRenderer.cpp src/render/CharacterShader.cpp src/app/GpuPreference.cpp)
    target_link_libraries(device_visual_gpu_test PRIVATE restoration_core glad::glad glfw OpenGL::GL)
    add_test(NAME reference_device_visual_gpu COMMAND device_visual_gpu_test "${CMAKE_CURRENT_SOURCE_DIR}/assets/reference/devices" "${CMAKE_BINARY_DIR}/captures/devices")
    add_executable(character_footstep_test tests/character_footstep_test.cpp)
    target_link_libraries(character_footstep_test PRIVATE restoration_core)
    add_test(NAME reference_character_footstep COMMAND character_footstep_test)
    add_executable(boss_material_gpu_test tests/boss_material_gpu_test.cpp src/render/BossRenderer.cpp src/render/CharacterShader.cpp src/app/GpuPreference.cpp)
    target_link_libraries(boss_material_gpu_test PRIVATE restoration_core glad::glad glfw OpenGL::GL ZLIB::ZLIB)
    add_test(NAME reference_boss_material_gpu COMMAND boss_material_gpu_test "${CMAKE_CURRENT_SOURCE_DIR}/assets/reference/boss")
    add_executable(stage_visual_gpu_test tests/stage_visual_gpu_test.cpp src/render/StageRenderer.cpp src/render/CharacterShader.cpp src/render/DirectionalShadow.cpp src/app/GpuPreference.cpp)
    target_link_libraries(stage_visual_gpu_test PRIVATE restoration_core glad::glad glfw OpenGL::GL ZLIB::ZLIB)
    add_test(NAME reference_stage_visual_gpu COMMAND stage_visual_gpu_test "${CMAKE_CURRENT_SOURCE_DIR}/assets/reference/stages" "${CMAKE_BINARY_DIR}/captures/stage-visual")
    add_executable(projectile_visual_test tests/projectile_visual_test.cpp)
    target_link_libraries(projectile_visual_test PRIVATE projectile_visuals)
    add_test(NAME reference_projectile_visuals COMMAND projectile_visual_test "${CMAKE_CURRENT_SOURCE_DIR}/assets/reference/projectiles/blob-fixtures.txt")
    add_executable(projectile_gpu_test tests/projectile_gpu_test.cpp src/render/ProjectileRenderer.cpp src/render/CharacterShader.cpp src/app/GpuPreference.cpp)
    target_link_libraries(projectile_gpu_test PRIVATE projectile_visuals glad::glad glfw OpenGL::GL)
    add_test(NAME reference_projectile_gpu COMMAND projectile_gpu_test "${CMAKE_CURRENT_SOURCE_DIR}/assets/reference/projectiles" "${CMAKE_BINARY_DIR}/captures/projectile-teardrops.png")
    add_executable(rml_renderer_test tests/rml_renderer_test.cpp src/app/GpuPreference.cpp)
    target_link_libraries(rml_renderer_test PRIVATE player_ui)
    add_test(NAME reference_player_ui_renderer COMMAND rml_renderer_test "${CMAKE_CURRENT_SOURCE_DIR}")

    add_executable(stage_map_camera_test tests/stage_map_camera_test.cpp src/render/StageMapCamera.cpp)
    target_link_libraries(stage_map_camera_test PRIVATE glm::glm)
    add_test(NAME reference_stage_map_camera COMMAND stage_map_camera_test)

    add_executable(game_ui_test tests/game_ui_test.cpp src/app/GpuPreference.cpp)
    target_sources(game_ui_test PRIVATE src/render/StageMapView.cpp src/render/StageMapCamera.cpp
        src/render/StageRenderer.cpp src/render/CharacterShader.cpp src/render/DirectionalShadow.cpp
        src/render/EnvironmentRenderer.cpp src/render/EnvironmentScene.cpp src/render/RenderPipeline.cpp src/render/PlayerShowcase.cpp src/render/CharacterArt.cpp)
    target_link_libraries(game_ui_test PRIVATE player_ui ZLIB::ZLIB)
    add_test(NAME reference_player_ui_interactions COMMAND game_ui_test "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/captures/ui")

    add_executable(audio_engine_test tests/audio_engine_test.cpp)
    target_link_libraries(audio_engine_test PRIVATE game_audio)
    add_test(NAME reference_audio_engine COMMAND audio_engine_test "${CMAKE_CURRENT_SOURCE_DIR}/assets/audio")
    add_executable(audio_reverb_test tests/audio_reverb_test.cpp)
    target_include_directories(audio_reverb_test PRIVATE "${MINIAUDIO_INCLUDE_DIR}")
    target_link_libraries(audio_reverb_test PRIVATE game_audio)
    add_test(NAME reference_native_reverb COMMAND audio_reverb_test "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures")
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

    add_executable(worldfx_gpu_test tests/worldfx_gpu_test.cpp src/render/WorldEffects.cpp src/render/WorldFxParticleRenderer.cpp src/render/WorldFxRecipes.cpp src/render/DeviceRenderer.cpp src/render/CharacterShader.cpp src/app/GpuPreference.cpp)
    target_link_libraries(worldfx_gpu_test PRIVATE restoration_core glad::glad glfw OpenGL::GL)
    add_test(NAME reference_worldfx_gpu COMMAND worldfx_gpu_test)
    set_tests_properties(reference_worldfx_gpu PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

    find_program(REFERENCE_NODE_EXECUTABLE node)
    if(REFERENCE_NODE_EXECUTABLE)
        add_test(NAME native_network_service_process
            COMMAND "${REFERENCE_NODE_EXECUTABLE}" tests/network_service_process.test.mjs "$<TARGET_FILE:Tiny-Splatoon-Server>" "$<TARGET_FILE:online_play_test>" "$<TARGET_FILE:network_room_test>")
        set_tests_properties(native_network_service_process PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" TIMEOUT 100)
        add_test(NAME reference_custom_music_import COMMAND "${REFERENCE_NODE_EXECUTABLE}" --test tests/import_music.test.mjs)
        set_tests_properties(reference_custom_music_import PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        add_test(NAME reference_boss_pose
            COMMAND "${REFERENCE_NODE_EXECUTABLE}" tests/boss_pose_oracle.test.mjs "$<TARGET_FILE:boss_pose_eval>")
        set_tests_properties(reference_boss_pose PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
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
            COMMAND "${REFERENCE_NODE_EXECUTABLE}" tests/character_pose_native.test.mjs "$<TARGET_FILE:character_pose_eval>" tests/fixtures/character_variant_fixtures.json)
        set_tests_properties(reference_character_pose_variants PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        add_test(NAME reference_native_entry_cli
            COMMAND "${REFERENCE_NODE_EXECUTABLE}" tests/native_entry_cli.test.mjs "$<TARGET_FILE:Tiny-Splatoon>")
        set_tests_properties(reference_native_entry_cli PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/splat-ref/src/config.js")
            add_test(NAME reference_native_reverb_export COMMAND "${REFERENCE_NODE_EXECUTABLE}" tools/export-native-reverb-fixture.mjs --check)
            set_tests_properties(reference_native_reverb_export PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" TIMEOUT 60)
            add_test(NAME reference_zone_data COMMAND "${REFERENCE_NODE_EXECUTABLE}" tools/export-zone-data.mjs --check)
            set_tests_properties(reference_zone_data PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
            add_test(NAME reference_zone_fixtures COMMAND "${REFERENCE_NODE_EXECUTABLE}" tools/export-zone-fixtures.mjs --check)
            set_tests_properties(reference_zone_fixtures PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
            add_test(NAME reference_zone_visual_export COMMAND "${REFERENCE_NODE_EXECUTABLE}" tools/export-zone-visuals.mjs --check)
            set_tests_properties(reference_zone_visual_export PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
            add_test(NAME reference_environment_source_export COMMAND "${REFERENCE_NODE_EXECUTABLE}" tools/export-environment-source.mjs --check)
            add_test(NAME reference_environment_scene_export COMMAND "${REFERENCE_NODE_EXECUTABLE}" tools/export-environment-scene.mjs --check)
            add_test(NAME reference_device_visual_export COMMAND "${REFERENCE_NODE_EXECUTABLE}" tools/export-device-visuals.mjs --check)
            add_test(NAME reference_showcase_drums_export COMMAND "${REFERENCE_NODE_EXECUTABLE}" tools/export-showcase-drums.mjs --check)
            set_tests_properties(reference_environment_source_export reference_environment_scene_export reference_device_visual_export
                PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
            set_tests_properties(reference_showcase_drums_export PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
            add_test(NAME reference_boss_material_export COMMAND "${REFERENCE_NODE_EXECUTABLE}" tools/export-boss-materials.mjs --check)
            set_tests_properties(reference_boss_material_export PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
            add_test(NAME reference_stage_visual_export COMMAND "${REFERENCE_NODE_EXECUTABLE}" tests/stage_visual_export.test.mjs)
            set_tests_properties(reference_stage_visual_export PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
            add_test(NAME reference_stage_texture_data COMMAND "${REFERENCE_NODE_EXECUTABLE}" tests/stage_texture_data.test.mjs)
            set_tests_properties(reference_stage_texture_data PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
            add_test(NAME reference_projectile_export COMMAND "${REFERENCE_NODE_EXECUTABLE}" tools/export-projectile-visuals.mjs --check)
            set_tests_properties(reference_projectile_export PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
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
            add_test(NAME reference_hud_equipment_assets COMMAND "${REFERENCE_NODE_EXECUTABLE}" tools/export-hud-equipment-icons.mjs --check)
            set_tests_properties(reference_hud_equipment_assets PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
            add_test(NAME reference_worldfx_particle_assets COMMAND "${REFERENCE_NODE_EXECUTABLE}" tools/export-worldfx-particle-shaders.mjs --check)
            set_tests_properties(reference_worldfx_particle_assets PROPERTIES WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
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
