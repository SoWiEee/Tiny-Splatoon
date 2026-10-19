# Tiny-Splatoon

[![Release Windows Build](https://github.com/SoWiEee/Tiny-Splatoon/actions/workflows/release.yml/badge.svg)](https://github.com/SoWiEee/Tiny-Splatoon/actions/workflows/release.yml)

以 C++17／OpenGL 4.5 還原 `splat-ref/` 的塗地射擊遊戲。原生程式現在只使用 `restoration/`，直接執行 `Tiny-Splatoon.exe` 即進入離線 Turf 選圖畫面。

目前可玩的是一名玩家與七名 Bot 的 4v4 Turf：選圖、移動／瞄準／射擊、變形／游泳／跳躍、副武器／特殊技能、暫停／返回、判分／結果與重開。模擬採固定 60 Hz，角色使用來源幾何與即時計算的骨架／程序動畫。

完整還原尚在進行；正式 hub／Locker／設定、部分戰鬥視效、投影尺寸 LOD 漸變、音效、保存、線上、Zones 與 Boss 仍須依 [spec.md](spec.md) 開發與驗收。舊版登入／大廳、GameNetworkingSockets 連線與引擎程式已移除，不能當成這些功能已完成。

## 建置與執行

需求：Windows、支援 OpenGL 4.5 的顯卡、C++17 編譯器（已驗證 MSVC x64）、CMake、Ninja 與 vcpkg。相依套件由 [vcpkg.json](vcpkg.json) 宣告：GLFW、GLAD、GLM、Dear ImGui、OpenGL 與 stb。GameNetworkingSockets 仍列於 vcpkg 供後續原生多人連線接入，但目前 target 不連結或初始化它。

在 Visual Studio x64 Native Tools 命令環境中執行：

```powershell
$env:VCPKG_ROOT = 'C:\path\to\vcpkg'
cmake --preset vcpkg -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
.\build\Tiny-Splatoon.exe
```

也可直接指定 toolchain：

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --parallel 4
```

`assets/reference/` 會複製到執行檔旁。一般建置／遊玩使用已提交的原生資料，不需要瀏覽器、Node.js 或 `splat-ref/`。個人的 `CMakeUserPresets.json` 保留在本機並由 Git 忽略。

Optimus 筆電會透過 `NvOptimusEnablement` 請求 NVIDIA；啟動紀錄中的 `GPU` 行顯示實際 renderer。Windows／驅動的個別程式偏好仍可能覆寫它。

## 操作

| 按鍵 | 動作 |
| --- | --- |
| WASD／方向鍵 | 相對攝影機移動 |
| 滑鼠 | 瞄準與轉視角 |
| 左鍵 | 主武器射擊 |
| 右鍵／E | 副武器 |
| Shift | 變形／己墨游泳 |
| Space | 跳躍 |
| F／Q | 啟動特殊技能 |
| C | Cheer |
| Esc | 暫停／繼續 |

失去視窗焦點會暫停；重新進入操作時需放開射擊鍵，避免按鈕殘留造成誤射。暫停與結果畫面可返回選圖，或關閉遊戲。

## 診斷與驗證

`--native` 是預設入口的相容別名。開發用指令仍使用同一套 restoration 模擬與渲染：

```powershell
.\build\Tiny-Splatoon.exe --frames 3
.\build\Tiny-Splatoon.exe --reference-match tidewater
.\build\Tiny-Splatoon.exe --reference-game halyard
.\build\Tiny-Splatoon.exe --reference-stage halyard --reference-mode turf
ctest --test-dir build --output-on-failure
.\tools\Measure-ReferenceTurf.ps1 -BuildDirectory .\build
```

`--frames 3` 可驗證預設選圖入口的開窗／繪製／結束。`--reference-game` 是控制器／膠囊練習診斷；實際角色與 Bot 對戰由預設選單或 `--reference-match` 進入。地形／練習擷取可使用 [check-stage-viewer.ps1](tools/check-stage-viewer.ps1) 與 [check-native-practice.ps1](tools/check-native-practice.ps1)。

若有 Node.js 22.17+，CTest 亦註冊 32 基本＋19 變體的 native pose fixture 對照與 CLI 測試。要重新匯出來源資料，另外在本機放置符合 [來源基準](docs/reference-baseline.sha256) 的 `splat-ref/`；這些來源重建測試僅在資料夾存在時註冊，不得直接改寫基準來掩蓋差異。

GTX 1650 的七圖 1080p live Turf 診斷曾達平均 301.8–484.9 FPS；原始資料與工作量限制見 [效能紀錄](docs/benchmarks/turf-2026-10-20/README.md)。該紀錄綁定當次建置，未包含最終陰影／後製／特殊視效與 Boss，完整 QA-12 仍未通過。

## 程式結構

```text
main.cpp                  restoration 的單一啟動入口
restoration/              模擬、角色、場地、渲染、輸入與視窗生命週期
assets/reference/         已提交的來源匯出資料與角色資產
tests/                    原生行為、來源 oracle、GPU 與 CLI 驗證
tools/                    來源匯出器、擷取與效能腳本
docs/                     來源基準、驗收進度與量測紀錄
```

- [架構與生命週期](architecture.md)
- [原生入口遷移紀錄](docs/native-application-migration.md)
- [工程規格](spec.md)
- [逐項還原進度](docs/restoration-progress.md)

## 舊版展示

[原始期末專案影片](https://youtu.be/rS_o25PM3hM) 展示已移除的舊 OpenGL 版本，保留作為歷史紀錄；不代表目前 restoration 的畫面與功能。
