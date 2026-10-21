# Tiny Splatoon v0.7.5

OpenGL 原生還原開發版，改善潛水墨面效果、Boss 音訊、HUD 可讀性，並移除舊診斷介面。

- 正式地形 shader 接上來源游泳資料：墨面隆起、波峰與持續尾流；分開處理魷魚變形、入墨／出墨及游泳水花。
- Boss 匯出全部 20 個命名 socket，聲源跟隨同一次固定步動畫姿勢；原有十個戰鬥碰撞點保持原樣。
- 補上部分 Boss 開場、預警與連續招式的來源音效延遲；結果 XP 顯示加入計數動畫與 65 ms 音效節奏。
- Zones HUD 更緊湊，放大 HUD／選單／結果字級；小地圖移除開發座標，保留隊伍與區域資訊。
- 移除舊 ImGui 練習診斷頁、stage lab、專用腳本與套件依賴；正式介面統一使用 RmlUi。
- README 簡化，加入技術 badges、Old Demo 與三張目前版本的真實遊戲截圖。

## Windows x64

完整解壓 `Tiny-Splatoon-v0.7.5-windows-x64.zip`，執行 `Tiny-Splatoon.exe`；請保留同目錄 DLL 與 assets。需要支援 OpenGL 4.5 的 GPU／驅動。

玩家保存位於 `%LOCALAPPDATA%/Tiny-Splatoon/profile.json`，新版不會覆蓋該路徑。GTX 1650 可先使用低畫質；中／高畫質的完整效能矩陣尚未驗收。

## 驗收範圍

MSVC Release 全目標建置成功；最終整合補驗 12/12 通過（32.16 秒），正式墨面尾流 GPU 檢查與 720p／1080p UI 檢查通過。Boss 嚴格姿勢 16/16、World FX 十個來源配方比對通過。

本版驗收結果見 [本輪紀錄](docs/acceptance-2026-10-22.md)，逐項狀態見 [還原進度](docs/restoration-progress.md)。元件 oracle、GPU 可見性與 UI 回歸不代表全遊戲像素或主觀音訊一致。

線上多人、部分道具／特殊技能與 Boss 次級視效、來源 paint-ripple 回呼、完整音訊空間化／混響及跨 GPU 最壞負載仍待完成。參考來源尚未退役；此版沒有刪除 splat-ref。
