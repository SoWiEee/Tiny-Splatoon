<div align="center">

# Tiny Splatoon

以 C++／OpenGL 還原的原生塗地射擊遊戲

![C++17](https://img.shields.io/badge/C%2B%2B-17-00599C?style=flat-square&logo=cplusplus&logoColor=white)
![OpenGL 4.5](https://img.shields.io/badge/OpenGL-4.5-5586A4?style=flat-square&logo=opengl&logoColor=white)
![RmlUi](https://img.shields.io/badge/UI-RmlUi-246B58?style=flat-square)
![miniaudio](https://img.shields.io/badge/Audio-miniaudio-7B4DAB?style=flat-square)
![CMake](https://img.shields.io/badge/Build-CMake-064F8C?style=flat-square&logo=cmake&logoColor=white)
[![Windows Build](https://github.com/SoWiEee/Tiny-Splatoon/actions/workflows/release.yml/badge.svg)](https://github.com/SoWiEee/Tiny-Splatoon/actions/workflows/release.yml)

[下載遊戲](https://github.com/SoWiEee/Tiny-Splatoon/releases/latest) · [還原進度](docs/restoration-progress.md) · [工程規格](spec.md) · [Old Demo](https://youtu.be/rS_o25PM3hM)

<sub>Old Demo 為原始期末專案影片；以下截圖為目前原生版本。</sub>

</div>

## 🎮 Features

- 離線 4v4 塗地、區域爭奪（Zones）、八人協力 Boss 與自由練習。
- 角色換裝、主／副／特殊武器、Bot AI 與固定 60 Hz 模擬。
- 連續墨水、動態場景、陰影／反射／後製與可調畫質。
- 繁中介面、Noto Sans TC／Inter、音樂與定位音效、玩家資料自動保存。

![原生標題與角色預覽](docs/images/menu.png)
*標題選單與角色預覽。*

![塗地對戰遊戲畫面](docs/images/turf.png)
*原生塗地對戰。*

![區域爭奪遊戲畫面](docs/images/zones.png)
*Zones 計數、控制區與小地圖。*

完整還原仍在進行；線上多人尚未開放。已完成範圍與差異請見[還原進度](docs/restoration-progress.md)及[待辦清單](docs/next-round-backlog.md)。

## 🛠️ Build & Run

直接遊玩：下載 [Windows x64 Release](https://github.com/SoWiEee/Tiny-Splatoon/releases/latest)，完整解壓後執行 `Tiny-Splatoon.exe`。

原始碼建置需要 Windows、OpenGL 4.5、Visual Studio C++ 工具、CMake、Ninja 與 vcpkg。在 **x64 Native Tools** 終端執行：

```powershell
$env:VCPKG_ROOT = 'C:\path\to\vcpkg'
cmake --preset vcpkg -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
.\build\Tiny-Splatoon.exe
```

[開發與驗證指引](docs/development.md) · [套件相依](vcpkg.json) · [玩家保存與設定](docs/save-player-flow.md)

## ⌨️ Controls

| 操作 | 按鍵 |
| --- | --- |
| 移動／瞄準 | WASD 或方向鍵／滑鼠 |
| 主武器／副武器 | 左鍵／右鍵或 E |
| 變形、己墨游泳／跳躍 | Shift／Space |
| 特殊技能／歡呼 | F 或 Q／C |
| 練習裝備／暫停 | L／Esc |

手把與完整操作說明可在遊戲選單查看。

## 🔎 Diagnose & Verify

```powershell
ctest --test-dir build -C Release --output-on-failure
.\build\Tiny-Splatoon.exe --reference-zones tidewater
```

[擷取、效能與來源對照](docs/development.md) · [最新驗收](docs/acceptance-2026-10-22.md) · [發布流程](docs/release-workflow.md)

## 🗂️ Project Layout

| 路徑 | 用途 |
| --- | --- |
| `src/app/` | 視窗、輸入與遊戲流程 |
| `src/game/`、`src/world/` | 模式、角色、武器、AI、地形與墨水 |
| `src/render/`、`src/gui/`、`src/audio/` | OpenGL、RmlUi 與 miniaudio |
| `assets/` | 已匯出的原生資產、UI、字體與音訊 |
| `tests/`、`tools/` | 回歸、來源匯出與效能工具 |
| `docs/` | 契約、還原進度與驗收證據 |

[架構說明](architecture.md) · [UI 翻譯](docs/ui-translations.md) · [場景](docs/stage-visual-source-contract.md) · [視效](docs/worldfx-source-contract.md) · [音訊](docs/audio-source-contract.md)
