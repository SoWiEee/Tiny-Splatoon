# Tiny-Splatoon

[![Release Windows Build](https://github.com/SoWiEee/Tiny-Splatoon/actions/workflows/release.yml/badge.svg)](https://github.com/SoWiEee/Tiny-Splatoon/actions/workflows/release.yml)
[![Ask DeepWiki](https://deepwiki.com/badge.svg)](https://deepwiki.com/SoWiEee/Tiny-Splatoon)

一款以《斯普拉遁 (Splatoon)》為靈感、從零打造的 3D 多人塗地射擊遊戲，使用現代 C++ 與 OpenGL 開發。兩支隊伍在 3D 場地中互相塗色，時間結束時佔地面積較大的隊伍獲勝。整個專案沒有依賴任何遊戲引擎框架，渲染、音效、網路連線與墨水塗地系統皆為手工實作。

> [!NOTE]
> 這是一個學習／作品集性質的專案，目標平台為 Windows，採用 listen-server 架構——主機同時也是玩家。

## Features

- **團隊塗地對戰** — 紅隊 vs. 綠隊，塗滿地圖、以佔地率決勝。單場時間 180 秒。
- **游泳機制** — 在自己的墨水中游泳可加速；敵方墨水會使你減速並暴露行蹤。
- **三種武器** — Shooter（快速墨球）、Brush（扇形擴散）、Slosher（重型拋射），於大廳中選擇。
- **特殊武器與道具** — 集氣後可施放火箭特殊技，場地各處還散布著可拾取的炸彈。
- **線上多人連線** — 透過 Valve GameNetworkingSockets 於區網或網際網路上開房／加入（支援 IPv4、IPv6、DNS 與 `host:port`）。
- **自製引擎** — 自行實作的 OpenGL 渲染器、GLSL 著色器、`.obj` 模型載入、粒子特效、`miniaudio` 音效層，以及基於 ImGui 的介面。

## Tech Stack

| 領域 | 技術 |
|---|---|
| 語言 | C++17 |
| 圖形 | OpenGL 4.5 · GLFW · GLAD · GLM |
| 介面 | Dear ImGui |
| 音效 | miniaudio |
| 網路 | Valve GameNetworkingSockets |
| 圖片載入 | stb |
| 建置 | CMake + vcpkg |

## Getting Started

最快的遊玩方式是從 [Releases](https://github.com/SoWiEee/Tiny-Splatoon/releases) 頁面下載預先建置好的 Windows 執行檔——解壓縮後直接執行 `Tiny-Splatoon.exe` 即可。

### Build from source

**前置需求**

- CMake 3.10+
- [vcpkg](https://github.com/microsoft/vcpkg)
- 支援 C++17 的編譯器（Windows 上建議使用 MSVC）

**步驟**

相依套件皆宣告於 [`vcpkg.json`](vcpkg.json)，並透過 vcpkg toolchain 自動解析安裝。

```bash
git clone https://github.com/SoWiEee/Tiny-Splatoon.git
cd Tiny-Splatoon
cmake -S . -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=<path-to-vcpkg>/scripts/buildsystems/vcpkg.cmake
cmake --build build
```

建置完成後會自動將 `assets/` 複製到執行檔旁，因此可直接在建置輸出目錄中執行。

> [!TIP]
> 在 Windows 上若已有本機 vcpkg，可使用內建的 CMake presets 簡化設定：
> ```bash
> cmake --preset default
> cmake --build build
> ```
> 請將 [`CMakeUserPresets.json`](CMakeUserPresets.json) 中的 `VCPKG_ROOT` 調整為你自己的路徑。

## How to Play

1. **Login** — 點擊 **Host Server** 於指定 UDP 埠開房，或輸入位址（支援 `host:port`）後 **Connect** 以玩家身分加入。
2. **Lobby** — 主機固定為第一個位置；在主機等待玩家期間選擇武器，接著由主機點擊 **Start Game**。
3. **Battle** — 盡情塗地。180 秒後哨聲響起，佔地率最高的隊伍獲勝。

### Controls

| 操作 | 動作 |
|---|---|
| `W` `A` `S` `D` | 移動（相對於攝影機方向） |
| 滑鼠 | 視角 |
| 滑鼠左鍵 | 射擊 |
| `Space` | 跳躍 |
| `Left Shift` | 游泳（僅限站在自己的墨水上） |
| `Q` | 火箭特殊技（集滿氣時） |
| `R` | 投擲炸彈（持有炸彈時） |

## Project Structure

```
Tiny-Splatoon/
├── engine/       # 視窗、輸入、計時、渲染、音效、場景框架
├── gameplay/     # 玩家、武器、拋射物、道具、GameWorld 模擬
├── splat/        # 墨水系統：SplatMap、painter、physics、renderer
├── scene/        # Login / Lobby / Game 場景與場地 (Level)
├── network/      # 封包協定與 GameNetworkingSockets 封裝
├── gui/          # 以 ImGui 實作的登入／大廳介面
├── components/   # 攝影機、HUD、記分板、生命值、mesh renderer
├── assets/       # 著色器、模型、材質、音效
└── main.cpp      # 進入點與主迴圈
```

遊戲以單執行緒主迴圈驅動一套場景系統（`Login → Lobby → Game`）。單例 `NetworkManager` 將封包送入當前場景，而 `GameWorld` 負責掌管遊戲邏輯模擬。墨水同時以兩種形式儲存：GPU 渲染目標（負責視覺呈現）與 100×100 的 CPU 網格（負責游泳判定與計分）。

## How the Ink System Works

每個可塗色的表面都有一個 `SplatMap`，內含兩種資料表示：

1. **GPU 材質／FBO** — 墨水筆刷渲染於此，即為畫面上看到的塗色效果。
2. **CPU 網格** — 記錄每個格子的歸屬，用於游泳判定與計分。

當拋射物落地時，其世界座標會被轉換為 UV 座標，筆刷材質被印入 FBO，同時更新歸屬網格——讓視覺呈現與遊戲邏輯保持一致。

## Documentation

- [`spec.md`](spec.md) — 以實作為本的軟體設計文件（場景、網路、遊戲邏輯、墨水系統）。
- [`architecture.md`](architecture.md) — 關於 `GameWorld` 職責拆分與重構方向的筆記。
- [DeepWiki](https://deepwiki.com/SoWiEee/Tiny-Splatoon) — 自動生成、可瀏覽的程式碼庫總覽。

> [!NOTE]
> `spec.md` 記錄的是專案「當前實作」的樣貌，包含已知落差——例如部分已宣告的封包型別尚未使用、AI 敵人已存在於程式碼中但目前停用。它是當前行為的權威依據。
