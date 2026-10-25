# 戰鬥視覺契約

`CombatWorld` 持有投射物、裝置及世界特殊物件的玩法狀態。`ProjectileRenderer`、`DeviceRenderer` 與 `WorldEffects` 只讀取狀態或消費視覺事件；特效不得再次造成命中、傷害、塗地或特殊充能。測試範圍與證據見[還原進度](../restoration-progress.md)。

## 投射物

來源投射物模型與材質用於墨彈、箭、拳、刀波及 Charger ribbon。Live rig 槍口會先檢查有效數值、距離與遮擋，再回退至物理槍口；預測瞄準 socket 依 `aimReady` 混合，rollers 不套用，雙槍左手使用獨立 socket。停止的刀波依來源時間淡出；弓箭與拳套可投影陰影。

連射槍（`shooter`）的普通墨彈使用局部體積 SDF 呈現膠囊與分離墨滴的平滑融合，近鏡寫入實際表面深度；遠距使用網格，正交視角、遮擋、鏡頭進入包圍體及陰影均有專用路徑。這是每顆投射物局部的視覺材質，不會把不同射擊或隊伍融合成全域流體場。變形與亂數以既有投射物 seed／age 決定，不消耗玩法 RNG。

Spinner 蓄力連射另使用 `InkStream` 原生視覺發射器，輸入來自實際 `CombatPose.streaming`。發射器以獨立 120 Hz 時間線記錄彈道節點，以受限 Hermite 曲線與弧長重採樣建立平行傳輸框架的八邊形 Tube。新舊段落分開、依速度收頸並可產生端點墨滴、飛濺和牆面撞擊效果；停止後按自身壽命消散。Tube 為正式模式；局部 SDF 與螢幕空間深度為診斷選項。螢幕空間模式只保留最近層且有視角限制。所有模式共用同一節點資料；傷害、耗墨及計分仍由既有武器與墨圖集管理。

Bucket、Roller、Brush 的甩墨與爆炸效果使用原生 `inkSlosh`、`inkFlick`、`inkExplosion`、`inkImpact`、`inkAnticipation` 配方；真實武器操作會發出完整分層效果。每次操作可同時產生 3–7 個 hero mesh drops、10–40 個 mid drops 與 24–96 個 micro billboard spray；micro 以投影像素大小與視錐裁剪，低係數 Brush 降低粒子量。既有邏輯主投射物仍為 Bucket 5、Roller 9、Brush 5 顆並負責塗地，所有新增粒子只作視覺呈現，不增加傷害或塗地。Hero bloom 與拉伸各自更新生命週期，爆炸 hero 尺寸與拉伸較低；發射重播依局部 seed 決定。

## 裝置、特殊技能與世界效果

- 裝置資產含 29 種模型型別與雙隊材質。Renderer 根據即時狀態、年齡、計時器、目標及 actor 姿勢呈現飛行、部署、鎖定、爆炸前後、雲層、護幕、光帶和特殊角色道具。Tracer 軌跡、粒子池與尾跡皆有容量及距離上限。
- World FX 將來源短生命週期配方與持續特殊物件分開。粒子 pool 含 drops、puffs、glows、rings、shells、beams；事件連到槍口、命中、裝置預警、特殊技能、地面步行、下潛及游泳。事件消費與音訊路由分離。
- 特殊物件由其原生 pose／狀態控制，例如 Storm、Wail、Blower、Jetpack、Stamp、Zipcaster、Kraken、Crab、Tornado 與 Zooka；Boss 危險區與小螃蟹另見[Boss 契約](boss.md)。

## 呈現實作

部分飛行裝置以通用年齡旋轉呈現；雲狀 puff 使用穩定原生 seed；部分特殊道具材質使用精簡 lit shader。整體視覺驗收狀態見[還原進度](../restoration-progress.md)。
