# Native architecture

`main.cpp` forwards every launch to `restoration::RunStageViewer`. The default launch opens the native player menu. `--native` remains an alias; `--reference-practice`, `--reference-match`, `--reference-boss`, and `--reference-zones` select the current native gameplay paths. Retired stage/practice diagnostic selectors are rejected. Unknown options fail during parsing before GLFW initialization.

## Build ownership

- `restoration_core` is the C++17 static library for reference data, stage geometry, CPU paint, player/controller, weapons, combat world, specials, navigation, bots, match lifecycle, and live character poses. It depends on GLM and has no GLFW or network lifetime.
- `Tiny-Splatoon` adds `StageViewer`, `CharacterArt`, `CharacterShader`, `PerformanceCapture`, and `GpuPreference`. GLFW／GLAD／OpenGL own the window and graphics; RmlUi owns all interface pages and stb writes validation PNGs.
- `player_ui` owns `GameUi` and the RmlUi OpenGL renderer. RML/RCSS assets describe the menu and HUD; UI emits actions and settings rather than mutating the simulation directly. Inter and Noto Sans TC are bundled under OFL.
- `game_audio` owns miniaudio playback, source cue metadata, bounded one-shot voices, positional loops and streamed source music. `GameAudio` maps actor, world, match and Boss state to that playback interface. Source-rendered assets and remaining limits are documented in [the audio contract](docs/contracts/audio-source-contract.md).
- Tests link the same core library. Source exporters operate on the locally supplied, hash-locked `splat-ref/`; native fixtures and application builds use committed data.

## Source layers

| Directory | Responsibility | Main owners |
| --- | --- | --- |
| `src/app` | Launch options, GLFW lifetime, input and match/menu orchestration | `StageViewer`, `GpuPreference` |
| `src/game` | Fixed-step actor, weapons, combat, AI, modes and procedural character poses | `MatchSimulation`, `PlayerSimulation`, `CombatWorld`, `WorldSpecials`, `BossSimulation` |
| `src/world` | Committed map data, geometry queries, logical ink and scoring | `ReferenceData`, `StageGeometry`, `FacePaint` |
| `src/render` | Context-local drawing, LOD, shadows, HDR postprocessing and visual state | `CharacterArt`, `CharacterLod`, `StageRenderer`, `DirectionalShadow`, `RenderPipeline`, `WorldEffects`, `ScreenEffectsController`, `LensField` |
| `src/gui` | Presentation of match/HUD state and player actions | `GameUi`, `RmlRenderer` |
| `src/audio` | Cached PCM, bounded playback, volume buses and gameplay audio routing | `AudioEngine`, `GameAudio` |

The physical `restoration/` directory is retired. The existing C++ namespace and library/test names remain compatible; they are not paths. The actor/controller and world already have explicit ownership, so this reorganization does not introduce a second entity/component registry. Render snapshots and one-shot visual events form the boundary between gameplay and presentation.

## Runtime ownership

`RunStageViewer` owns one GLFW window, OpenGL context, `GameUi` and `AudioEngine`. The native menu starts a `MatchLoop` in that same context. The match owns its stage geometry, `FacePaint`, navigation, `MatchSimulation`, `FollowCamera`, per-actor `CharacterPose`, and character GPU resources. Return/replay destroys these match objects and clears active sounds before reopening the stage menu. All UI and GL resources are destroyed before the context is released; audio sounds release their borrowed PCM cursors before the engine and cache are destroyed.

`MatchSimulation` owns eight actors and seven bots in offline Turf. `PlayerSimulation`, `WeaponRunner`, `CombatWorld` and `WorldSpecials` are the shared gameplay path. Pose trigger/frame callbacks consume those states; renderer and UI do not run a second combat implementation. Simulation and the match follow-camera advance at a fixed 60 Hz; rendering displays the latest pose at the window frame rate. Focus loss and pause stop match advancement, and resume resets mouse/fire input edges.

## Paint and presentation

`StageGeometry` supplies collision and paintable/scoring faces. `FacePaint` stores logical ownership for gameplay and queues visual splats separately. `StageRenderer` draws exported source bevel/PropKit geometry and materials, using captured source texture arrays, atlases and murals; its source paint shaders grow splats and dry wet ink independently of the ownership grid. `CharacterArt` draws committed source meshes/shaders with live pose matrices and material state. Renderer diagnostics can also draw fixed source snapshots, but gameplay does not use them as animation.

`ProjectileRenderer` reads live combat rounds and drained visual events. Source deformed spheres, satellites, bow arrows, mitts fists, blade waves and charger ribbons are presentation only; collision, paint and damage remain in `CombatWorld`. It shares the existing source material shader path and DFG table. See [the projectile contract](docs/contracts/projectile-source-contract.md) for fixture and GPU checks and remaining limits.

`CharacterLod` selects hero/game/far using projected 1.32 m character height, local/remote and quality thresholds, ±12% hysteresis and a 0.3 s transition. Body tiers use complementary source screen-door masks; equipment changes at the transition midpoint. A source oracle covers all four qualities and both local/remote selection, with GPU transition checks.

`RenderPipeline` resolves a half-float MSAA scene and depth into optional source GTAO/Poisson denoising, source five-mip bloom, theme grade, screen FX, Neutral tone mapping and sRGB output. GUI is drawn afterward. `DirectionalShadow` owns the fitted light camera/depth texture and exposes the reference stable 3×3 PCF sampler. `ScreenEffectsController` and `LensInk` keep temporal presentation state separate from combat; `LensField` renders bounded metaball instances to the HDR lens texture. Exact port choices and targeted evidence are recorded in [the rendering contract](docs/contracts/render-pipeline-source-contract.md).

The match submits stage, character and Boss shadow casters before the HDR scene. Character and stage receivers use exported source material/shadow shaders. World FX share the character mesh cache and consume a bounded visual-event queue once per simulation tick. Screen FX and projectile presentation read that drained snapshot; local damage/splat/match events also reach the screen controller. Source recipes and known visual differences are recorded in [the World FX contract](docs/contracts/worldfx-source-contract.md).

Offline Boss adds an eight-member friendly squad, source navigation, move/hazard state, hit queries and mode-specific HUD/results. Its CPU pose sink runs after the fixed-step Boss update and before combat queries. The pose evaluates source clips, gait, IK, springs and secondary chains; its 100-bone/socket/material oracle passes all 16 scenarios at the original 0.001 threshold. The renderer uses six source material shaders, UVs, stencil, DFG and live pose uniforms. See [the Boss pose contract](docs/contracts/boss-pose-source-contract.md) and [material contract](docs/contracts/boss-material-source-contract.md). Environment lighting, remaining device/audio details, saves, online and Zones remain separate spec requirements. Having a shader, state machine or source fixture is not whole-scene acceptance.

## Legacy retirement — 2026-10-20

The user authorized keeping only the new native implementation. The old `engine`, `components`, `gameplay`, `splat`, `scene`, `gui`, and `network` implementations and their unreferenced audio/model/shader/texture assets were removed. The NVIDIA preference export now lives in `src/app`. `fmt` is removed. `GameNetworkingSockets` stays declared in vcpkg for future native multiplayer integration, but current targets neither link nor initialize it; the legacy protocol/manager is removed. Historical code remains recoverable from Git before the retirement commit.

Shared CMake presets are portable; machine-specific user presets stay local. The executable deploys the current reference assets. Release packaging continues to use the same executable name and asset layout.
