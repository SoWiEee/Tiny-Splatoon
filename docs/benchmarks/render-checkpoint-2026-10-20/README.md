# LOD／渲染／Boss 開發檢查點 — 2026-10-20

這是單圖診斷，不是完整 QA-12。MSVC x64 Release，GTX 1650 Max-Q／NVIDIA 591.74，i7-1280P，1920×1080，VSync off，seed 1，固定 60 Hz simulation。測量時沒有同時編譯或跑其他測試。實際 GPU／CPU／版本、逐幀資料與限制均保留於各 JSON／CSV。

| 模式 | 暖機＋測量幀 | GPU 完成後平均 FPS | CPU p95 ms | GPU p95 ms | draws/frame | particles/frame |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Tidewater Turf | 120＋600 | 210.7129 | 4.4099 | 4.8968 | 155.6817 | 0.0000 |
| Tidewater Boss | 480＋600 | 192.3630 | 4.3689 | 5.7078 | 162.0017 | 1.0467 |

Schema 2 在暖機結束同步一次 GPU；`fps_mean` 為測量幀數除以測量起點至最後 GPU query 完成的 wall time。`cpu_submit_fps_mean` 保留舊的 CPU frame 倒數，但不拿它當 GPU 完成後的吞吐量。GPU frame 時間仍由 `GL_TIME_ELAPSED` 取得。Shadow、場景、角色、FX 與後製 draw 均計入；benchmark 不繪製互動 HUD。

此版本啟用投影尺寸 LOD、2048 shadow map、4× MSAA HDR、GTAO／Poisson、五層 bloom、grade／screen composite。場地材質仍為本機近似；projectile/device world meshes 未完整繪製。Boss 使用來源幾何和靜態骨架，完整程序動畫與來源材質尚未完成。俯視 camera 的距離剔除使這段 Turf 平均 particles 為零，因此這不是全特殊視效滿負載測試；另以近鏡 GPU 測試檢查龍捲風／音箱／光束／泡泡的實際 draw 與非背景像素。`qa12_claim=false`。

重現指令（專案根目錄；需要 GTX 1650）：

```powershell
.\build\Tiny-Splatoon.exe --reference-match tidewater --benchmark-report build/checkpoint-turf.json --benchmark-warmup 120 --benchmark-frames 600
.\build\Tiny-Splatoon.exe --reference-boss tidewater --benchmark-report build/checkpoint-boss.json --benchmark-warmup 480 --benchmark-frames 600
ctest --test-dir build -R reference_worldfx_gpu --output-on-failure
```

量測執行檔 SHA-256：`67344024c20298bb09066619cf11d5e244f8361936e7c0f463d4c66394a01d6d`。

- [Turf JSON](checkpoint-turf.json)／[600 幀 CSV](checkpoint-turf.csv)
- [Boss JSON](checkpoint-boss.json)／[600 幀 CSV](checkpoint-boss.csv)
