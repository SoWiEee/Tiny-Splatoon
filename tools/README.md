# 工具導覽

這裡的腳本分為來源匯出、來源執行環境及原生工具。`export-*` 仍由 CTest／oracle 或已提交原生資產使用；來源尚未退役前需要保留。相互依賴的匯出器維持目前命令路徑。

| 類別 | 入口與輸出 |
| --- | --- |
| 遊戲／AI fixtures | `export-arsenal`、`export-controller`、`export-player`、[LOD 固定 fixture](export-character-lod-fixtures.mjs)、`export-*-source-fixtures`、`export-ai-*` → `src/game/`、`tests/fixtures/` |
| 角色／動畫／Boss | `export-character-*`、`export-boss-*`、[來源頒獎台](export-showcase-drums.mjs) → 原生角色資料、材質與姿勢 fixtures |
| 場景／渲染 | `export-stage-*`、`export-environment-*`、`export-post-shaders` → `assets/reference/` |
| 武器／道具／HUD | [裝置](export-device-visuals.mjs)、[投射物](export-projectile-visuals.mjs)、[世界粒子](export-worldfx-particle-shaders.mjs)、[武器圖示](export-hud-icons.mjs)、[副武器／特殊圖示](export-hud-equipment-icons.mjs)、[選單分類／返回圖示](export-menu-source-icons.mjs) |
| 音訊 | [來源音效](export-audio-source.mjs)、[混響 fixture](export-native-reverb-fixture.mjs) |
| 來源載入 helpers | `reference-loader.mjs`、`device-reference-loader.mjs`、`reference-browser-session.mjs`；供匯出器共用 |
| 原生工具 | [匯入自訂音樂](import-music.mjs)、[七圖效能量測](Measure-ReferenceTurf.ps1) |

匯出器的 `--check` 用於檢查固定來源與已提交資料，普通遊玩不需要執行。共用瀏覽器 helper 會在關閉時清除本次建立的暫存 profile。

[建置與驗證指引](../docs/development.md) · [測試導覽](../tests/README.md)
