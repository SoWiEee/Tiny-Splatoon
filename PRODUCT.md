# Product

Tiny-Splatoon is a C++17 / OpenGL 4.5 restoration of the supplied browser game. It began as the user's school computer-graphics final project and is under continued development.

## Platform and stack

Native Windows desktop, GLFW, OpenGL, CMake and vcpkg. RmlUi is the selected direction for player-facing UI; ImGui remains available for diagnostics during migration.

## Purpose and constraints

- Preserve the gameplay, equipment and mode requirements in `spec.md`, using the immutable `splat-ref/` baseline.
- Current playable entries are offline Turf and an incomplete offline Boss checkpoint; do not imply online or Zones are complete.
- Keep one native window/context, fixed-step gameplay ownership and working return/replay flows.
- The user explicitly changed the visual requirement on 2026-10-20: clean, minimal UI with less decoration than the reference; Noto Sans TC for Traditional Chinese and Inter for Latin text and numbers.
- Audio must derive from the reference's synthesis and event parameters, with controllable volume and clean scene teardown.

Success is a readable, operable game interface with reliable mouse/keyboard focus, legible Traditional Chinese, and audible events matching actual gameplay state.
