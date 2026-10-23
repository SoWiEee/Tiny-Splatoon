# HUD 來源對照（v0.8.0）

本表以 `splat-ref/src/ui/hud.js`、`hud-boss.js`、`styles/hud.css` 與目前原生 `GameUi`／`ScreenEffectsController` 為依據。「已接入」不代表逐像素或所有動畫一致。

| 項目 | 原生狀態 | 尚存差異 |
| --- | --- | --- |
| 雙隊／八人協力名單、武器、存活、復活秒數、自己、特殊就緒 | ✅ 已接入實際 active 名單與線上玩家身分 | 缺來源復活環及陣亡／復活／就緒的完整徽章彈跳動畫 |
| 準心右側墨管 | ✅ 本輪接入直立墨管、耗墨刻線、低墨、滿墨閒置淡出；以原生 RmlUi geometry 畫出液面與管身 | 低墨失敗事件的額外 flash、相機橫向加速度及隨機泡泡的逐幀一致仍未宣稱通過 |
| 特殊技能／副武器顯示 | 部分：charge 數值、橫條、就緒提示已有 | 缺原版右上液態特殊球、副武器小圓章、gain pulse／active spin 與否決動畫 |
| 各武器準星 | ✅ 十二種可選主武器接入對應準心；散布依實際 FOV／畫面高度，蓄力、雙槍、傘 HP 等讀取玩家狀態 | 命中／擊倒標記與出生盾環仍待移植 |
| 塗地／Zones 計分與時計 | ✅ 時計、末分鐘／十秒色彩、塗地比例、Zones 計數／罰則／控制／輪替／延長已接入 | 塗地比例目前置中；缺來源右上個人塗地分數、+Np 浮動與完整時間演出 |
| 受傷／低血量／死亡與復活覆蓋 | ✅ 原生 ScreenEffectsController 已接入傷害墨跡、低血量及死亡／復活畫面效果 | 原版 HUD 的方向弧線與完整死亡卡未全量還原；死亡原因／兇手武器卡尚缺 |
| 擊殺、助攻與連殺通知 | 未全量接入 | 缺 kill feed、助攻卡、first／double／triple／wipeout／revenge 等 callout |
| 隊友／離屏箭頭、世界死亡標記 | ✅ 活隊友頭頂名牌、武器、特殊就緒、距離淡出及離屏箭頭；敵人僅在己方追蹤期間可見 | 世界／地圖死亡墨魚標記的完整生命週期仍待移植 |
| 小地圖與超級跳躍 | ✅ 地形／墨、合法標記、隊友／基地／訊標選點與死亡預排已接入 | 現為 2D 地圖，缺來源可互動 3D diorama 與完整 pins／轉場美術 |
| 開場、暫停、結果與 XP | 部分：流程、podium、模式統計、XP、再戰／回房／返回可用 | 缺完整隊伍登場、READY／GO 圖文、結果武器／獎章／完整來源欄位與演出 |
| Boss HUD | ✅ 生命條、階段、招式預告、協力名單與結果可用 | 缺完整 title card、傷害數字及來源模式專屬 callout 美術 |

後續優先順序：命中回饋 → 特殊球與副武器小圓章 → 死亡原因／擊殺通知 → 世界死亡標記與完整開場／結算。這些差異仍保留在還原範圍內，未以裝飾風格變更取代功能。

## 武器墨水 FX（獨立於 HUD 表）

來源 [`fxHooks.js`](../../splat-ref/src/fx/fxHooks.js#L228) 對低墨乾射、開火／命中、roller/bucket/brush 甩墨、charger 蓄力與光束、閃避、普通炸彈、Ink Tempest 有明確 FX hooks；武器／裝置矩陣與每個原生接線位置見 [`weapon-device-fx-audit.md`](weapon-device-fx-audit.md)。原生玩家武器準星已在另一個 HUD 工作項目處理，不能以準星存在推定 muzzle、projectile trail、擊中墨花或裝置 fuse 提示已完成。

本輪盤點限定 Arsenal 實際註冊的 12 main、15 sub、19 special。退役 upstream `splatling` 的 spinUp/spinFull 與 `slosher` 的 head-only sloshImpact 不列為 active gap；active bucket 使用一般 slosh trail，但不產生 head 標記。所有目視驗收仍須分別確認來源觸發器、位置／法線／隊伍色與生命週期；此表不是逐像素 parity 聲明。
