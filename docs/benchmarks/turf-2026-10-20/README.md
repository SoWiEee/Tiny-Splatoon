# Live Turf 診斷效能紀錄 — 2026-10-20

這份紀錄只驗收現有原生 Turf 模組場景。`qa12_claim=false`；未包含最終環境、陰影、後製、projectile meshes、particles、特殊 world props／視效與 Boss。不能把結果外推為完成版 1080p high 的 QA-12 通過。

Release／MSVC x64，OpenGL 4.5，1920×1080，VSync off，固定 seed 1／60 Hz simulation。每圖 120 warmup + 600 measured frames，每幀執行 MatchSimulation、八名 live actor／CharacterPose 及來源角色 mesh／現有地形與墨渲染；body 固定 game tier，使用固定 overview camera，略過互動 HUD。量測時沒有同時執行編譯或測試。

硬體：Intel Core i7-1280P（20 logical processors），NVIDIA GeForce GTX 1650 with Max-Q Design，NVIDIA driver 591.74。隱藏量測視窗採無邊框，避免視窗外框限制 client framebuffer；仍逐幀強制驗證實際 1920×1080，沒有更改顯示器設定。原先有邊框的量測在尺寸檢查時拒絕，該次沒有列入結果。

| Stage | Mean FPS | CPU mean ms | CPU p95 ms | GPU p95 ms | Draws/frame | Peak working set MiB |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| tidewater | 351.9 | 2.8419 | 3.6454 | 1.2124 | 75.8 | 176.1 |
| kelpline | 423.3 | 2.3626 | 2.8608 | 1.1927 | 80.1 | 176.3 |
| halyard | 484.9 | 2.0624 | 2.5830 | 1.1969 | 70.5 | 173.8 |
| saltpan | 444.9 | 2.2477 | 2.6908 | 1.2191 | 76.1 | 174.3 |
| crossmarket | 301.8 | 3.3135 | 4.2004 | 1.1213 | 80.0 | 174.5 |
| lockgate | 430.4 | 2.3237 | 2.9571 | 1.4123 | 73.0 | 174.4 |
| terraces | 327.1 | 3.0575 | 4.2609 | 1.6220 | 75.8 | 178.3 |

七圖均符合本輪平均 ≥60 FPS、CPU p95 ≤16.667 ms 的診斷門檻。FPS 是包含 simulation／draw submission／swap 的 CPU frame mean 之倒數；GPU 時間由 GL_TIME_ELAPSED query 取得。每幀 actors／draws／particles／projectiles、process working set 與可取得的 NVIDIA available-memory 計數保留在各 CSV；零 particles 反映視效尚未接入。

重現指令（專案根目錄）：

```powershell
./tools/Measure-ReferenceTurf.ps1 -BuildDirectory ./build -WarmupFrames 120 -MeasuredFrames 600 -OutputDirectory ./build/captures/turf-2026-10-20
```

JSON／CSV 值保留原報告，這裡僅正規化為 UTF-8／LF。JSON 包含實際 CPU／GPU／driver、量測設定、記憶體與限制；CSV 共 4200 個實際量測影格。

綁定本次建置的來源／執行檔 SHA-256：

- `restoration/CharacterPose.cpp`: `aad24bfa723e015419827a4d2031f74e0c4eddb943012149b55075745324c8eb`
- `restoration/StageViewer.cpp`: `58afbc7c3c3e381dd0bb6bb9a1b5e83b02515eae3fdaa812589bfafbce2bfbaf`
- `build/Tiny-Splatoon.exe`: `7427d9c2bfc91a5a158fbf4aef19f1585753b963b36d3520518891fb65143143`

本次報告 SHA-256：

- [crossmarket-seed1.csv](crossmarket-seed1.csv): `66a60f051d462ad6bf388f06b99e6096698032ea169b87f173d08571e41f3513`
- [crossmarket-seed1.json](crossmarket-seed1.json): `ebc30f0a3f9701ac812a1fde07f408189e402f77807ecb43b67b8a9fb07ca3da`
- [halyard-seed1.csv](halyard-seed1.csv): `7890370260a7ca844b6b92980cd4519ebffa56c8adf3d74f00a2c9dc8be11a6c`
- [halyard-seed1.json](halyard-seed1.json): `7437403cb6ab544464995415a3d617fcd7b1d87ef86862becf9b0c6375bec828`
- [kelpline-seed1.csv](kelpline-seed1.csv): `6d3328daac8004baa5c74c5e88aae653a37fd40b378937d5941accb7a24f4ae1`
- [kelpline-seed1.json](kelpline-seed1.json): `bcd06e6d47db299082ac9f077b7f3d4db6d804e1ec936a4bc523e84b4bff1a69`
- [lockgate-seed1.csv](lockgate-seed1.csv): `328511fbd7319516af9c38c6303bcf99b6427b303adf39531d20948b1e993105`
- [lockgate-seed1.json](lockgate-seed1.json): `058f194845cdc42590a1503805d22237e67e9fb9d963d0895b70f8e530cf6a4f`
- [saltpan-seed1.csv](saltpan-seed1.csv): `09950b6d86a529b09525f85baf81b26a092f71f0f4e637524be7ecb168d80933`
- [saltpan-seed1.json](saltpan-seed1.json): `92d3ca28d54314b61797034c3019cc0e73ce84c7c4508b208e48eb460a9281a5`
- [terraces-seed1.csv](terraces-seed1.csv): `1672701593808224326e83fd6d65bd0bec145c8863696fe78c3c3959cb96ab0c`
- [terraces-seed1.json](terraces-seed1.json): `6c59c63a16746af9fe1ec2e832f31d0c19bc0dff930f36cf49b4210d5b21f35c`
- [tidewater-seed1.csv](tidewater-seed1.csv): `4e568fb1be784455cac33ba48a5e33aa0d693e98ce9ba750fe00de6aa50df283`
- [tidewater-seed1.json](tidewater-seed1.json): `72fa8fbd8cbcbef06ea9e1bc459a0f4c08a2b94318d7e76a23eee0891dfdf0c1`
