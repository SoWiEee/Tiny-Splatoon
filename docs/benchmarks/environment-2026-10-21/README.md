# Player-view graphics quality measurements

2026-10-21, MSVC x64 Release, NVIDIA GTX 1650 Max-Q / 591.74, Intel i7-1280P, OpenGL 4.5. Every run uses 1920x1080 output, a real FollowCamera and native-resolution RmlUi HUD, seed 1, eight actors, fixed 60 Hz simulation and vsync off. Runs are serialized with other GPU tests inactive. Each measures 600 frames after Turf 120 / Boss 480 warmup frames; completed-frame FPS includes the final GPU query.

| Mode | Quality | FPS | GPU mean / p95 ms | Mean draws | Mean particles | Peak working set MiB |
| --- | --- | ---: | ---: | ---: | ---: | ---: |
| turf | low | 156.60 | 5.95 / 6.77 | 114.79 | 132.18 | 393.03 |
| boss | low | 143.65 | 6.60 / 7.32 | 145.19 | 149.57 | 405.86 |
| turf | medium | 45.30 | 22.05 / 27.77 | 223.04 | 132.18 | 379.22 |
| boss | medium | 38.87 | 25.69 / 29.55 | 284.73 | 149.57 | 370.58 |
| turf | high | 27.57 | 36.24 / 40.83 | 223.04 | 132.18 | 378.60 |
| boss | high | 22.93 | 43.58 / 49.09 | 284.73 | 149.57 | 354.67 |

Low uses 65% internal scene resolution with shadows, reflections and optional post effects disabled. Medium uses 85%, 1024 shadows, no reflection and AO/bloom/screen effects. High uses 100%, 2048 shadows, marina reflections and post effects. HUD text remains at full output resolution. Individual overrides are available from the menu and apply on the next match. Settings currently live only for this app session.

Low exceeds the 60 FPS target in these two measured cases; Medium and High do not. This is evidence for selectable quality on this device, not the complete performance matrix or a promise for other machines. These short fixed-seed runs do not cover every map, long-session memory behaviour, all simultaneous specials, reduced-motion combinations or browser pixel parity. The earlier pre-environment overview reports are not comparable to this player-view workload.

A preliminary full-quality player-view run measured 19.84 FPS before the shadow fragment path skipped PBR lighting after source alpha clipping. Its diagnostic artifact is retained only under build/. The six final reports above include that optimization and the final quality controls. Raw JSON and per-frame CSV files are committed alongside this note.

The final six runs were refreshed after material identity keys were corrected to include uniform defaults, preserving per-team and per-object colors.
