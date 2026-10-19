# 角色、全裝備與 Turf AI 移植執行契約

2026-10-18；前置提交 `cb1d5b3`。需求仍以 `spec.md` 和鎖定的 `splat-ref/` 為準；本文不縮減需求，也不把可選 ID 或靜態模型視為完成。

## 執行範圍與檢查點

1. 資料與介面檢查點：30 分鐘工作包，產出可重現的角色／裝備資料、共用 actor/world/pose headers 及匯出檢查。資料可獨立匯出；原生介面尚未通過前不接到玩家入口。
2. 原生行為檢查點：每個 owner 以最多 60 分鐘一包移植並建置可測的來源函式群；每包列出已移植、未移植及對照結果。完整目標仍是全部來源行為，不能用到時限為由把未完成項標為完成。到檢查點而未通過時，先定位／修正或重排下一包；其他無依賴資料匯出、shader、導航與來源測試可繼續。
3. 整合檢查點：全部 46 個 ID 的定義行為檢查、角色程序 pose／材質與 Turf bot 對照通過後，才將「全裝備／角色／Turf AI」列為可驗收。保留舊 350 actor、20 camera、23 stage、paint fixtures。
4. 原生接受檢查點：GTX 1650 上跑七張離線 Turf 的固定 seed 4v4、載入／換裝／死亡／重開、截圖與量測。使用 1080p high 目標 60 fps，記錄 p95、driver、記憶體及 draws；未達標只記失敗與原因，不標 QA-12 通過。

來源對照失敗時停止該行為的整合，修正第一個差異，不能調寬期望值或改來源。介面／更新順序未解時停止相依原生整合，來源資料與獨立 fixture 工作可繼續。視覺或效能接受失敗時停止完成宣告，保留診斷 capture／數據並修正；不回退成膠囊、通用射擊或 baked clip 冒充還原。這些是工程檢查點，不新增使用者批准關卡。

## 唯一所有權與更新順序

- `PlayerSimulation`：actor 的 position/velocity、輸入沿、形態、血墨、狀態、每 actor 的 WeaponRunner、局部動畫狀態。WeaponRunner 是唯一消耗主／副武器墨、決定發射與產生攻擊命令的地方。
- `CombatWorld`：唯一持有與更新 projectiles、subs、special world objects；唯一命中／距離傷害／友傷／盾／裝置傷害處理；唯一把每次有效 paint 回傳面積記到 actor turf／特殊 charge。actor 不再自行更新舊 shooter projectile 陣列或再次記 paint credit。
- `CombatWorld` 的 source-special/kit body hook：在 actor 的普通 movement 前決定 body override；body-owning special 走來源特定移動／碰撞並略過普通 horizontal/integrate，其餘 modifier 作用於同一份 controller。不是 world tick 後再移動一次。
- 特殊更新採來源的明確接點：既有 body special 優先於新啟動；新啟動消耗當幀，完成 frame 後返回。非 body 的 move modifier 在 horizontal 前、weapon override 在 WeaponRunner 前、actor tick 在 runner 後；special world tick 在 projectile／sub 後僅一次。共用 resolveMotion 碰撞尾段不再施加一般 integrate 的重力。傷害依無敵檢查→active armor→special filter→main-kit filter→HP 順序。
- 每個 fixed step：所有 bot/controller 寫 intent → actor 順序處理狀態／form／body hook／普通 movement（適用時）／WeaponRunner／actor special tick → source match soft push → world 順序更新 projectiles、subs、special objects 一次。實作須以 `main.js`／`match.js` 的實際順序校準；oracle 對照優先。
- `MatchSimulation` 僅擁有 actor lifetime、roster、phase／clock 與排程；CombatWorld 只登記 pointer，不擁有 actor。移除 actor／死亡／換裝／重開統一進 cleanup；移除後不得留下 pointer、盾、device、loop 或本生命攻擊。
- 遷移 shooter 為一個原子工作包：把發射與 flight/paint/credit 搬到上述唯一 owner，再同時切換獨人與多人的呼叫；不保留第二套默認 shooter 實作。default constructor 仍供 fixture 測試，但走同一套 runner/world。
- 多 actor 斷言：同一發 shot 只產生一個 hit／一次有效 paint 面積／一次特殊充能；覆己墨為零新增，覆敵墨正確轉移；死亡及重開後本生命攻擊數為零。

## 模組介面與檔案所有權

- Sol data owner：`ArsenalData.*`、`export-arsenal.mjs`、對應 assets／source fixtures；完整巢狀數值／字串／bool／array 與穩定順序，不能漏 kit 欄位。Node 僅開發時使用，執行時不需 Node／參考資料夾。
- 深度 combat owner：`PlayerSimulation.*`、`CombatWorld.*`、`WeaponRunner.*` 與 combat tests。提供 `setLoadout`、main/sub/special ID、team、weapon profile、combat pose；input 增加 sub/special/cheer。空 sub/special 取主武器來源預設；非法 ID 拋錯，沒有 generic fallback。
- 特殊模組 owner：新增 `WorldSpecials.*` 與對應來源 state/event fixtures；由 CombatWorld 唯一持有，模組保存特殊 active state／world objects，不取得 actor lifetime。combat owner 統一接入 actor／world 的 activate、body、move、weapon、actor/world tick、damage、pose、cleanup 接點；新模組 owner 不修改既有 combat 檔案。先以 Slam／Storm／Bubbler／Zooka 驗證接點，再完成全部 19 ID，未移植行為不得用通用攻擊替代。
- character asset owner：source geometry／indices／87-bone rig／rest／inverse／所有自訂 attribute、武器 grip/muzzle／part pivot／style palettes，binary exporter 和 native loader；`CharacterArt.*` renderer。
- 深度 character owner：`CharacterPose.*`／`CharacterShader.*` 和來源 pose／uniform fixtures；live pose 輸出 bone matrices、form visibility/scale、face/hair/tank/hurt/invuln uniforms、weapon part matrices。資料與 shader contract 確認後並行，不能各自定義第二份骨架順序。
- AI owner：`NavGraph.*`、`BotSimulation.*`、source fixtures。消費 actor profile／pose 與 CombatWorld threat/device descriptors，不寫 actor 或 combat 所有的檔案。
- parent：`MatchSimulation.*`、`StageViewer.cpp`、CMake、選單／HUD、整合測試、文件及最終判斷。

## 裝備逐項接受檢查

每列皆須有來源產生的狀態／事件期望值，及原生定義行為斷言；僅匯出或 selectable 不算通過。共同檢查：耗墨／cooldown、team filtering、LOS（來源要求時）、有效 paint credit、死亡／重開清理。

| ID | 定義行為與互動檢查 |
|---|---|
| shooter | cadence／bloom／ballistic impact/trail；跨隊命中與單次 credit |
| roller | rolling 墨帶／按距離耗墨／接觸傷害；flick windup 與近遠傷害 |
| charger | tap／full charge 消耗與射程／damage、beam paint／穿透規則 |
| blaster | direct 與 air burst 時序／距離傷害／同次爆風不重複 |
| twins | 交替射擊、jump+fire dodge／連 roll／turret lock／耗墨 |
| brolly | 開傘防禦、canopy HP／launch／recovery、canopy 與射擊碰撞 |
| spinner | charge→release→burst 持續／耗墨／中斷／移動速度 |
| bow | 地／空 fan、charge 與貼地 arrows 延時爆、同 volley 傷害規則 |
| blade | tap／charge slash、lunge、近戰／飛行 slash 的距離與去重 |
| mitts | 雙拳、蓄力 leap／armor／著牆、jump 脫離與 punch 命中 |
| brush | brush 墨路／速度、快速 swipe／耗墨與扇面傷害 |
| bucket | lob arc／越障／volley 去重與高度 falloff |
| bomb | bounce／落地起算 fuse、爆風 LOS／近遠傷害 |
| sticky | 地牆頂黏附、延時爆與範圍 |
| burst | contact explode、direct／splash 與低耗墨 |
| shaker | 移動／跳／搖動 charge、最多三次跳爆 |
| seeker | 地面追蹤／轉向／近距衝刺及墨路 |
| waddle | 落地感測／nav chase／跳階／無目標 timeout、可射毀 |
| torpedo | lock／展開／加速 homing／可射毀／子彈爆裂／一枚限制 |
| tracer | 高速反彈、hit/path marking 與落點墨 |
| boomerang | outbound／hover／return／orbit／finish 爆、hit detonation／一枚限制 |
| scan | cloud tracking 全隊分享，零直接傷害 |
| curtain | actor／shot blocking、HP／自然衰減／受擊衰減 |
| sprinkler | 任意表面 attachment、pulse paint、受擊／主人死亡清理 |
| mine | hidden sensor／延時／tracking、最多兩枚 |
| beacon | 三枚上限／兩次使用／受擊、jump anchor 契約 |
| mist | cloud slow／drain、direct poison／refill interaction |
| slam | rise／hang／fall／impact 距離 damage 與 body override |
| storm | throw／drift／rain paint+dps、special paint 不充能 |
| barrage | bomb 免費連投／gap／主武器共用／timer end |
| barrage_sticky | sticky 同上，來源獨立 gap |
| barrage_burst | burst 同上，來源獨立 gap |
| barrage_seeker | seeker 同上，來源獨立 gap |
| barrage_mist | mist 同上，來源獨立 gap |
| bubbler | damage→knockback、接觸隊友分享、timer／cancel |
| sonar | team reveal、slow／額外耗墨與結束恢復 |
| strike | map target→launch→impact vortex、dps／pull、取消與限時 |
| zooka | weapon override tornado、射距／致命命中／paint interval |
| wail | 持有瞄準／charge→wall-piercing beam 持續 damage |
| kraken | squid body／invuln／enemy ink immunity、hop attack |
| blower | 三泡泡上限／inflate／drift、友射爆／敵射 interaction |
| jetpack | hover／boost／blast shot、origin mark／結束 jump-back |
| stamp | pressure attacks／front guard／bomb clear／air flip／throw end |
| booyah | charge／ally cheer、throw 與大爆 damage；實際來源 Actor 未提供 armor，不自行新增 |
| zipcaster | sub grapple／wall cling／撞擊、main 共用與結束 jump-back |
| crab | machinegun／cannon／ball、vehicle HP／背頂弱點／destruction |

## 角色與 AI 接受

角色：來源 geometry／rig／material attributes byte/數值對照；8 hair／9 skin／10 outfit／8 iris／4 hat／4 brow／10 presets 索引完整；12 main model 與 sub/special prop source anchor 對照。live source Character 的 idle/run/strafe/brake/turn/jump/land/aim/fire/charge/throw/hurt/spawn/form/special/dance 逐步 bone／part／uniform fixtures；各 style/material 與 kid/squid/swim/climb 原生捕捉需固定光線／相機對照，沒有 capture 不宣稱視覺完成。

Turf AI：

- 七張合法離線地圖的 source NavGraph node/edge/nearest/path 對照，包含 spawn→centre／側路、ramp／jump／drop／climb／water／spawn barrier；不只單一平面。
- 固定 RNG 與場景下，實際 source BotBrain 的 paint、LOS/reaction fight、low-ink refill、low-HP retreat、device／shield 場景，對照 intent 與狀態切換／aim spring。Chill/Fresh/Fierce 三組來源調校均測。
- 每張地圖三個 seed，4v4 各 30 秒模擬；assert roster=玩家+3 vs4、雙隊都有移動／paint、可見敵人觸發 fight、低墨進 refill、低 HP 進 retreat。記錄卡住／水死；非戰鬥／非回墨時持續有 move intent 卻 5 秒內水平移動 <0.25m 即失敗，不能靠 teleport 消除統計。路線重試／source recovery 需有可追溯行為。
- actor／bot 共用同一個 controller、runner、world、耗墨與傷害路徑；Practice 仍只有玩家，Cargo 禁止離線 bot。
- Zone／Boss／online AI 檢查待對應模式／權責系統整合，保持 AI-04/06 未完成，不能因本輪 Turf 通過而宣稱所有 AI 驗收。

每個接受檢查失敗都保留 fixture／seed／stage／equipment ID 與第一個差異。獨立 verifier 僅在一個完整整合邊界執行一次；有修正或新失敗才重驗相應範圍。

## 2026-10-19 完成後提交的執行邊界

依本次使用者要求，完成 live pose、其餘八個特殊與 FPS 工作後提交；不能因已有部分來源樣本通過就先提交並稱驗收完成。

- live pose：保留既有 0.001 門檻，修正 32 場景的第一差異，並擴充原生比較至來源已捕捉的 parts、muzzle、head 與完整材質狀態；補驗形態／遠近 LOD／外觀代表樣本。來源 asset、rig 和 shader 的既有檢查繼續保留。正常渲染必須使用即時計算，不能使用 fixture 畫面代替。
- 特殊：逐項完成 strike、kraken、blower、jetpack、stamp、booyah、zipcaster、crab 的來源 state/event oracle 與原生行為／Actor-World 接點；依來源補上移動、傷害、擊中體積、cheer、map target、clinging 及真正的 jump-back 軌跡。原生 COMBAT-06 的生命清理差異繼續明示。
- FPS：新增 opt-in 原生 Turf 4v4 即時場景，執行 MatchSimulation、八名 actor 的 live pose／來源角色 mesh 及現有場地與墨渲染。1920×1080、關閉 VSync／FPS cap、固定 seed／步長，以 120 warmup + 600 量測影格起跑，保留 CPU／GPU 原始影格、p95、實際 GPU／driver、記憶體與真實 draws／particles 計數；七張合法離線地圖逐張量測，目標平均 60 FPS 與 p95 frame ≤16.667 ms。未達標需定位瓶頸並重驗，不能刪 actor／pose／draw work 換取通過。
- FPS 報告須區分本輪 live Turf 模組場景與完整 QA-12：目前未還原的環境／陰影／後製／特殊視效及 Boss 工作量不得冒稱已量測。這些缺項不因量測工具存在而豁免，QA-12 保持未通過，直到完整工作量具備並實測。不得把低負載數值外推為完成版效能。
- 提交前：完成受影響回歸及一次全新上下文的整合核查；只 stage 本輪及其前置還原檔案，保留使用者既有 `.gitignore` 與 `review.md` 變更，不提交 build／工具暫存檔，不推送遠端。

### 2026-10-19 主程式遷移接點

使用者追加要求：還原接近完成時，開始接回既有 C++ 主程式。已驗證的模擬模組由同一個 `reference_geometry` target 提供，`main.cpp` 的 `--reference-match` 已共用 MatchSimulation／Actor／裝備／AI／CharacterArt；正式流程接回應沿用這一套實作。

目前普通啟動仍由 `LoginScene` → `LobbyScene` → `GameScene`／`GameWorld` 管理，另有 Window／SceneManager／GUIManager 的生命週期。接回正式離線 Turf 前須把來源玩家輸入與 FollowCamera 接至本輪共用模擬，並重用既有視窗／ImGui context；不能讓測量用 scripted local controller 成為正式玩家控制。網路模式的舊 Player／GameWorld 不可只改資料型別便宣稱還原，需依對應權責規格另行整合。

### 2026-10-20 提交與推送指示更新

使用者追加要求將既有 `.gitignore` 變更一併 commit 並 push。完成本輪接受與獨立核查後，明確 stage 還原來源／資產／測試／效能紀錄及 `.gitignore`，提交並正常推送目前分支；`review.md` 刪除與建置／工具暫存檔仍不納入。不使用 force push。此指示取代上方歷史段落對 `.gitignore` 與不推送的舊限制。

### 2026-10-20 唯一入口與清理指示

使用者要求只保留 restoration，移除舊 OpenGL 程式並連同 review.md 的刪除 commit/push；本輪不分派 agent 驗證，由主 agent 自行建置與測試。此指示取代上方歷史段落的舊入口保留／review.md 不提交安排；完整來源玩法與視覺仍依 spec.md 逐項完成。
