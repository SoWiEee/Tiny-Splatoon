# Tiny Splatoon v0.8.7

## 更新內容

- 加入 Linux x64 支援與 Ubuntu 22.04 建置／發布流程，隨包附上獨立房間服務。Linux 玩家資料遵循 XDG_DATA_HOME，未設定時使用 ~/.local/share/Tiny-Splatoon/profile.json。
- Linux 入口以實際執行檔位置尋找 assets，可從其他工作目錄或 PATH 啟動；本機房間服務改用 POSIX 程序啟動。修正 GCC LOD 引數、fixture include 與 Boss Float32 粒子精度，保留來源姿勢。
- 繁中換成華康新綜藝體，英文與數字換成 BlitzMain；保留 Noto Sans TC／Inter 作為玩家姓名等字元的後備字體，修正 UTF-8 字體檔名部署。
- 選單、裝備卡片、設定列表、結算與 HUD 加強字級層級、間距、深墨描邊及橘藍互動狀態；保留暖紙色與既有減少動態設定。
- 撤回本輪尚未提交的實驗墨片、水束與體積墨水改動，回到既有投射物與 RT 墨面；實驗備份不隨遊戲發布。
- 收入 v0.8.6 發布後完成的 Boss、UI 與潛水驗收修正。

## 安裝

Windows：完整解壓 Tiny-Splatoon-v0.8.7-windows-x64.zip，執行 Tiny-Splatoon.exe。

Linux：解壓 Tiny-Splatoon-v0.8.7-linux-x64.tar.gz，執行 ./Tiny-Splatoon-linux-x64/Tiny-Splatoon。目標為 Ubuntu 22.04 或更新的 x64 系統，需安裝系統圖形／音訊執行庫與支援 OpenGL 4.5 core 的 GPU 驅動，詳見 README 與開發指引。保留 lib 與 assets 目錄。

玩家資料格式維持相容；Windows 仍使用 %LOCALAPPDATA%/Tiny-Splatoon/profile.json。一般遊玩不需要 splat-ref/。

## 驗收界線

Windows UI／音訊／Boss 與受影響回歸通過；Linux Mesa UI／正式入口五項通過，Boss 16 情境及已部署素材的完整音訊通過。五項來源重匯出因缺 Edge 未完成，Windows 掛載來源的 MP3 非同步 seek 測試仍有時限差異；一般執行不需要這些來源工具。詳細結果記錄於 docs/restoration-progress.md。Linux 以 Ubuntu 22.04／WSL 與軟體 Mesa 執行建置與啟動測試；實體 Linux NVIDIA／AMD／Intel 的效能、手把及跨網多人矩陣未完整驗收。此次更新不宣稱墨水視效已與商業版完全一致。
