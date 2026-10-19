# Native restoration architecture

`main.cpp` forwards every launch to `restoration::RunStageViewer`. With no explicit diagnostic selector, the application opens the native offline Turf stage-select menu. `--native` remains an alias; `--reference-stage`, `--reference-game`, and `--reference-match` select the existing diagnostic or direct-match paths. Unknown options fail during parsing before GLFW initialization.

## Build ownership

- `restoration_core` is the C++17 static library for reference data, stage geometry, CPU paint, player/controller, weapons, combat world, specials, navigation, bots, match lifecycle, and live character poses. It depends on GLM and has no GLFW or network lifetime.
- `Tiny-Splatoon` adds `StageViewer`, `CharacterArt`, `CharacterShader`, `PerformanceCapture`, and `GpuPreference`. GLFW／GLAD／OpenGL／ImGui own presentation; stb writes diagnostic PNGs.
- Tests link the same core library. Source exporters operate on the locally supplied, hash-locked `splat-ref/`; native fixtures and application builds use committed data.

## Runtime ownership

`RunStageViewer` owns one GLFW window, OpenGL context and ImGui context. The native menu starts a `MatchLoop` in that same context. The match owns its stage geometry, `FacePaint`, navigation, `MatchSimulation`, `FollowCamera`, per-actor `CharacterPose`, and character GPU resources. Return/replay destroys these match objects before reopening the stage menu. All GL resources are destroyed before the context is released.

`MatchSimulation` owns eight actors and seven bots in offline Turf. `PlayerSimulation`, `WeaponRunner`, `CombatWorld` and `WorldSpecials` are the shared gameplay path. Pose trigger/frame callbacks consume those states; renderer and UI do not run a second combat implementation. Simulation and the match follow-camera advance at a fixed 60 Hz; rendering displays the latest pose at the window frame rate. Focus loss and pause stop match advancement, and resume resets mouse/fire input edges.

## Paint and presentation

`StageGeometry` supplies collision and paintable/scoring faces. `FacePaint` stores logical ownership and versions changed atlas regions; the stage renderer uploads those changes. `CharacterArt` draws committed source meshes/shaders with live pose matrices and material state. Renderer diagnostics can also draw fixed source snapshots, but gameplay does not use them as animation.

The current body renderer uses the game tier. Screen-size tier selection/dither transitions, final environment/shadows/postprocessing, complete projectile/special world effects, audio, saves, online, Zones and Boss remain spec work; removing the legacy implementation does not complete these requirements.

## Legacy retirement — 2026-10-20

The user authorized keeping only restoration. The old `engine`, `components`, `gameplay`, `splat`, `scene`, `gui`, and `network` implementations and their unreferenced audio/model/shader/texture assets were removed. The NVIDIA preference export moved into restoration. `fmt` is removed. `GameNetworkingSockets` stays declared in vcpkg for future native multiplayer integration, but current targets neither link nor initialize it; the legacy protocol/manager is removed. Historical code remains recoverable from Git before this retirement commit.

Shared CMake presets are portable; machine-specific user presets stay local. The executable deploys the current reference assets. Release packaging continues to use the same executable name and asset layout.
