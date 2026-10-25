# 玩法與玩家流程契約

本契約描述目前原生版本的可玩流程與玩法規則。接受證據及仍待驗收項目集中記錄於[還原進度](../restoration-progress.md)。

## 模擬與內容

- 遊戲以固定 60 Hz 更新角色、AI、武器、傷害、裝置、特殊技能、計分與模式時計。`PlayerSimulation` 管理角色；`CombatWorld` 唯一管理投射物、裝置及世界特殊物件。畫面與音效只消費模擬狀態或事件，不重算傷害、塗地或 XP。
- 角色有 100 HP；敵方墨水每秒造成 20 傷，累積上限 40 且不會致死。離開敵墨 1.3 秒後回復生命，乾地每秒 22、己墨潛行每秒 60。直接命中目標不會再承受同一爆炸的爆風傷害。
- CPU ownership grid 負責可著墨面、隊伍歸屬、計分、區域控制及墨量統計。GPU 墨圖集提供連續墨面視覺；化妝性飛沫不改變格子歸屬或得分。
- 目前登記 12 種主武器（`shooter`、`twins`、`brolly`、`blaster`、`spinner`、`charger`、`bow`、`roller`、`brush`、`blade`、`mitts`、`bucket`）、15 種副武器（`bomb`、`sticky`、`burst`、`shaker`、`seeker`、`waddle`、`torpedo`、`tracer`、`boomerang`、`scan`、`curtain`、`sprinkler`、`mine`、`beacon`、`mist`）與 19 種特殊技能（`slam`、`storm`、`barrage`、`barrage_sticky`、`barrage_burst`、`barrage_seeker`、`barrage_mist`、`bubbler`、`sonar`、`strike`、`zooka`、`wail`、`kraken`、`blower`、`jetpack`、`stamp`、`booyah`、`zipcaster`、`crab`）。

## 模式、地圖與 AI

| 流程 | 目前實作 |
| --- | --- |
| Turf | 離線 4v4 塗地、Bot 對戰、結果與重開。練習沿用場地及武器，但不產生 Bot、時限或對戰成績。 |
| 區域爭奪 | 七張離線場地；五分鐘、兩隊各 100 點。區域塗色達 80% 控制，低於 40% 中立；中央每秒 1 點、己側 0.5 點、敵側 2 點。輪替為 30–60 秒，最後 30 秒與延長賽鎖定中央；懲罰先扣點且不計入比分，延長與驟死依來源時計。 |
| Boss | 最多八人協力，支援來源三階段、六種招式、弱點與專用結算。Boss 難度及地圖依選單合法組合提供。 |
| 線上房間 | GNS 私人房碼、八人房間與三模式對戰；服務、權責及限制見[多人契約](network.md)。 |

八張地圖為 Tidewater Plaza、Kelpline Terminal、Halyard Marina、Saltpan Basin、Crossroads Market、Lockgate Canals、Terrace Heights 與 Cargo Terminal。前七張用於離線 Turf、區域爭奪及 Boss；Cargo Terminal 僅供線上真人房間，禁用 Bot 與 Boss。實際可選組合由地圖模式設定決定。

離線 Bot 支援一般塗地、Zones 與 Boss 的已接入行為；線上由房主模擬 Bot。練習不加入隊友或敵人。Bot 不會主動選隊友進行超級跳躍。

裝置由 `CombatWorld` 部署並固定步更新，負責命中、爆炸與清理；特殊技能使用 actor 狀態或持續世界物件。視覺 renderer 讀取狀態快照，粒子路徑消費獨立事件。Kraken 由一般角色移動路徑處理，不擁有獨立替代角色控制器；其敵墨免疫與零傷害擊退由特殊技能狀態處理。

## 角色操作與生命週期

- 鍵鼠操作支援 WASD／方向鍵、滑鼠瞄準、左鍵主武器、右鍵或 E 副武器、Shift 形態／游泳、Space 跳躍、F 或 Q 特殊技能、C 歡呼、按住 Tab 或 M 開地圖，以及 Esc 暫停。
- Shift 僅能在己方墨面潛水，並可在己墨牆面攀爬；乾地或敵墨不會因 Shift 轉為魷魚。離開己墨牆、起跳與強制彈射依目前角色狀態處理。
- Tab／M 地圖提供隊友、基地、訊標選取與彈射預排。地圖與小地圖只顯示己方資訊及己方追蹤中的敵人；陣亡時可預選復活落點。
- 練習可清除墨水、補充墨量與特殊量表、返回出生點、切換場地及開啟裝備選擇。對戰包含暫停、離場確認、結果、再戰或返回。
- 角色置物櫃提供外觀、髮型、臉部、服裝及 3D 預覽；裝備頁提供主／副／特殊配置。介面使用華康新綜藝體呈現繁中、BlitzMain 呈現英數，缺字由備援字體補足。
- HUD 顯示隊伍名單、武器、存活／復活、特殊技能狀態、武器準星、擊倒訊息、Zones 計分與局內提示。敵方位置只在己方追蹤期間顯示；隊伍塗地比例在結果揭曉，不顯示局內 HP 數字。
- 小地圖與彈射地圖提供地形、墨面、隊友、基地、訊標、合法落點及預選提示；死亡標記保留五秒並於最後 1.3 秒淡出，對局重開時清除。

## 玩家資料

設定檔位於 Windows `%LOCALAPPDATA%/Tiny-Splatoon/profile.json`；Linux 使用絕對路徑 `XDG_DATA_HOME/Tiny-Splatoon/profile.json`，否則使用 `$HOME/.local/share/Tiny-Splatoon/profile.json`。檔案包含玩家名稱、裝備、外觀、等級、XP、勝場、場次、累積塗地及支援的設定。缺檔或無效 JSON 使用預設；舊武器別名轉為現行 ID；寫入先使用暫存檔再原子替換，替換失敗保留舊檔。完整離線對戰只結算一次；練習與中途離場不結算 XP。

畫質、音量、視角、色盤、靈敏度及其他設定依 UI 所提供的欄位保存。玩家流程、輸入與保存的驗收範圍見[還原進度](../restoration-progress.md)。
