# Tiny-Splatoon — OpenGL 完整還原工程規格

版本：2.0 · 基準日期：2026-10-17 · 狀態：開發與驗收目標，尚未完成還原

本文件取代原先記錄 C++ 現況的 SDD。目標為使用原生 C++ / OpenGL，完整還原本機 `splat-ref/` 的 **INKWAVE: Turf Riot**。後續設計、實作、測試及完成判定均須對齊本文件與鎖定的參考來源；現有 Tiny-Splatoon 的功能範圍不構成降低目標的理由。

## 1. 範圍、權威與完成定義

### 1.1 產品範圍

- 原生 Windows 桌面遊戲，沿用 C++17、OpenGL 4.5、GLFW、GLAD、GLM 與 CMake / vcpkg 基礎。
- 全部正式玩家流程：離線對戰、練習、線上房間、三種模式、全部正式地圖、主／副／特殊武器、角色外觀、介面、音效、進度與設定。
- 世界、角色、墨水、FX 與後製由 OpenGL 實作；不得以嵌入網頁、Electron／Three.js 執行期或影片替代。
- 可重用既有引擎／依賴，內部模組與演算法組織不必逐行翻譯；可觀察行為、內容與呈現必須一致。
- repository 仍稱 Tiny-Splatoon；參考版名稱、文案與美術為畫面還原依據。重新命名或改美術應另行記錄，不能默默混入移植。

### 1.2 「100% 還原」的五個軸

| 軸 | 必須一致 | 不能視為完成 |
|---|---|---|
| 功能 | 所有正式入口、模式、內容及成功／失敗／返回流程 | 一張地圖的射擊 demo |
| 規則 | 調校數值、碰撞、狀態、算分、輸入時序、AI、網路權責 | 使用舊 C++ 玩法或任意簡化 |
| 視覺 | 地圖、角色、材質、墨、光影、相機、動畫、HUD 與轉場 | 方塊角色、平板地圖、預設 debug UI |
| 音訊 | 配樂段落、音色、事件、空間、混音及提示時序 | 隨意替代歌曲或僅有射擊聲 |
| 資料／流程 | 設定、外觀、裝備、進度保存及房間生命周期 | 重啟遺失、不能重賽、結束踢回登入 |

100% 代表全部強制需求與內容矩陣有通過證據，不代表不同 GPU 逐像素完全一致。邏輯／內容不能以視覺容差刪減；視覺、音訊必須固定條件比對。任何缺項仍只能稱階段成果。

### 1.3 來源優先序與基準

1. 本文件中明示並經使用者接受的產品決策。
2. 鎖定版本 `splat-ref/` 的正式程式碼及實際執行行為。
3. 參考版 `docs/` 與 README；若與程式衝突採程式。
4. 現有 C++ 僅為可重用實作，不覆寫目標。

本次盤點所在根 repository HEAD：`607c6e037dbace1c8177b576e2101bb06c39e353`。這不是參考目錄的獨立版本證明；完整檔案與 SHA-256 基準見 [reference-baseline.sha256](docs/reference-baseline.sha256)。必須保留可取得的參考原檔，hash 不能還原內容。未經記錄不自動跟隨參考版更新。

| 關鍵檔 | SHA-256 |
|---|---|
| `splat-ref/src/config.js` | `76A1BC70F855E58343A1BC1D7A41C6B72DCD2DA55B3E4C47B5A5908756180999` |
| `splat-ref/src/main.js` | `84D4CBFB31178F65A6E24D4ABAE5AB1158DD4F609D276C793A17C79589A451B9` |
| `splat-ref/src/world/maps.js` | `B6A50805C67792EE54B285A09A87387F87B8AF3FA66FD6709AE1600D18C21813` |
| `splat-ref/src/game/match.js` | `92A2716417F4B29B2A87EF1A3BEAA3FA3F5CA35EFFE66F80647A58A25020FCF4` |

本文數字為工程重點摘要。鎖定 `config.js` 的所有正式資料欄位，以及 kit、相機、AI、Boss、材質／動畫模組的有效調校值均屬規範；未逐項抄錄仍須一致。**名稱、數量相同不等於玩法已還原。**

### 1.4 原生等效與排除範圍

| 原平台能力 | 原生要求 |
|---|---|
| WebGL / Three.js | OpenGL VAO/VBO、GLSL、FBO、skinning、instancing 與原生後製 |
| DOM / CSS / SVG | 等效字體、版型、圖案、互動、焦點與動畫；可用 ImGui 作繪製基礎 |
| pointer lock | GLFW 游標捕捉／raw mouse motion，選單與失焦行為等效 |
| localStorage | 本機持久化，保留資料欄位、預設及遷移語意 |
| WebAudio | 原生合成或由同參數生成音訊，保留聲音與空間混音 |
| WebSocket relay | 支援房間契約的原生傳輸；協定互通或替代方案需明示與測試 |
| Gamepad／touch | 標準手把／震動及 Windows 觸控裝置等效操作 |
| 全螢幕／Electron shell | 原生視窗／全螢幕、DPI 與 resize |

不要求複製瀏覽器權限、HTTPS 限制、網址列、macOS 安裝包／專屬快捷鍵、Three.js vendor 結構或 npm 工具 UI。但不能因此移除手把、觸控或正式線上功能。`tools/` 實驗室與 debug URL 不算正式玩家入口；其固定步進、捕捉與連線測試能力需有原生等效手段。

## 2. 共同工程契約

來源：`splat-ref/docs/CONTRACTS.md`、`docs/EVENTS.md`、`src/core/ctx.js`、`src/main.js`。

| ID | 強制要求 |
|---|---|
| ENG-01 | 世界單位為公尺／秒／弧度，Y 向上，角色原點腳底，yaw=0 面向 +Z；與既有 GLM 角度介面在明確邊界轉換。 |
| ENG-02 | 調校資料唯一來源，穩定裝備／地圖 ID、外觀索引與存檔欄位；不能在不同系統各抄一份數值。 |
| ENG-03 | 分離模擬、渲染、網路播放時間，30/60/120 Hz 不改變速度、射速、傷害或時計。 |
| ENG-04 | 遊戲事件驅動 HUD／FX／音效；事件不重複算傷害、墨、特殊或 XP。 |
| ENG-05 | 場景、GPU、音效、網路可清理及重入；重賽／切圖不殘留 actor、墨、裝置、監聽、loop 或按鍵。 |
| ENG-06 | shader／資源／裝置／連線失敗有可恢復錯誤；不能靜默降成 stub、假結果或空白畫面當作通過。 |

目前原生程式集中於 `restoration/`；模組拆分依耦合決定，不要求重寫已驗證的模擬與渲染。

2026-10-19 架構決策：若既有主程式架構不適合來源契約，可在 `restoration/` 完成原生 application、輸入、場景與資源生命週期，再替換舊入口。玩家入口與驗收工具共用同一套 actor／runner／world／match／pose，不維護第二套玩法。每個視窗只建立一個 GLFW／OpenGL／ImGui 生命週期，GPU 資源須在 context 結束前釋放。舊檔案於呼叫與建置依賴移除、替代流程可運作且受影響驗收通過後才刪除；目錄重組與入口切換不改變本規格的完整還原要求。

2026-10-20 使用者決策：移除舊 OpenGL 程式，僅保留最新 `restoration/`；普通啟動直接進入原生選圖。舊場景／網路／音效／renderer 與未使用資產、依賴一併移除；NVIDIA 選卡提示保留於 restoration；GameNetworkingSockets 套件宣告依使用者補充保留供未來連線接入，目前不連結／初始化。此決策取代保留舊入口的遷移安排；完整 UI／音效／連線／模式等需求仍須依本規格開發，不能因舊碼刪除而標完成。

## 3. 啟動、選單與完整流程

來源：`splat-ref/src/main.js`、`src/ui/{menus,menu-art}.js`、`src/game/{showcase,lobbySet}.js`、`styles/`。

| ID | 需求與驗收條件 |
|---|---|
| FLOW-01 | 載入進度／項目→標題→主選單，3D 背景／attract 持續運作；無需先開伺服器才能遊玩。 |
| FLOW-02 | 主選單提供 Play、Online、Loadout、Locker、Settings、How to、Credits，所有入口與返回可完成。 |
| FLOW-03 | Play 選模式、合法地圖、day/dusk、難度及模式適用時長；縮圖、說明、預覽與限制一致。 |
| FLOW-04 | Loadout 選主／副／特殊、看能力條與 3D 持槍角色；右側展示空間、換裝反應與音效一致。 |
| FLOW-05 | Locker 分 presets、hair/headgear、face、outfit，3D 即時預覽並保存，進房／對戰外觀一致。 |
| FLOW-06 | 正常生命周期 `init → intro → playing → finish → judge → results`；塗地 intro 約 4.2 s、finish 約 2.6 s，精確時序採來源。 |
| FLOW-07 | Esc 暫停／恢復、設定、教學、離開確認；離線凍結模擬／時計，線上選單不凍結其他玩家。 |
| FLOW-08 | 失焦／游標釋放依參考暫停路徑；恢復不跳視角、不殘留按鍵、不點穿射擊，judge/results 不疊戰鬥暫停。 |
| FLOW-09 | Practice 單人、無隊友／敵人／倒數；L 開換裝、清墨重設、回生點／滿墨／滿特殊、換場及返回。 |
| FLOW-10 | 結果含勝敗演出、隊伍 podium、統計、XP、重賽／返回；線上回原房間保留成員設定。 |

## 4. 輸入、相機、移動與物理

來源：`splat-ref/src/core/{input,touch}.js`、`src/game/{player,actor,physics,cameraRig}.js`、`src/config.js PLAYER`。

### 4.1 操作表

| 動作 | 鍵鼠基準 |
|---|---|
| 移動／視角 | WASD／方向鍵，相對相機；滑鼠瞄準 |
| 主武器 | 左鍵，依武器按住／放開／蓄力 |
| squid／游泳／攀牆 | Shift，依表面與狀態改變 |
| 跳躍／特定武器閃避或蓄力跳 | Space，區別按下沿與持有 |
| 副武器 | 右鍵／E，持有預覽、釋放或放置依種類 |
| 特殊 | F／Q，再依種類接射擊／副武器／地圖 |
| Yeah! 加油 | C，與 Cheer Orb 及隊友充能連動 |
| 展開地圖 | 按住 Tab／M；控制地圖游標，不轉戰鬥相機 |
| Super Jump | 地圖 1–3 隊友、4 基地、5–0 信標，或點 pin |
| 暫停 | Esc |

| ID | 強制要求 |
|---|---|
| INPUT-01 | 滑鼠預設 raw 1:1、無額外平滑／加速度，靈敏度／invert-Y 可調，短按／放開不丟失。 |
| INPUT-02 | 標準手把左／右搖桿、RT 射擊、LT squid、A 跳、RB 副、Y 特殊、View 地圖、Start 暫停及選單導航；其他依來源。dead zone、response curve、edge boost、震動與裝置熱切換一致。 |
| INPUT-03 | 手把／觸控預設 aim assist：摩擦、追蹤與條件彈道吸附，非瞬間鎖定；滑鼠預設關閉、可選開啟。 |
| INPUT-04 | 觸控移動／拖視角、射擊、跳、squid、副、特殊、地圖、加油、暫停，多點同時操作、橫向縮放及獨立靈敏度／invert-Y。 |
| CAM-01 | 肩後第三人稱、彈簧跟隨、遮擋探針／防穿牆，準星瞄點與實際出槍一致。 |
| CAM-02 | intro、死亡觀戰、地圖 diorama、pedestal／locker、results podium、lobby 相機與平滑轉場。 |
| CAM-03 | 水平 FOV 以 16:9 為基準，預設 82°、選單 65–100°；不能誤當垂直 FOV。震動／後座受設定限制。 |

### 4.2 物理與形態

| ID | 需求 | 基準摘要 |
|---|---|---|
| MOVE-01 | 尺寸／形態 | radius 0.38 m、kid 高 1.45 m、squid 高 0.55 m |
| MOVE-02 | run／乾地 squid／己墨 swim／敵墨速度 | 6.0 / 2.9 / 11.8 / 1.9 m/s |
| MOVE-03 | 加減速／反向制動／轉向／空控／游泳慣性 | 全部 PLAYER 調校，不以單一 lerp 近似 |
| MOVE-04 | 跳／落／邊緣寬容／預輸入 | jumpVel 8.4、swimJumpVel 9.4、gravity 25；buffer 0.13 s、coyote 0.12 s；apex/fall 與重落恢復 |
| MOVE-05 | 地形控制 | stepUp 0.35、stepDown 0.45 m；斜坡、窄邊、天花板、roof 滑落、grate/rail 依資料 |
| MOVE-06 | 攀牆／越頂／脫離／浮出射擊 | 己墨牆可攀及側移；emergeDelay 0.07 s、fireBuffer 0.16 s；乾地／空中 squid 不等於 submerged |
| MOVE-07 | 水域與墜落 | waterY -1.6 m、fallDeathY -1.45 m；缺口沒有 ground，不補成實心底板 |
| MOVE-08 | 角色互推／spawn／死亡／復活／Super Jump | 玩家、bot、remote 共用規則，推擠權責依來源 |

驗收包含旋轉地形、牆角、上下坡、階梯、窄邊、頭頂、跳水、出墨及同幀浮出射擊，不能只測空場前進。

## 5. 戰鬥、墨槽與特殊充能

來源：`splat-ref/src/game/{actor,weapons,subs,specials}.js`、`src/game/kits/`、`src/config.js`。

| ID | 強制行為 |
|---|---|
| COMBAT-01 | HP=100；各武器距離／部位／裝甲／盾／無敵、穿透與友傷規則依來源，不統一成 10 傷害。 |
| COMBAT-02 | 敵墨 20 HP/s、敵墨傷害封頂 40、不單獨擊殺；regenDelay 1.3 s，普通／游泳回血 22／60 HP/s，條件與順序依 actor。 |
| COMBAT-03 | 墨槽 100，各武器耗墨、低墨／空槍、idle delay 0.9 s、swim/kid 回墨 42/9 每秒，不能與 HUD 更新重複回補。 |
| COMBAT-04 | 特殊依新取得 turf 面積充能，rate 0.8、各武器 specialCost；自墨重塗／特殊墨／cosmetic 飛沫不能重複刷分。 |
| COMBAT-05 | 特殊啟動滿墨，死亡取消並依 specialKeepOnSplat=0.5 保留充能，精確順序依來源。 |
| COMBAT-06 | 死亡停止操作／攻擊，記錄 splat/death/cause/attacker，5.5 s 復活、1.6 s 保護，上一生命攻擊清理。 |
| COMBAT-07 | swept projectile、ray、爆風、遮蔽、裝置破壞、同攻擊受害者去重與命中回饋一致，薄牆不能被跳過。 |

## 6. 完整裝備名冊

ID、順序、名稱、預設 kit、數值、動畫／音效／UI／網路均屬要求。所有主武器都可更換副／特殊。以下摘要不取代完整 config 與模組行為。

### 6.1 十二種正式主武器

| ID | 名稱 | 必須還原的核心玩法 | 預設副／特殊 |
|---|---|---|---|
| shooter | Spritzer | 連射、散布、航程後落下／沿途墨 | bomb / zooka |
| twins | Twinfire Pistols | 雙槍交替、射擊＋跳翻滾、兩次 roll、站樁快速精準射擊 | seeker / bubbler |
| brolly | Canopy Brolly | 霰彈、持有開傘、發射傘面、盾破壞／修復／再生 | tracer / bubbler |
| blaster | Popper Blaster | 慢射、定距空爆、直擊與爆風不同傷害 | burst / jetpack |
| spinner | Squall Spinner | 蓄力後連射、蓄力決定射程／持續時間 | curtain / crab |
| charger | Glint Charger | 蓄力放開、長線穿透、滿蓄致命與線墨 | mine / sonar |
| bow | Tideline Bow | 兩段蓄力、三箭、地面橫／空中縱、插地延時爆 | waddle / strike |
| roller | Swell Roller | 滾塗／碾壓、甩墨前搖與距離衰減 | sticky / kraken |
| brush | Swish Brush | 拖行高速細墨路、快速左右甩墨 | mist / stamp |
| blade | Brine Cutlass | 橫斬、蓄力縱斬／突進、近戰與飛行墨波 | shaker / zipcaster |
| mitts | Sponge Mitts | 墨拳、正面減傷、蓄力長跳／落地爆發／貼牆耗墨 | boomerang / slam |
| bucket | Bilge Bucket | 拋物線墨浪、越障、每次去重與高度傷害規則 | sprinkler / blower |

來源：`splat-ref/src/config.js WEAPONS/WEAPON_ORDER`、`src/game/weapons.js`、`src/game/kits/{brolly,bow,blade,mitts}.js` 及其模型／FX。

舊資料相容：`dualies→twins`、`splatling→spinner`、`slosher→bucket`，不增加退役武器選單。既有 C++ Brush／Slosher 不可只改名稱視為還原。代表校準：Spritzer 0.1 s/發、36 傷害；Charger 1.0 s 滿蓄／27 m／160 傷害；Bucket 55；Twins roll 3.4 m/0.3 s；全部其餘欄位仍須轉寫。

### 6.2 十五種副武器

| ID | 名稱 | 必須還原的行為 |
|---|---|---|
| bomb | Splat Bomb | 彈跳、落地起算引信、距離爆風 |
| sticky | Cling Charge | 黏地／牆／天花板、延時大爆 |
| burst | Pop Pellet | 撞擊爆、直擊／爆風、低耗墨 |
| shaker | Shaker Bomb | 搖動蓄力，移動／跳／搖晃加速，最多三次跳爆 |
| seeker | Skitter Bomb | 地面追敵／墨路、寬轉向、近距直衝 |
| waddle | Waddle Bomb | 落地感測／導航追蹤／跳階、無目標引爆、可射毀 |
| torpedo | Tide Torpedo | 飛行鎖定／展開／加速追蹤、可射毀／爆裂子彈、一枚限制 |
| tracer | Tracer Bolt | 低射高速反彈、落點墨、命中／軌跡標記 |
| boomerang | Whirl Boomerang | 射出／懸停傷害／返回環繞／結束爆、命中引爆、一枚限制 |
| scan | Echo Orb | 感測雲／全隊追蹤，無直接傷害 |
| curtain | Drip Curtain | 阻敵人／子彈的墨幕、HP／自然及受擊衰減 |
| sprinkler | Twirl Sprinkler | 黏任意表面、脈衝灑墨、摧毀／主人死亡清理 |
| mine | Lurk Mine | 腳下隱藏、感測／延時／追蹤、最多兩個 |
| beacon | Hop Beacon | 隊友 jump 點、可破壞、最多三個／各兩次使用 |
| mist | Murk Bomb | 毒霧、緩速／耗墨、直擊中毒 |

來源：`splat-ref/src/config.js SUBS/SUB_ORDER`、`src/game/subs.js`、`src/game/kits/{shaker,waddle,torpedo,tracer,boomerang}.js`。副武器從墨槽付費，不以場上拾取炸彈替代；持有／釋放／低墨／數量限制／盾／清理／線上重播逐項驗收。

### 6.3 十九種特殊武器

| ID | 名稱 | 必須還原的行為 |
|---|---|---|
| slam | Tidal Slam | 起跳／懸停／落地衝擊／距離傷害 |
| storm | Ink Tempest | 投雲／漂移／降墨／持續傷害 |
| barrage | Splat Bomb Barrage | 限時免費快速投 bomb，主武器仍可用 |
| barrage_sticky | Cling Charge Barrage | sticky 連投及獨立間隔 |
| barrage_burst | Pop Pellet Barrage | burst 連投及獨立間隔 |
| barrage_seeker | Skitter Bomb Barrage | seeker 連投及獨立間隔 |
| barrage_mist | Murk Bomb Barrage | mist 連投及獨立間隔 |
| bubbler | Bubble Guard | 傷害轉推力、接觸分享隊友 |
| sonar | Deep Sonar | 全隊揭露敵人、緩速／耗墨增加 |
| strike | Vortex Strike | 地圖選點／導彈／墨渦／持續傷害／牽引 |
| zooka | Twister Zooka | 限時遠距致命旋風，非舊版三發一般火箭 |
| wail | Howl Box | 持有瞄準／充能／穿牆持續聲波 |
| kraken | Kraken | 無敵巨型 squid、跨敵墨、跳躍攻擊 |
| blower | Bubble Blower | 最多三泡泡、漂移、友射引爆／敵方互動 |
| jetpack | Ink Jet | 懸浮／boost／爆裂射擊、標記起點／結束跳回 |
| stamp | Mega Stamp | 前壓連擊／正面防禦／清炸彈／空翻／投擲終止 |
| booyah | Cheer Orb | 舉球蓄力、隊友加油加速及回特殊、大爆 |
| zipcaster | Zipline | 副變鉤索、貼牆／撞擊、主武器可用／結束跳回 |
| crab | Crab Rig | 可破壞載具、主機槍／副砲／滾球、背與上方弱點 |

來源：`splat-ref/src/config.js SPECIALS/SPECIAL_ORDER`、`src/game/{specials,special-props,weapons}.js`。每項測啟動、作用、自然結束、死亡／比賽結束取消、輸入覆寫、物件清理及線上權責。

## 7. AI 與隊伍

來源：`splat-ref/src/game/{match,bots,nav}.js`、kit bot hooks。

| ID | 強制需求 |
|---|---|
| AI-01 | 一般離線玩家＋3 AI 對4 AI；Boss 玩家＋7 AI；Practice 僅玩家。姓名／外觀／武器組合依來源。 |
| AI-02 | paint/fight/refill/retreat、視線／距離／反應、瞄準誤差／預測、走位、適距、回墨／撤退／復活。隨機走與無條件射擊不通過。 |
| AI-03 | 各圖導航／斜坡／平台／水／落差／攀牆、游泳／跳與前線 jump，不長期卡牆或自殺。 |
| AI-04 | 武器及副／特殊策略、盾／裝置／追蹤威脅處理；Zone 爭當前區，Boss 閃預警／瞄弱點／清小蟹。 |
| AI-05 | Chill/Fresh/Fierce：反應 0.55/0.32/0.17 s，awareness 16/21/26 m；其他 aimError/fireDiscipline/aim spring 採 DIFFICULTY。 |
| AI-06 | 線上僅 owner 模擬 bot；玩家／bot 共用物理、耗墨、傷害及塗墨規則。 |

## 8. 模式、勝負與裁判

### 8.1 Turf War

來源：`splat-ref/src/config.js MATCH`、`src/game/match.js`、`src/world/paint.js`。

- MODE-T01：4v4；90/180 s、預設／上限180，最後一分鐘音樂提示、末十秒逐秒倒數。
- MODE-T02：`coverage()` 以全場有效 turf cell 為分母，未塗仍計，兩隊比例不必加總100%。牆可游不代表計分；表面依 face.turf 與有效性。
- MODE-T03：較高比例勝；完全平手按參考隨機決勝，線上由 host 一次決定並共享。
- MODE-T04：歸零進 finish，停止新有效攻擊／刷分，經 judge 動畫／數字揭示進 results。

### 8.2 Zone Control

來源：`splat-ref/src/config.js ZONES`、`src/world/{zones-data,variants}.js`、`src/game/zones.js`。

- MODE-Z01：300 s、兩隊 count=100；持有目標倒數，降至0 knockout。
- MODE-Z02：中央／側區30–60 s輪替，末30 s與 overtime 鎖中央；每圖多邊形／高度／模式地形變體一致。
- MODE-Z03：80%奪區、敵墨30%警告／40%中立化；雙中央須全控。5 Hz 採樣、奪區 flood、輪替 wipe 一致。
- MODE-Z04：中央1 point/s、己側0.5、敵側2；失控 penalty=`round(0.75×(start−end))`，start=100加1；先扣罰分再倒 count。
- MODE-Z05：未控方特殊補償4.5 point/s、中立落後方1.5，依實際規則分派。
- MODE-Z06：時間到落後隊仍控制或剛中立且10 s內則 overtime；同分 sudden death。逆轉、領先方奪回、10 s不控或300 s上限按來源判終。
- MODE-Z07：HUD／結果含 count、penalty、輪替、overtime 與 knockout/time/comeback/retake/neutralised/overtime-cap 原因；線上 host 裁定。

### 8.3 Boss Battle

來源：`splat-ref/src/boss/`、`src/ui/hud-boss.js`、`src/audio/bossAudio.js`、`src/game/match.js`。

- MODE-B01：至多8人 SQUAD 對 HULLBREAKER；預設240 s、選項180/240/300；intro7.2 s、勝／敗finish5.2/3.2 s。
- MODE-B02：HP=`round(18000×squadUnits×difficulty)`，真人1.0／bot0.6；easy/normal/hard HP倍率0.75/1/1.3、傷害0.7/1/1.2、節奏1.25/1/0.85。
- MODE-B03：66%／33%分三階段、轉階咆哮／短無敵；弱點×2.5、stun×1.25及各武器倍率採 BOSS 與命中實作，多球體／弱點優先判定。
- MODE-B04：六招 telegraph→action→recovery：Crusher Slam衝擊環、Container Barrage投桶、Ink Cannon Sweep、Hull Charge撞牆stun、二階Brood、三階Shell Frenzy。
- MODE-B05：適應合法地圖，不落水／穿牆／攻spawn安全區；全滅正常復活，不直接輸。HP0勝、逾時敗，結束清hazard／停止傷害。
- MODE-B06：巨大寄居蟹、貨櫃殼、大小鉗、眼柄、弱點、裂殼、腿IK、招式／死亡動畫；大方塊加HP條不可替代。
- MODE-B07：Boss HP刻度／傷害殘影、預警／OPEN!、SQUAD／時計、專屬結果／傷害／弱點命中／splats／turf與XP。
- MODE-B08：host模擬Boss／墨，玩家owner處理自身hazard傷害；boss hit、seed/time move、snapshot、換主與冪等重播一致。

## 9. 八張地圖與場景契約

來源：`splat-ref/src/world/{maps,mapkit,level,variants}.js`、`src/world/stages/` 及各專屬模組。

| ID | 名稱 | 辨識與玩法結構 | 可用範圍 |
|---|---|---|---|
| MAP-01 tidewater | Tidewater Plaza | 海濱廣場／Jubilee鐘塔／柱廊／promenade／loggia | 離線／線上、三模式 |
| MAP-02 kelpline | Kelpline Terminal | 斜轉碼頭／貨車道／貨櫃高地／吊車／船 | 離線／線上、三模式 |
| MAP-03 halyard | Halyard Marina | 浮碼頭／車渡輪／clubhouse／Fuel Dock／Long Pier／Boatyard | 離線／線上、三模式 |
| MAP-04 saltpan | Saltpan Basin | 下沉鹽田／棧道／風泵／棚／鹽堆／gallery | 離線／線上、三模式 |
| MAP-05 crossmarket | Crossroads Market | 斜交街／Market Hall／No.3 tram／巷道／gallery | 離線／線上、三模式 |
| MAP-06 lockgate | Lockgate Canals | 乾船閘／斜渠／橋／閘步道／倉庫 | 離線／線上、三模式 |
| MAP-07 terraces | Terrace Heights | 白牆山村／piazza／多層階梯露台／Villa高處spawn | 離線／線上、三模式 |
| MAP-08 cargo | Cargo Terminal | 獨立ID的貨櫃碼頭、旋轉幾何／場景內容 | 僅線上真人、禁AI／Boss |

| ID | 全地圖驗收要求 |
|---|---|
| GEO-01 | 各自bounds、spawnPads、尺度、方向、高度、水／路線，不一律壓成80×80。 |
| GEO-02 | single一次、half＋180°鏡像；盒、旋轉盒、斜坡、弧／八角分段、薄面均依資料。 |
| GEO-03 | 可走／roof／rail／grate／prop collider、角色與彈體阻擋、noPaint保留差異；不只用世界AABB近似旋轉地形。 |
| GEO-04 | 每面paintable/turf/ceiling、遮蔽剔除、材質方向／壁畫／字樣／道具／專屬dressing完整。 |
| GEO-05 | 每圖day/dusk，day映射day或golden、dusk映射sunset依config；mode variants不污染其他模式。 |
| GEO-06 | Cargo不出現在離線，強制bots off，至少兩人且兩邊有人；離線移除actor不補bot，Boss轉合法圖。 |

比對俯視輪廓、玩家視角、關鍵高度與spawn→中央→側路→對方spawn可達路徑；只像外形但玩法拓樸不同不通過。

## 10. 墨水、材質與計分

來源：`splat-ref/src/world/{paint,inkShading,levelMaterial}.js`、`src/fx/swimWake.js`。

| ID | 強制需求 |
|---|---|
| INK-01 | 可塗面各有face-space atlas，支援地／牆／坡／角及跨面splat，不只整場XZ投影。 |
| INK-02 | 每面CPU約0.25 m cell，GPU／CPU墨主體邊界同seed／演算法，sample／游泳／攀牆／zone／score一致。 |
| INK-03 | team share／wetness／tone／coverage語意，覆塗／union／交界，約6 s乾燥；內部可等效編碼。 |
| INK-04 | shot/line/blast/bomb/trail/drop/roll/speck、定向拉伸、展開／飛沫／牆滴／波紋／墨路接續。 |
| INK-05 | 主體外細飛沫／牆滴／cosmetic speck不計分或充能；僅有效turf cell算coverage。 |
| INK-06 | counts/turfTotal比例與新取得面積分開；覆敵墨正確扣加、自墨不重複取得、空場不NaN。 |
| INK-07 | zone flood/wipe、reset、minimap version、seeded網路重播與批次繪製一致；舊墨展開不蓋回新墨。 |
| INK-08 | 厚唇／濕光澤／粗糙度／微紋理／陰影保色；品質改atlas不改邏輯計分。 |

## 11. 角色、美術、環境與渲染

### 11.1 Squidkid

來源：`splat-ref/src/game/character*.js`、`src/game/kits/*-model.js`、`docs/RIG.md`。

- CHAR-01：程序幾何／骨架、頭眼眉／觸手髮與吸盤、衣褲鞋／背帶、透明墨罐可見液位、武器持握、squid；現有OBJ不能視為參考角色完成品。
- CHAR-02：kid/squid/swim/climb不同，約0.12 s變形、local水下可辨識／remote潛藏輪廓、貼牆朝向一致。
- CHAR-03：idle／跑／側移／制動／轉向、跳落／重落、pitch瞄準、各武器前搖／後座／蓄力／投擲、受擊／spawn／特殊／勝敗／menu/lobby；落腳IK／spring／secondary motion／LOD與切換。
- CHAR-04：8髮、9膚、10服裝、8眼、4帽、4眉、10 presets：Rookie/Dash/Pip/Coral/Marlo/Riptide/Nori/Suki/Kelp/Skipper。索引append-only，舊存檔外觀不能變。
- CHAR-05：身上敵墨、tank低墨、特殊滿條、出生保護閃爍、隊色／眼神／遠距辨識完整，幾何材質共享。

### 11.2 場景與渲染

來源：`splat-ref/src/core/renderer.js`、`src/world/{environment,texlib,levelMaterial,inkShading}.js`、stage props/surfaces/murals。

- RENDER-01：明亮玩具感、厚輪廓、柔影／濕墨，環境中性粉彩、隊墨最突出；不可任意改成寫實貼圖。
- RENDER-02：linear/sRGB、Neutral tone mapping、HDR、環境反射、方向日光／半球光、穩定soft shadow，採實際renderer值。
- RENDER-03：HDR scene→可選GTAO→bloom→grade/vignette→screen FX→output，品質適用AA；曝光／飽和／冷暖／hurt／flash一致。
- RENDER-04：天空／雲／日光、海動畫／反射／泡沫／水線、港柱／遠景／霧／鳥／燈／背景；marina/open-sea依地圖，不能清屏色替代。
- RENDER-05：resize／DPI／全螢幕／比例／UI縮放更新viewport、target及projection，非固定1600×900。

### 11.3 FX

來源：`splat-ref/src/fx/`、Boss與kit FX。

- FX-01：槍口／軌跡／命中／爆炸／死亡／spawn、跳落／變形／wake／攀牆滴墨、蓄力／盾／裝置／特殊／Boss／zone全事件回饋。
- FX-02：粒子碰撞／落地細墨／隊色／速度拉伸／生命週期，池化／數量上限依品質，長局不無限增長。
- FX-03：damage smear／hit/kill marker／低血、shake/recoil／轉場／reduced-motion等效行為；視覺FX不額外重算遊戲傷害或墨。

## 12. HUD、地圖、Super Jump 與結果

來源：`splat-ref/src/ui/{hud,hud-boss,diorama,menus}.js`、`src/game/{minimap,player,actor}.js`。

| ID | 強制需求 |
|---|---|
| HUD-01 | 隊色／雙方四人存活／復活／special ready、時計、各武器準星／散布／charge、hit marker；Boss改SQUAD／Boss HUD。 |
| HUD-02 | 墨槽／低墨／sub耗墨預覽、特殊charge/active、HP警示、feed／死亡原因倒數、隊友名牌／離屏箭頭、情境提示／可選FPS。 |
| HUD-03 | 小地圖地形／墨、友軍／合法揭露敵人、死亡／爆炸／jump，不能顯示所有潛藏敵人。death mark5 s、最後1.3 s淡出。 |
| HUD-04 | Tab/M可互動3D map diorama，隊友／基地／beacon pins與有效性，開圖阻正常射擊；strike獨立選點。 |
| HUD-05 | jump0.75 s蓄力、飛行弧線／落點警告／目標有效性／beacon消耗；死亡可預排隊友／beacon jump，復活執行，失效取消依來源。 |
| HUD-06 | ready/go／末分鐘／十秒／timesup／judge／模式事件動畫與音效，不重複觸發。 |
| HUD-07 | results姓名／隊伍／武器／turf/splats/deaths、self/bot、獎章、模式統計／XP／升級、rematch/back全流程。 |
| HUD-08 | Titan One display／Rubik UI或其轉製原生字型、墨形圖案、版型／色彩、focus/hover/press音效及spring轉場；預設debug面板不通過。 |

## 13. 音訊、設定與進度

### 13.1 音訊

來源：`splat-ref/src/audio/{audio,music,bossAudio}.js`、`songs/README.md`、事件呼叫處。

- AUDIO-01：重製程序音色／配樂，或同參數預生成原生音訊；不可任意既有歌曲替代。選配songs偵測／分配依來源。
- AUDIO-02：title/menu/battle/battle_final/results_win/results_lose、Boss／ambient，crossfade／最後分鐘／intensity／duck／stings時序一致。
- AUDIO-03：全主副特殊、命中／死亡／spawn、移動形態表面、低墨／倒數／zone／Boss／UI／XP都有聲；全清單以定義與呼叫處為準。
- AUDIO-04：master/music/sfx buses、3D距離／方向、loop更新／停止、voice limit／pitch variation、壓縮／混響等效；音量0靜音，切場無殘loop。

### 13.2 保存與設定

來源：`splat-ref/src/config.js DEFAULT_SETTINGS/QUALITY/PROGRESSION`、`src/main.js`、`src/ui/menus.js`、`src/game/character-style.js`。

| ID | 強制需求 |
|---|---|
| SAVE-01 | profile name預設Player／至多16字元、level1/xp0/wins/matches/totalTurf、weapon/sub/special/style重啟後一致。 |
| SAVE-02 | mouse/pad sensitivity、invertY、aimAssist及mouse opt-in、FOV、quality/shadows/bloom、FPS顯示/cap、master/music/sfx、shake/rumble、colorblind/minimap、預設難度／時長／模式、touch設定／fullscreen。 |
| SAVE-03 | 色盤tangerine-cobalt/bubblegum-mint/lemon-grape/aqua-cherry/lime-magenta與色盲yellow-blue，色值採TEAM_PALETTES。 |
| SAVE-04 | XP勝1200／敗500、每turf point1／splat40；每級需求800+level×350、可跨多級；Zone turf×0.6／zone ink額外／knockout勝+300，Boss按其分支。 |
| SAVE-05 | 缺欄位／無效值／舊武器遷移、合理default；失敗寫入不破壞舊檔、同場只結算一次。 |

| 品質 | pixelRatio cap | shadow | MSAA | bloom/AO | ink atlas | particles |
|---|---:|---:|---:|---|---:|---:|
| low | 0.75 | 1024 | 0 | off/off | 2048 | 0.4 |
| medium | 1.0 | 2048 | 2 | on/off | 2048 | 0.7 |
| high | 1.5 | 4096 | 4 | on/on | 4096 | 1.0 |
| ultra | 2.0 | 4096 | 4 | on/on | 4096 | 1.0 |

觸控影像密度／MSAA下限採rendererQuality；quality選擇／shadows選擇紀錄與裝置預設不可互相覆寫。品質只改呈現成本，不改遊戲判定。

## 14. 線上房間與同步

來源：`splat-ref/src/net/{session,transport,netmatch}.js`、`server/src/index.js`、`docs/NET.md`。已移除的舊GameNetworkingSockets host:port不能等同本節完成；正式傳輸選擇須保留房碼與行為契約。

| ID | 強制需求 |
|---|---|
| NET-01 | 五字元私人房碼create/join、最多8人，offline/connecting/lobby/starting/match/error；未知／滿房／開局加入／斷線正確錯誤與返回。 |
| NET-02 | 穩定名單順序、name/team/auto/ready/kit/style/ping/host/self/emote，每隊最多4。 |
| NET-03 | host選map/time/mode/duration/bots/difficulty/palette；依ready／隊伍／stage flag開始，全員一致config/roster。 |
| NET-04 | 雨後藍調巷弄3D hub/lobby、游入／離房／換隊、ready/換裝反應、倒數／jump開局、名字／空位牌。 |
| NET-05 | 玩家模擬自己，host模擬bot／時計／zone／Boss／judge；20 Hz actor/events，remote不二次模擬／傷害。 |
| NET-06 | sender timeline／jitter buffer85–300 ms、Hermite含速度、最多180 ms ballistic extrapolation、teleport cut／阻尼修正／事件動畫。 |
| NET-07 | seeded splat逐筆重播；remote projectile/kit ghost只視覺，不二次墨／傷害；裝置hit回owner、盾仍阻本地攻擊。 |
| NET-08 | shooter畫面判hit、victim owner套用；host統一judge。改成其他權責為顯式規格變更，不可稱完全同規則。 |
| NET-09 | 普通場leaver在原actor接bot；host離開由下一人接AI／時計／zone／Boss。Cargo移除且不補bot。 |
| NET-10 | 延遲／重複／斷線／換主／持續裝置／特殊／Boss move／開終局race處理確定，不重播傷害／XP。 |
| NET-11 | results後同房回lobby，參考host約12 s自動／可提早返回；不能全員踢回登入。 |
| NET-12 | relay membership/election/ping/idle timeout/fan-out相容，封包長度／索引／數值做邊界驗證。 |

至少測2/3/8 clients、AI開關、三模式、換隊／ready／換裝、玩家／host中途斷線、latency/jitter/spike、結算回房、Cargo限制。loopback不能冒充跨網互通；需記錄relay版本與測試條件。

## 15. 驗收方法與完成門檻

### 15.1 需求追蹤

狀態僅用：`未實作`、`實作中`、`待驗收`、`通過`。追蹤欄位須有需求／內容ID、參考來源、原生程式位置、狀態、檢查方法、證據／版本、已知差異。本文件完成不代表任何還原項目已驗收。共用系統通過不自動讓全部武器／地圖通過，不能用平均進度隱藏缺項。

逐項狀態記錄於 [restoration-progress.md](docs/restoration-progress.md)。資料匯入、診斷畫面與完整玩家功能分開記錄，不能以部分測試結果將整項需求標成通過。

### 15.2 邏輯與數值

- QA-01：抽取／對照完整config／kit資料、ID／順序／stage flags／style索引／defaults，避免漏欄位。
- QA-02：固定初態／seed／dt／輸入序列對照移動、跳／攀牆、武器、墨、傷害／死亡／特殊；30/60/120 Hz。
- QA-03：離散判定、名冊、傷害／耗墨公式、狀態與勝方一致；浮點容差逐項記錄，改變命中／路線／勝負的誤差不接受。
- QA-04：每主武器地面／空中／各墨／低墨／中斷／shield／device／Boss；每副／特殊限制／清理／線上重播，逐項有案例。
- QA-05：三模式完整至結果／重賽，反覆切圖／模式／品質／視窗／裝置，無上一場殘留。

### 15.3 視覺與音訊

- QA-06：先跑參考版取得固定camera/resolution/quality/map/time/loadout/style/seed截圖／短片，不能只靠README或記憶。
- QA-07：至少1600×900與1920×1080，另測寬螢幕／小視窗／高DPI／觸控橫向；八圖×day/dusk俯視與代表遊玩視角，合法mode variants另取證。
- QA-08：比輪廓／高度／角色比例／持握／動畫、墨邊／滴流／乾燥、光影／後製、HUD/menu/lobby/results；微小AA差可接受，缺物件／錯相機／替代效果不接受。
- QA-09：保存side-by-side／diff與條件，影像門檻以baseline captures校準；未校準前不宣稱視覺100%通過。
- QA-10：錄UI→intro→戰鬥→末分鐘→結果、各裝備／zone／Boss音訊，比cue／段落／空間／音量／loop；靜音截圖不驗聲音。

### 15.4 效能、硬體與打包

- QA-11：release可從輸出目錄執行、資源齊全，不依賴瀏覽器runtime／開發工作目錄／本機絕對資源路徑。
- QA-12：明示OpenGL4.5 Windows驗收硬體；1080p high一般4v4／8人Boss目標60fps，記CPU/GPU/driver、frame time/p95／記憶體／draws／particles，不虛構跨機保證。
- QA-13：品質降低不改規則／分數／網路可見性，完整局與多次切場無持續資源增長。
- QA-14：鍵鼠、至少一標準手把／震動、觸控分開記實機結果；缺硬體／relay／capture列待驗收，不以build替代。

原生工具最少需邏輯測試、seed／指定圖與模式、固定步進／輸入回放、捕捉、效能及多client測試能力；不必重寫所有參考lab。

### 15.5 全量完成門檻

1. 全部強制需求與12主＋15副＋19特殊／8圖／3模式／外觀／輸入／線上矩陣逐項通過。
2. 數值／流程無未核准差異，平台等效有來源、實作與證據。
3. 視聽／硬體／網路／效能／打包證據齊全，baseline與測試版本可識別。
4. 無阻斷遊玩、崩潰、資料遺失、必現網路分歧或不能返回流程。
5. 任何未完成項仍只能稱階段成果，不能稱100%。

## 16. 開發順序與現有差距

順序依依賴安排，不縮小最終範圍。階段進度更新不等於需要重新批准已授權的必要工作；重大規格變更另依§18。

| 階段 | 交付／退出條件 | 既有接點 |
|---|---|---|
| P0 | 鎖基準、內容資料、參考captures、數值fixtures | 本文件／參考／測試工具 |
| P1 | face geometry／碰撞／atlas／CPU grid，基準一圖路線與墨一致 | Level／splat／shader |
| P2 | 角色／物理／相機／血墨／首項裝備端到端對照 | Player／Camera／模型動畫 |
| P3 | 全裝備／AI／Turf生命周期／Practice | gameplay／GameScene |
| P4 | 八圖合法內容／環境／材質／FX／後製／音訊／menu/HUD/showcase | assets／render／audio／UI |
| P5 | Zone輪替／OT、Boss全招／模型／AI／HUD／結果 | 共用與模式系統 |
| P6 | 房碼／lobby／全kit同步／換主／mode結果／保存 | network／UI／persistence |
| P7 | 完整矩陣、視聽對比、硬體／網路／效能與打包 | §15驗收證據 |

現有C++可重用OpenGL視窗／shader／mesh、控制、三種舊武器、GPU/CPU墨資料、基礎血量／特殊、host:port與ImGui，但須重新驗收。主要差距：固定arena、OBJ角色、AI停用、裝備名冊／規則不同、拾取炸彈、特殊入口不全、非per-face墨圖、缺完整menu/locker/practice/map jump/三模式/房碼/換主，以及環境／後製／音訊差異。這是差距摘要，非完整bug清單。

本文件未宣稱完成執行期／視覺驗收。原SDD部分已過時，例如目前 `SplatMap::CalculatePercentages()` 已用CPU grid cell分母，不能繼續照舊文稱其除以texture pixel。

## 17. 來源衝突與待校準事項

| 項目 | 決定 |
|---|---|
| CONTRACTS舊四主武器／外觀數／bloom門檻 | 採有效WEAPON_ORDER／character-style／renderer |
| BOSS文件曾稱三場地 | 採MAPS flags：七圖Boss可用、Cargo禁止；仍逐圖測nav |
| EVENTS混合To add與後續additions | 查實際emit/subscribe，不把提案當已存在 |
| 程序資源與optional songs並存 | 程序為預設、選配曲目仍是正式行為 |
| 視覺diff／效能機器／觸控設備 | 取得capture與實機後校準，不能先標通過 |
| 參考可重現缺陷／不確定分支 | 記錄重現與影響；不擅刪功能，不刻意複製損毀／安全問題，提出最小等效修正並記規格差異 |

## 18. 後續變更規則

- 每次功能工作標明需求ID、來源、影響範圍與驗收方式，修改最小完整接點。
- 未重複列出的細節依鎖定參考補齊；未查明標待查證，不能自由設計後稱還原。
- 變玩法、精簡內容、改美術／網路權責、移除輸入／平台範圍，須列原／新行為、理由及驗收影響，交使用者決策。
- 進度區分「已實作」「已建置」「邏輯測試通過」「已參考比對」「已驗收」，不能互代。
- 參考改版同步hash manifest／需求／證據，受影響通過項退回待驗收。
