# 原生區域爭奪（Zones）

本輪接入離線 Zones；介面使用自訂譯名「區域爭奪」。主要對照 `splat-ref/src/game/zones.js`、`world/zones-data.js`、`game/bots.js`、`fx/zoneMarks.js` 與 `game/match.js`，來源檔案未修改。這不包括原版的多人 follower／事件重播協定。

## 規則與資料

七張離線場地使用已烘焙的 `zones` 幾何變體。`ZoneData.cpp` 直接匯出來源多邊形的小數座標，不在 C++ 重算 JavaScript `toFixed`。中央有一或兩個區域，側區依來源定義作 180 度鏡射；格子遮罩只採計高度範圍內、非遮蔽的可著墨地板格。

五分鐘對局、兩隊各 100 計數。每 0.2 秒取樣著墨比例，80% 捕獲、40% 中和，雙區中央必須全數控制才倒數。中央每秒 1 點、己方側區每秒 0.5 點、敵方側區每秒 2 點；懲罰先消耗但不算比分。失去給對手時依來源持有起訖與追平截點計算懲罰。實作 30–60 秒輪替、終局前最低 10 秒停留、最後 30 秒中央鎖定、落後方延長機會、平手驟死、10 秒失控寬限與延長上限。

捕獲以 0.6 秒 ease-out 前沿填墨，輪替以 0.7 秒線性前沿清墨；CPU 每個模擬 tick 推進，正式 GPU 墨水圖集使用來源 flood shader。這些操作不給玩家塗地或特殊量表。玩家在有效區域上方，或瞄準點落在其中時，實際新著墨面積另外累計為區域塗地。結算接到已存在的 Zones XP 公式與一次性保存流程。

## Bot 與呈現

整場共用 `ZonePlan`，維持角色分區、塗墨需求、看守／警戒／前推、敵方控制時的集結反攻、特殊技能協同、副武器目標與隊友跳躍。一般塗地／Boss 仍使用原有路徑。

簡約繁中 HUD 顯示計數、獨立懲罰、目前有效區域、著墨比例、輪替提示、最後中央、延長與結果原因。小地圖標示有效區域輪廓，切換模式會重建地形快取。場地輪廓、內側光暈、光幕、遠距加粗、佔領閃光、填墨波峰與受爭奪脈動使用來源匯出的單一 mesh／shader；動畫由原生狀態驅動。

音訊接上控制／失控、懲罰、輪替、最後中央、延長、警告、最後十點與定位填墨／清墨。每個有效區域有獨立定位 hum，原生連續合成依 neutral／ours／theirs 及 tension 調整音程、濾波、脈動與音色，避免固定 PCM loop 無法反映狀態。仍需人工聽感及 Web Audio 混響／空間化差異驗收，沒有宣稱逐樣本相同。

## 可重現驗證

- `native_zone_control`：七圖 cells／center／reach 對照固定來源 fixture；捕獲／中和、雙區、無玩家信用的 flood／wipe、懲罰、輪替／最後中央、時間到／延長／KO、暫停與重開。
- `native_zone_ai`：角色、較弱區域的集結反攻／被擊退重組，以及出生點出發、登船、在捕獲前塗到有效區域的固定 seed 情境。
- `native_zone_matches`：七圖正常難度完整對局，加潮汐廣場簡單／困難難度，要求 Bot 實際捕獲與區域塗地並完成結算。
- `native_zone_visual_gpu`：實際來源 mesh／shader 繪製、正式墨水圖集填墨／清墨回讀、重開清理與 GL 錯誤檢查。
- `native_zone_hum` 與既有音訊測試：連續模式／張力、分塊回呼一致性、有限輸出及 miniaudio 啟停。
- `reference_zone_data`／`reference_zone_fixtures`／`reference_zone_visual_export`：由鎖定來源重建固定資料，並檢查過期資產。一般原生建置及上述 native 測試不必讀取 JavaScript。

這些證據不取代七圖的完整人工視覺／聲音矩陣、跨 GPU 與最壞視效負載驗收。全專案仍有網路、部分最終美術與來源退役測試待完成，不能因此刪除 `splat-ref/`。

Final v0.7.3 integration acceptance: MSVC Release all-target build passed; all 81 CTest tests passed in 319.20 seconds, including nine complete fixed-60-Hz offline Zones matches.
