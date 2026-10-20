# Source scene integration measurement

Final measurement refreshed on 2026-10-21 after source stage geometry, PropKit maps, 46-layer surface textures, continuous GPU ink, projectiles, Boss pose/materials and world audio were integrated. These are Tidewater overview measurements, not the full QA-12 matrix or a player-view worst case. The folder name retains the original integration date.

```powershell
.\build\Tiny-Splatoon.exe --reference-match tidewater --benchmark-report build/current-turf-performance.json --benchmark-warmup 120 --benchmark-frames 600 --width 1920 --height 1080
.\build\Tiny-Splatoon.exe --reference-boss tidewater --benchmark-report build/current-boss-performance.json --benchmark-warmup 480 --benchmark-frames 600 --width 1920 --height 1080
```

GTX 1650 Max-Q / NVIDIA 591.74, i7-1280P, MSVC x64 Release, vsync off. Both runs used 1920×1080 and measured 600 frames, sequentially with no exporter or other GPU test running. Schema 2 includes completion of the last GPU query in the measured wall time.

| Mode | Completed-frame FPS | Wall time (ms) | GPU mean / p95 (ms) | CPU submission mean (ms) | Mean draws | Peak working set (bytes) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Turf | 81.78 | 7,337.21 | 12.16 / 13.08 | 4.34 | 172.03 | 338,501,632 |
| Boss | 67.35 | 8,908.34 | 14.63 / 18.79 | 4.45 | 175.67 | 385,400,832 |

The benchmark omits the player HUD. Its overview culled nearby particles (Turf mean zero, Boss mean 2.36); it therefore does not certify player-view effects load. Full environments and device visuals remain unfinished. Raw per-frame data and reports are committed beside this note. CPU submission rates are not completed-frame throughput, and earlier pre-material benchmarks do not represent the current renderer.
