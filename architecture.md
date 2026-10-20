# Native architecture

`main.cpp` forwards every launch to `restoration::RunStageViewer`. With no explicit diagnostic selector, the application opens the native offline Turf stage-select menu. `--native` remains an alias; `--reference-stage`, `--reference-game`, and `--reference-match` select the existing diagnostic or direct-match paths. Unknown options fail during parsing before GLFW initialization.

## Build ownership

- `restoration_core` is the C++17 static library for reference data, stage geometry, CPU paint, player/controller, weapons, combat world, specials, navigation, bots, match lifecycle, and live character poses. It depends on GLM and has no GLFW or network lifetime.
- `Tiny-Splatoon` adds `StageViewer`, `CharacterArt`, `CharacterShader`, `PerformanceCapture`, and `GpuPreference`. GLFW／GLAD／OpenGL／ImGui own presentation; stb writes diagnostic PNGs.
- Tests link the same core library. Source exporters operate on the locally supplied, hash-locked `splat-ref/`; native fixtures and application builds use committed data.

## Source layers

| Directory | Responsibility | Main owners |
| --- | --- | --- |
| `src/app` | Launch options, GLFW lifetime, input and match/menu orchestration | `StageViewer`, `GpuPreference` |
| `src/game` | Fixed-step actor, weapons, combat, AI, modes and procedural character poses | `MatchSimulation`, `PlayerSimulation`, `CombatWorld`, `WorldSpecials`, `BossSimulation` |
| `src/world` | Committed map data, geometry queries, logical ink and scoring | `ReferenceData`, `StageGeometry`, `FacePaint` |
| `src/render` | Context-local drawing, LOD, shadows, HDR postprocessing and visual state | `CharacterArt`, `CharacterLod`, `StageRenderer`, `DirectionalShadow`, `RenderPipeline`, `WorldEffects`, `ScreenEffectsController`, `LensField` |
| `src/gui` | Presentation of match/HUD state and player actions | `MatchHud` |

The physical `restoration/` directory is retired. The existing C++ namespace and library/test names remain compatible; they are not paths. The actor/controller and world already have explicit ownership, so this reorganization does not introduce a second entity/component registry. Render snapshots and one-shot visual events form the boundary between gameplay and presentation.

## Runtime ownership

`RunStageViewer` owns one GLFW window, OpenGL context and ImGui context. The native menu starts a `MatchLoop` in that same context. The match owns its stage geometry, `FacePaint`, navigation, `MatchSimulation`, `FollowCamera`, per-actor `CharacterPose`, and character GPU resources. Return/replay destroys these match objects before reopening the stage menu. All GL resources are destroyed before the context is released.

`MatchSimulation` owns eight actors and seven bots in offline Turf. `PlayerSimulation`, `WeaponRunner`, `CombatWorld` and `WorldSpecials` are the shared gameplay path. Pose trigger/frame callbacks consume those states; renderer and UI do not run a second combat implementation. Simulation and the match follow-camera advance at a fixed 60 Hz; rendering displays the latest pose at the window frame rate. Focus loss and pause stop match advancement, and resume resets mouse/fire input edges.

## Paint and presentation

`StageGeometry` supplies collision and paintable/scoring faces. `FacePaint` stores logical ownership and versions changed atlas regions; the stage renderer uploads those changes. `CharacterArt` draws committed source meshes/shaders with live pose matrices and material state. Renderer diagnostics can also draw fixed source snapshots, but gameplay does not use them as animation.

`CharacterLod` selects hero/game/far using projected 1.32 m character height, local/remote and quality thresholds, ±12% hysteresis and a 0.3 s transition. Body tiers use complementary source screen-door masks; equipment changes at the transition midpoint. A source oracle covers all four qualities and both local/remote selection, with GPU transition checks.

`RenderPipeline` resolves a half-float MSAA scene and depth into optional source GTAO/Poisson denoising, source five-mip bloom, theme grade, screen FX, Neutral tone mapping and sRGB output. GUI is drawn afterward. `DirectionalShadow` owns the fitted light camera/depth texture and exposes the reference stable 3×3 PCF sampler. `ScreenEffectsController` and `LensInk` keep temporal presentation state separate from combat; `LensField` renders bounded metaball instances to the HDR lens texture. Exact port choices and targeted evidence are recorded in [the rendering contract](docs/render-pipeline-source-contract.md).

The match submits stage, character and Boss shadow casters before the HDR scene. Character receivers use exported source shadow shaders; the stage uses a local lit material. World FX share the character mesh cache and consume a bounded visual-event queue once per simulation tick. Screen FX read that drained snapshot and receive local damage/splat/match events. Source recipes and known visual differences are recorded in [the World FX contract](docs/worldfx-source-contract.md).

Offline Boss adds an eight-member friendly squad, source navigation, move/hazard state, hit queries and mode-specific HUD/results. Its CPU pose sink runs after the fixed-step Boss update and before combat queries. The current pose publishes a rest rig with static sockets; source animation and material uniforms remain unimplemented and fail the strict native pose oracle. See [the Boss pose contract](docs/boss-pose-source-contract.md). Complete environment/ink materials, audio, saves, online and Zones remain separate spec requirements. Having a shader, state machine or source fixture is not whole-scene acceptance.

## Legacy retirement — 2026-10-20

The user authorized keeping only the new native implementation. The old `engine`, `components`, `gameplay`, `splat`, `scene`, `gui`, and `network` implementations and their unreferenced audio/model/shader/texture assets were removed. The NVIDIA preference export now lives in `src/app`. `fmt` is removed. `GameNetworkingSockets` stays declared in vcpkg for future native multiplayer integration, but current targets neither link nor initialize it; the legacy protocol/manager is removed. Historical code remains recoverable from Git before the retirement commit.

Shared CMake presets are portable; machine-specific user presets stay local. The executable deploys the current reference assets. Release packaging continues to use the same executable name and asset layout.
