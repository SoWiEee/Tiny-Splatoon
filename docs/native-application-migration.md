# Native application entry migration

As of 2026-10-20, restoration is the only application implementation. The user explicitly authorized retiring the legacy program before the full reference feature set is complete. GameNetworkingSockets remains declared in vcpkg for future multiplayer integration, without linking or initialization in the current restoration target. The old LoginScene／LobbyScene／GameWorld／network／audio/rendering modules and unused assets have been removed from the build and repository; they remain available in earlier Git commits.

`Tiny-Splatoon.exe` directly enters offline Turf stage select. `--native` is a compatible alias; `--frames N` draws N menu frames, prints `NATIVE_MENU state=stage-select`, and exits for startup acceptance. Main forwards all arguments to the restoration parser, so unknown options cannot fall back into an obsolete application.

Stage select, the local-player match, pause/return, judge/results and replay share one GLFW window and ImGui context. Returning destroys match-owned stage, paint, simulation, pose and GPU resources while the OpenGL context is current. Closing the application releases those resources before shutting down ImGui, the window and GLFW.

`--reference-match [stage]` starts the same local-player match directly. WASD/arrows move relative to camera; mouse aims; left click fires; right click/E uses the sub; Shift squid-swims; Space jumps; F/Q activates a special; C cheers; Escape pauses/resumes. Losing focus pauses play. Resume resets cursor baseline, synchronizes held sub/special inputs, and blocks fire until release. Strike-style map targets confirm with a click after the source ready delay.

The match HUD shows clock, coverage, HP, ink, special gauge and respawn state. `MatchSimulation` owns combat, bot control, respawn, judge and coverage; StageViewer translates input and presents that state.

Explicit `--reference-stage` and `--reference-game` diagnostics remain available. The latter is the older restoration controller/capsule probe, not the removed legacy program. Probe/capture options require their explicit entry; omitting a selector means native menu. `--native` cannot mix with gameplay diagnostics or benchmark options.

`--benchmark-report` uses a direct match with scripted local input, fixed 60 Hz simulation, hidden borderless 1920×1080 window and fixed warmup/sample counts. The default overview camera omits the HUD; `--benchmark-camera player` uses the real FollowCamera, nearby FX and RmlUi HUD. The report records which camera was used. Both check the actual framebuffer on every measured frame; neither proves the worst-case effects workload. Ordinary startup does not enable scripted controls.

The default entry is now playable restoration, with its current limitations: offline Turf only, simplified menus/results and incomplete visual effects. Full reference hub, loadout/Locker/settings, audio/saves, online, Zones, Boss, full input matrix and final rendering still require spec implementation and acceptance. Legacy removal is an architecture change, not a full-game fidelity claim.
