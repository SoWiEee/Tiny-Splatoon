# Tiny-Splatoon

[![Release Windows Build](https://github.com/SoWiEee/Tiny-Splatoon/actions/workflows/release.yml/badge.svg)](https://github.com/SoWiEee/Tiny-Splatoon/actions/workflows/release.yml)

以 C++17／OpenGL 4.5 還原 `splat-ref/` 的塗地射擊遊戲。原生程式位於依職責分層的 `src/`，直接執行 `Tiny-Splatoon.exe` 即進入 Tiny Splatoon 標題與玩家首頁。

目前可玩的是一名玩家與七名 Bot 的 4v4 Turf：選圖、移動／瞄準／射擊、變形／游泳／跳躍、副武器／特殊技能、暫停／返回、判分／結果與重開。模擬採固定 60 Hz，角色使用來源幾何與即時計算的骨架／程序動畫。

完整還原尚在進行；投影尺寸 LOD 已加入來源門檻、遲滯與交錯淡變。玩家選單與 HUD 使用 RmlUi，採簡約版面、Inter 與 Noto Sans TC，優先顯示繁體中文。可選離線塗地／Boss、地圖、裝備與難度，並調整音量和減少動態效果。場景已接入來源 bevel／PropKit 幾何、46 層程序材質與連續墨水；投射物使用來源墨滴、弓箭、拳套、刀波與蓄力光帶。miniaudio 的來源資料已擴充為 201 個音效及九首曲目，整合驗收進度見 [還原紀錄](docs/restoration-progress.md)。首頁、角色置物櫃、3D 換裝預覽、結果頒獎台與設定保存已接入；線上與 Zones 仍須依 [spec.md](spec.md) 開發。Boss 的 100 骨架／命中 socket／材質數值已通過 16 組來源姿勢比對，完整模式與材質畫面仍在驗收。

本輪已接入來源天空／海面／雲／遠景、港區反射、PMREM 與共用光照，裝置目錄擴為雙隊 58 個來源模型，並讓布條與道具持續動畫。潛水變形、潑墨動作與音訊的實際驗收及保留差異見 [本輪驗收](docs/acceptance-2026-10-21.md)。

選單「設定」可選低／中／高畫質，或個別切換陰影、反射及後製。低畫質使用65%內部解析度並停用這三項效果；中畫質使用85%及1024陰影；高畫質使用原生解析度及2048陰影。HUD與字體維持原生解析度，畫質設定於下一場對局套用，音量即時套用。玩家名稱、裝備、外觀、設定及 XP 會自動保存至 `%LOCALAPPDATA%/Tiny-Splatoon/profile.json`，寫入失敗時介面顯示錯誤。診斷入口可使用 `--graphics-quality low|medium|high`，效能報告會記錄實際設定。保存契約與限制見 [玩家流程](docs/save-player-flow.md)，繁中名稱來源見 [翻譯表](docs/ui-translations.md)。

## 建置與執行

需求：Windows、支援 OpenGL 4.5 的顯卡、C++17 編譯器（已驗證 MSVC x64）、CMake、Ninja 與 vcpkg。相依套件由 [vcpkg.json](vcpkg.json) 宣告：GLFW、GLAD、GLM、RmlUi、miniaudio、Dear ImGui、OpenGL、zlib 與 stb。Dear ImGui 用於開發診斷介面；GameNetworkingSockets 留供後續原生多人連線接入，目前 target 不連結或初始化它。

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

`assets/` 會複製到執行檔旁，包含來源匯出資料、UI、字體與音效。一般建置／遊玩使用已提交的原生資料，不需要瀏覽器、Node.js 或 `splat-ref/`。Inter 與 Noto Sans TC 的授權檔位於 `assets/fonts/`。個人的 `CMakeUserPresets.json` 保留在本機並由 Git 忽略。

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
| L | 練習中的裝備與外觀選單 |
| Esc | 暫停／繼續 |

失去視窗焦點會暫停；重新進入操作時需放開射擊鍵，避免按鈕殘留造成誤射。暫停與結果畫面可返回選圖，或關閉遊戲。

## 診斷與驗證

`--native` 是預設入口的相容別名。開發用指令與玩家入口使用同一套原生模擬與渲染：

```powershell
.\build\Tiny-Splatoon.exe --frames 3
.\build\Tiny-Splatoon.exe --reference-match tidewater
.\build\Tiny-Splatoon.exe --reference-boss tidewater --duration 240
.\build\Tiny-Splatoon.exe --reference-game halyard
.\build\Tiny-Splatoon.exe --reference-stage halyard --reference-mode turf
ctest --test-dir build --output-on-failure
.\tools\Measure-ReferenceTurf.ps1 -BuildDirectory .\build
```

`--frames 3` 可驗證預設選圖入口的開窗／繪製／結束。`--reference-game` 是控制器／膠囊練習診斷；實際角色與 Bot 對戰由預設選單或 `--reference-match` 進入。地形／練習擷取可使用 [check-stage-viewer.ps1](tools/check-stage-viewer.ps1) 與 [check-native-practice.ps1](tools/check-native-practice.ps1)。

`--reference-boss` 是開發中的離線八人協力 Boss 模式，時間可選 180／240／300 秒。模式入口不代表 Boss 動畫、材質與全部技能演出已通過來源驗收；目前限制見 [Boss 姿勢契約](docs/boss-pose-source-contract.md)。

若有 Node.js 22.17+，CTest 亦註冊 32 基本＋19 變體的 native pose fixture 對照與 CLI 測試。要重新匯出來源資料，另外在本機放置符合 [來源基準](docs/reference-baseline.sha256) 的 `splat-ref/`；這些來源重建測試僅在資料夾存在時註冊，不得直接改寫基準來掩蓋差異。

GTX 1650 的舊七圖 1080p live Turf 診斷曾記錄 301.8–484.9 的 CPU frame 換算 FPS；原始資料與工作量限制見 [效能紀錄](docs/benchmarks/turf-2026-10-20/README.md)。那份 schema 1 紀錄未等待最後的 GPU 工作完成，不能等同整段 GPU 完成後的吞吐量，也未包含最終陰影／後製／特殊視效與 Boss。現行 schema 2 以測量區間開始至最後 GPU query 完成的實際耗時算 FPS，另外保留 CPU 換算值。完整 QA-12 仍未通過。

## 程式結構

```text
main.cpp                  原生程式的單一啟動入口
src/app/                  CLI、輸入、視窗生命週期與對局流程協調
src/game/                 Actor、武器、戰鬥、AI、模式與角色姿勢計算
src/world/                地圖資料、碰撞幾何與邏輯墨色
src/render/               角色／場地繪製、LOD、陰影、HDR／FX與GPU資源
src/gui/                  HUD與玩家介面繪製
src/audio/                miniaudio播放、PCM快取與音量群組
assets/ui/                RmlUi文件與樣式
assets/fonts/             Inter、Noto Sans TC與OFL授權
assets/audio/             來源渲染WAV、FLAC音樂、播放名冊與manifest
assets/reference/         已提交的來源匯出資料與角色資產
tests/                    原生行為、來源 oracle、GPU 與 CLI 驗證
tools/                    來源匯出器、擷取與效能腳本
docs/                     來源基準、驗收進度與量測紀錄
```

- [架構與生命週期](architecture.md)
- [原生入口遷移紀錄](docs/native-application-migration.md)
- [工程規格](spec.md)
- [逐項還原進度](docs/restoration-progress.md)
- [音效來源與目前限制](docs/audio-source-contract.md)
- [場景／連續墨水來源契約](docs/stage-visual-source-contract.md)
- [投射物來源契約](docs/projectile-source-contract.md)
- [Boss 姿勢來源契約](docs/boss-pose-source-contract.md)
- [下一輪候選工作](docs/next-round-backlog.md)
- [手動清理清單](docs/cleanup-checklist.md)

## 舊版展示

[原始期末專案影片](https://youtu.be/rS_o25PM3hM) 展示已移除的舊 OpenGL 版本，保留作為歷史紀錄；不代表目前版本的畫面與功能。
