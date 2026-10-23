# Tiny Splatoon v0.8.0

## 新增與調整

- 原生 GameNetworkingSockets 私人房間：房碼、八人名單、分隊與準備、換主、遊戲同步及返回同房。
- 更新選單視覺與操作回饋，沿用來源烏賊／武器圖示；設定頁可捲動，返回列固定可見。
- 戰鬥上方顯示隊伍存活狀態、復活倒數與特殊就緒；準心右側新增動態墨管、副武器耗墨刻線與低墨提示。
- 選單 BGM 改為 LifeWillChange_Instrumental.mp3，對戰 BGM 改為 Splattack.mp3；依實際音檔長度循環，最後一分鐘及 Boss 換階段沿用同一串流。
- 十二種主武器專屬準心、實際散布與蓄力狀態；隊友頭頂名牌、武器、特殊就緒與離屏方向。
- 六類世界粒子改用來源 shader／mesh，補接槍口、尾跡、甩墨、閃避、充能與命中墨花。
- 補齊普通炸彈與插地箭倒數範圍、Shaker 動畫噴口／充能燈／泡沫、迴旋鏢滴墨、傘破裂及特殊武器粒子事件；波刃停止後短暫消退。
- README 更新三張原生遊戲截圖，並附 HUD 差異與傷害來源核對文件。

- 文件依用途歸檔、測試 fixture 獨立分層，補上導覽並修正來源瀏覽器暫存清理。

## 安裝與驗收

完整解壓 Tiny-Splatoon-v0.8.0-windows-x64.zip，執行 Tiny-Splatoon.exe；請保留同目錄 DLL 與 assets。需要支援 OpenGL 4.5 的 GPU／驅動；多人房間服務 Tiny-Splatoon-Server.exe 隨包附上。

玩家資料仍位於 %LOCALAPPDATA%/Tiny-Splatoon/profile.json。畫質與音量可在選單設定。

驗收範圍與已知差異見 [本輪紀錄](docs/acceptance/acceptance-2026-10-23.md)、[HUD 對照](docs/audits/hud-source-audit.md)、[傷害核對](docs/audits/damage-source-audit.md)及[還原進度](docs/restoration-progress.md)。本機多人驗證不代表跨網／NAT／公開服務已驗收；完整來源 HUD 演出與最壞負載仍待完成。
