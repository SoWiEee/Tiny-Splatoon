# Tiny Splatoon：目前實作規格

本文整理原生版本目前已提供的產品流程與系統。按領域整理的實作細節見下列契約；整體驗收狀態與限制見[還原進度](restoration-progress.md)。

## 1. 執行環境與入口

遊戲以 C++17、OpenGL 4.5、GLFW、RmlUi 與 miniaudio 建置。建置與遊戲執行直接使用已提交的原生資產，不需 `splat-ref/`、瀏覽器或 Node.js；參考目錄供來源檢視及選擇性 oracle 測試。Windows 與 Ubuntu 22.04 x64 有原生建置與套件流程。

一般啟動進入原生標題／首頁。玩家可進入練習、Turf、區域爭奪、Boss、裝備、置物櫃、設定、操作說明、製作名單或線上房間。詳細工程命令見[開發指引](development.md)，玩家玩法見[玩法與玩家流程契約](contracts/gameplay-player-flow.md)。

## 2. 地圖與對戰內容

目前含八張場地：Tidewater Plaza、Kelpline Terminal、Halyard Marina、Saltpan Basin、Crossroads Market、Lockgate Canals、Terrace Heights 與 Cargo Terminal。前七張支援離線 Turf、區域爭奪與 Boss 的合法組合；Cargo Terminal 僅供線上真人房間，不使用 AI 或 Boss。

| 模式 | 目前流程 |
| --- | --- |
| Turf | 離線 4v4 塗地、Bot、比賽結果及重開。 |
| 區域爭奪 | 七圖離線規則，含控制、懲罰、輪替、延長與驟死。 |
| Boss | 最多八人協力，含三階段、六種招式、Boss 結果與隊伍統計。 |
| 練習 | 單人、無敵隊伍與倒數，提供清墨、補充、回出生點、換圖與換裝。 |
| 線上 | GNS 房碼房間、八人名單、3D 大廳演出與三模式同步。 |

## 3. 角色、裝備與玩家資料

原生登記 12 種主武器、15 種副武器及 19 種特殊技能。固定 60 Hz 模擬處理角色、AI、武器、墨水、碰撞、裝置、特殊技能及模式裁判。角色可換裝與外觀；結果記錄模式資料及 XP。線上權責與同步限制見[多人連線契約](contracts/network.md)。

支援鍵鼠及 GLFW gamepad mapping。Shift 在己墨下潛、在己墨牆攀爬；乾地與敵墨不會因 Shift 變形。Tab／M 開啟原生 3D 地圖及彈射選擇；Tab 地圖、小地圖與敵人追蹤遵守可見性規則。

玩家名稱、裝備、外觀、進度與已支援設定保存於作業系統使用者資料目錄。設定檔以暫存檔及原子替換保護舊資料；完整離線對戰只結算一次，練習及中途離場不結算 XP。保存目錄與欄位見[玩法與玩家流程契約](contracts/gameplay-player-flow.md)。

## 4. 場景、墨水與畫面

場景載入來源幾何、PropKit、貼圖、天空／海面、動態遠景與原生 HUD。CPU ownership grid 管理遊戲計分及隊伍歸屬；GPU 連續墨圖集負責濕墨外觀。渲染管線提供 HDR、方向陰影、GTAO、bloom、色彩分級、Screen FX、品質與反鋸齒選項。

投射物、裝置、世界粒子、特殊技能及 Boss 視覺各依模擬狀態呈現。畫面不重算傷害與塗地；場景與各項渲染系統見[場景與渲染契約](contracts/rendering-stage-environment.md)、[戰鬥視覺契約](contracts/combat-visuals.md)及[Boss 契約](contracts/boss.md)。

## 5. 音訊、房間與同步

音效、loop、配樂及事件由原生 miniaudio 引擎播放，細節見[音訊契約](contracts/audio.md)。線上透過獨立 GNS 服務使用相同服務位址與私人房碼；direct-IP 不含公開房間搜尋、NAT 穿透或帳號服務，連線與部署細節見[多人連線契約](contracts/network.md)。
