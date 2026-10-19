# OpenGL 還原進度

基準：2026-10-17，依 [spec.md](../spec.md) 與 [來源雜湊](reference-baseline.sha256)。最近驗證：2026-10-19。

## 本次整合範圍

- P0：直接執行參考版模組匯出 config、layout、dressing／placement 原始資料及 23 個合法地圖／模式的標準化幾何。
- 地形核心：使用來源建立的 oriented blocks／faces，以及 PropKit 產生的道具碰撞體；原生 pointInside、groundHeight、raycast 保留來源的空間查詢順序。CPU 幾何使用 double 對齊 JavaScript Number；測試期望值來自 JavaScript Level／Physics，非 C++ 自己產生。
- 墨水核心：face-space CPU grid、dead cells、seeded splat、coverage 與 atlas packing；邏輯格顯示不等於最終 GPU 墨材質。
- OpenGL stage lab：可切換圖／模式、可塗與計分表面、碰撞體、邏輯墨／清墨／coverage，提供可重現 PNG 捕捉。
- P2 原生練習預覽：`--reference-game`，七張離線 Turf 地圖的本地玩家、Spritzer 射擊／落墨／充能、基本 HUD、選場／開始／暫停／重設／返回。固定 60 Hz；Capsule／flat footprint／台階／鐵網／屋頂滑落／欄杆／出生區屏障與跟隨相機已接入。

## 目前明確未完成

正常啟動仍走既有遊戲；新入口需 --reference-stage 或 --reference-game。沒有宣稱原遊戲已遷移。標準化碰撞資料已含 layout 與 dressing 生成的 prop colliders；道具外觀尚未繪製，碰撞檢視僅顯示其定向包圍盒，練習人物僅為碰撞膠囊。原生畫面未還原 rounded bevel、程序材質、壁畫、海水、環境、角色、動畫、光影後製或完整墨水 GPU 動畫。

全裝備、角色與 Turf AI 正在獨立模組及來源對照階段，尚未接入遊戲入口；具體證據與限制見末尾 P3 紀錄。模式規則、正式選單／練習、房碼／同步、音樂／保存仍待遷移。Cargo 可診斷檢視不代表能離線遊玩，Cargo Boss 組合不存在。

P0 仍缺固定條件的參考視聽 captures；P1/P2 的基礎碰撞、邏輯墨與本地玩家已有端到端接點，但 GPU 墨、角色動畫／槍口／完整裝備與全場路線對照未完成。不得將本次資料與查詢測試寫成 P0/P1/P2 全部通過。

## 驗證方式

- node --test tests/reference-export.test.mjs：檢查鎖定來源、可重現產物、名冊與合法組合。
- ctest --test-dir build --output-on-failure：原生幾何、墨水及可用時的匯出對照。
- build/Tiny-Splatoon.exe --reference-stage halyard --paint-probe 1 --capture build/captures/halyard.png --frames 3：診斷畫面捕捉。
- tools/check-native-practice.ps1：七張離線地圖，各 240 步移動／跳／射擊／squid 探測、GPU 墨資料／清墨／重塗回讀及非法組合拒絕。
- 實際測試與硬體結果另見本文末尾；沒有紀錄不能視為驗收。

## 需求追蹤

所有 ID 的來源及完整驗收契約見 spec.md 同 ID。以下「實作中」僅代表有基礎接點，不代表整項通過；原生實作集中在 restoration/，對照測試在 tests/。

| 需求 ID | 狀態 | 本次證據／限制 |
|---|---|---|
| ENG-01 | 實作中 | 僅資料／layout／查詢或邏輯墨基礎；完整需求待驗收 |
| ENG-02 | 實作中 | 僅資料／layout／查詢或邏輯墨基礎；完整需求待驗收 |
| ENG-03 | 實作中 | 練習模擬固定 60 Hz、渲染獨立；全武器／網路／完整時計待驗收 |
| ENG-04 | 未實作 | 尚無原生還原證據 |
| ENG-05 | 實作中 | 練習可重設／換圖與清理 GL；音效／網路及完整重入待驗收 |
| ENG-06 | 實作中 | 僅資料／layout／查詢或邏輯墨基礎；完整需求待驗收 |
| FLOW-01 | 未實作 | 尚無原生還原證據 |
| FLOW-02 | 未實作 | 尚無原生還原證據 |
| FLOW-03 | 實作中 | 七張離線 Turf 選場；完整模式／預覽／設定待移植 |
| FLOW-04 | 未實作 | 尚無原生還原證據 |
| FLOW-05 | 未實作 | 尚無原生還原證據 |
| FLOW-06 | 未實作 | 尚無原生還原證據 |
| FLOW-07 | 實作中 | 練習暫停凍結模擬、恢復／重設／返回；正式介面與線上待驗收 |
| FLOW-08 | 實作中 | GLFW 失焦暫停、恢復重置游標差與阻擋按鈕點穿；人工互動矩陣待驗收 |
| FLOW-09 | 實作中 | 單人無倒數預覽；完整換裝／工具／正式外觀待移植 |
| FLOW-10 | 未實作 | 尚無原生還原證據 |
| INPUT-01 | 實作中 | raw mouse 與來源 gain／pitch 範圍；可調設定及短按事件待移植 |
| INPUT-02 | 未實作 | 尚無原生還原證據 |
| INPUT-03 | 未實作 | 尚無原生還原證據 |
| INPUT-04 | 未實作 | 尚無原生還原證據 |
| CAM-01 | 實作中 | 正常跟隨／彈簧／遮擋／肩位有來源逐幀對照；角色槍口仍為碰撞體替代 |
| CAM-02 | 未實作 | 尚無原生還原證據 |
| CAM-03 | 實作中 | 82° 水平 FOV 正確轉換、正常跟隨 FOV kick；設定／完整震動待移植 |
| MOVE-01 | 實作中 | kid／squid capsule 尺寸接入；模型動畫尚未還原 |
| MOVE-02 | 實作中 | run／dry squid／swim／enemy ink 速度有來源軌跡對照 |
| MOVE-03 | 實作中 | 加減速／反向／轉向／空控／慣性有來源軌跡對照 |
| MOVE-04 | 實作中 | 跳／gravity／buffer／coyote／落地回復；完整時序矩陣待驗收 |
| MOVE-05 | 實作中 | 台階／支撐／body／roof／rail／grate 已接入；八圖完整路線待驗收 |
| MOVE-06 | 實作中 | 攀牆／越頂／emerge／fire buffer 接入；完整邊界矩陣待驗收 |
| MOVE-07 | 未實作 | 尚無原生還原證據 |
| MOVE-08 | 未實作 | 尚無原生還原證據 |
| COMBAT-01 | 實作中 | 本地 HP／傷害／出生無敵；敵人命中／全武器規則待移植 |
| COMBAT-02 | 實作中 | 敵墨傷害／上限／回血有來源對照及斷言 |
| COMBAT-03 | 實作中 | 墨槽與回墨、Spritzer cadence／耗墨接入；全裝備待移植 |
| COMBAT-04 | 實作中 | 新取得 turf 面積充能接入；完整特殊／裝備待移植 |
| COMBAT-05 | 未實作 | 尚無原生還原證據 |
| COMBAT-06 | 實作中 | 5.5 秒重生／1.6 秒保護、清理本生命彈丸；擊殺統計／死亡演出待移植 |
| COMBAT-07 | 未實作 | 尚無原生還原證據 |
| AI-01 | 實作中 | 10 組來源初始化名單／裝備／完整指定造型／共用 gameplay RNG 通過；完整作戰與 Boss 待驗收 |
| AI-02 | 實作中 | 三難度 duel 等 10 個來源 intent／aim 場景通過，含 refill／retreat／Waddle／held shield／canopy；完整矩陣待驗收 |
| AI-03 | 實作中 | 七圖導航／21 路徑與七圖三 seed 的 4v4 回歸通過；已接 --native／--reference-match，前線 jump 與完整模式仍待驗收 |
| AI-04 | 實作中 | 11 個來源特殊控制場景與盾／裝置場景通過；Zone／Boss 策略仍缺 |
| AI-05 | 實作中 | 三難度來源數值與 duel 對照；完整作戰矩陣未完成 |
| AI-06 | 未實作 | 尚無原生還原證據 |
| MAP-01 | 實作中 | 僅資料／layout／查詢或邏輯墨基礎；完整需求待驗收 |
| MAP-02 | 實作中 | 僅資料／layout／查詢或邏輯墨基礎；完整需求待驗收 |
| MAP-03 | 實作中 | 僅資料／layout／查詢或邏輯墨基礎；完整需求待驗收 |
| MAP-04 | 實作中 | 僅資料／layout／查詢或邏輯墨基礎；完整需求待驗收 |
| MAP-05 | 實作中 | 僅資料／layout／查詢或邏輯墨基礎；完整需求待驗收 |
| MAP-06 | 實作中 | 僅資料／layout／查詢或邏輯墨基礎；完整需求待驗收 |
| MAP-07 | 實作中 | 僅資料／layout／查詢或邏輯墨基礎；完整需求待驗收 |
| MAP-08 | 實作中 | 僅資料／layout／查詢或邏輯墨基礎；完整需求待驗收 |
| GEO-01 | 實作中 | 僅資料／layout／查詢或邏輯墨基礎；完整需求待驗收 |
| GEO-02 | 實作中 | 僅資料／layout／查詢或邏輯墨基礎；完整需求待驗收 |
| GEO-03 | 實作中 | 僅資料／layout／查詢或邏輯墨基礎；完整需求待驗收 |
| GEO-04 | 實作中 | 僅資料／layout／查詢或邏輯墨基礎；完整需求待驗收 |
| GEO-05 | 實作中 | 僅資料／layout／查詢或邏輯墨基礎；完整需求待驗收 |
| GEO-06 | 實作中 | 僅資料／layout／查詢或邏輯墨基礎；完整需求待驗收 |
| INK-01 | 實作中 | 僅資料／layout／查詢或邏輯墨基礎；完整需求待驗收 |
| INK-02 | 實作中 | 僅資料／layout／查詢或邏輯墨基礎；完整需求待驗收 |
| INK-03 | 未實作 | 尚無原生還原證據 |
| INK-04 | 未實作 | 尚無原生還原證據 |
| INK-05 | 實作中 | 僅資料／layout／查詢或邏輯墨基礎；完整需求待驗收 |
| INK-06 | 實作中 | 僅資料／layout／查詢或邏輯墨基礎；完整需求待驗收 |
| INK-07 | 未實作 | 尚無原生還原證據 |
| INK-08 | 未實作 | 尚無原生還原證據 |
| CHAR-01 | 實作中 | 128 rig／411 model／558 mesh、23 shader 與 37 固定畫面對照通過；八名 live 角色已接原生 Turf，完整視覺仍待驗收 |
| CHAR-02 | 實作中 | kid／squid／swim／climb 的來源 pose、local／remote 顯示代表場景通過；完整遊戲視覺矩陣待驗收 |
| CHAR-03 | 實作中 | 32 基本＋19 外觀／形態／LOD 代表場景以 0.001 通過；來源程序姿勢已接 Turf，投影尺寸 LOD 選級／dither 漸變與完整切換矩陣仍待接入 |
| CHAR-04 | 實作中 | 全部外觀索引／128 組 rig 已匯出，10 preset 固定畫面對照通過；Locker／保存及完整動畫待整合 |
| CHAR-05 | 實作中 | 來源材質 uniform／共享幾何已移植；完整戰鬥／出生狀態視覺未驗收 |
| RENDER-01 | 未實作 | 尚無原生還原證據 |
| RENDER-02 | 未實作 | 尚無原生還原證據 |
| RENDER-03 | 未實作 | 尚無原生還原證據 |
| RENDER-04 | 未實作 | 尚無原生還原證據 |
| RENDER-05 | 實作中 | 僅資料／layout／查詢或邏輯墨基礎；完整需求待驗收 |
| FX-01 | 未實作 | 尚無原生還原證據 |
| FX-02 | 未實作 | 尚無原生還原證據 |
| FX-03 | 未實作 | 尚無原生還原證據 |
| HUD-01 | 未實作 | 尚無原生還原證據 |
| HUD-02 | 未實作 | 尚無原生還原證據 |
| HUD-03 | 未實作 | 尚無原生還原證據 |
| HUD-04 | 未實作 | 尚無原生還原證據 |
| HUD-05 | 未實作 | 尚無原生還原證據 |
| HUD-06 | 未實作 | 尚無原生還原證據 |
| HUD-07 | 未實作 | 尚無原生還原證據 |
| HUD-08 | 未實作 | 尚無原生還原證據 |
| AUDIO-01 | 未實作 | 尚無原生還原證據 |
| AUDIO-02 | 未實作 | 尚無原生還原證據 |
| AUDIO-03 | 未實作 | 尚無原生還原證據 |
| AUDIO-04 | 未實作 | 尚無原生還原證據 |
| SAVE-01 | 未實作 | 尚無原生還原證據 |
| SAVE-02 | 未實作 | 尚無原生還原證據 |
| SAVE-03 | 未實作 | 尚無原生還原證據 |
| SAVE-04 | 未實作 | 尚無原生還原證據 |
| SAVE-05 | 未實作 | 尚無原生還原證據 |
| NET-01 | 未實作 | 尚無原生還原證據 |
| NET-02 | 未實作 | 尚無原生還原證據 |
| NET-03 | 未實作 | 尚無原生還原證據 |
| NET-04 | 未實作 | 尚無原生還原證據 |
| NET-05 | 未實作 | 尚無原生還原證據 |
| NET-06 | 未實作 | 尚無原生還原證據 |
| NET-07 | 未實作 | 尚無原生還原證據 |
| NET-08 | 未實作 | 尚無原生還原證據 |
| NET-09 | 未實作 | 尚無原生還原證據 |
| NET-10 | 未實作 | 尚無原生還原證據 |
| NET-11 | 未實作 | 尚無原生還原證據 |
| NET-12 | 未實作 | 尚無原生還原證據 |
| QA-01 | 實作中 | 僅資料／layout／查詢或邏輯墨基礎；完整需求待驗收 |
| QA-02 | 實作中 | 僅資料／layout／查詢或邏輯墨基礎；完整需求待驗收 |
| QA-03 | 實作中 | 僅資料／layout／查詢或邏輯墨基礎；完整需求待驗收 |
| QA-04 | 未實作 | 尚無原生還原證據 |
| QA-05 | 未實作 | 尚無原生還原證據 |
| QA-06 | 未實作 | 尚無原生還原證據 |
| QA-07 | 未實作 | 尚無原生還原證據 |
| QA-08 | 未實作 | 尚無原生還原證據 |
| QA-09 | 未實作 | 尚無原生還原證據 |
| QA-10 | 未實作 | 尚無原生還原證據 |
| QA-11 | 實作中 | 僅資料／layout／查詢或邏輯墨基礎；完整需求待驗收 |
| QA-12 | 實作中 | 七圖 live Turf 診斷量測達標；最終環境／陰影／後製／特殊視效及 Boss 未量測，完整 QA-12 未通過 |
| QA-13 | 未實作 | 尚無原生還原證據 |
| QA-14 | 未實作 | 尚無原生還原證據 |

## 2026-10-18 驗證紀錄

- Windows／MSVC x64 建置成功；CTest 3/3 通過（6.71 秒）：23 個合法地圖／模式的 851 筆地面高度、920 筆射線，以及兩張地圖的 atlas／dead mask／逐次 seeded splat 格點對照。這些樣本不能取代完整遊戲驗收。
- 每個變體含 265–597 個來源道具碰撞體。新增斜向射線與道具命中樣本；double 修正浮點邊界造成的 Halyard 遮蔽格點／計分面積差異。
- 八張 Turf 地圖皆完成 1600×900 PNG 擷取，Cargo Boss 正確拒絕。紀錄位於 `build/captures/smoke-results.json`；八次 OpenGL renderer 均為 `NVIDIA GeForce GTX 1650 with Max-Q Design/PCIe/SSE2`，OpenGL `4.5.0 NVIDIA 591.74`。
- 執行檔加入 `NvOptimusEnablement=1`，請求 Optimus 使用 NVIDIA；實際顯卡以 renderer 紀錄為準。未變更 Windows／驅動的全域設定，也尚未量測 FPS。
- 參考版的選單／Halyard 設定／結算觀察截圖位於 `build/captures/reference/`。相機、重播條件與原生畫面未對齊，不能作為 QA-06 視覺一致性通過證據。
- 獨立唯讀驗證結果：CONFIRMED，範圍限於道具碰撞資料、CPU 雙精度對照、診斷擷取與 NVIDIA 提示；無新增發現，不代表完整遊戲或視覺一致性已驗收。

擷取檔與建置紀錄是本機驗證產物，後續可用上述指令重新產生。正式遊戲操作、角色與道具外觀、音效、GPU 墨材質及連線同步仍待遷移。

## 2026-10-18 P2 練習整合

- 提交回復點 `d0a1fd9` 包含上一輪資料／地形／CPU 墨／GTX 1650 提示；本節描述其後的玩家整合。
- MSVC 建置成功，CTest 6/6 通過。新增控制器來源對照：292 ground、584 body、292 capsule、292 bodyFits、146 camera probe；保留原有 23 變體查詢與墨水格點驗證。
- 實際 JavaScript Actor 的 350 個取樣點（60／120 Hz）對照 position／velocity／yaw／HP／ink／ground／squid／submerged／climbing；涵蓋跑動、煞車、反向、轉彎、跳、游泳、敵墨、攀牆越頂、開火／收火移動、台階、屋頂、欄杆、鐵網與出生區屏障。
- 實際 CameraRig／Physics 的 20 個連續影格對照 eye／forward／FOV，包含 30／60／120 Hz 與 0.05 秒步長、跳落／遮擋／地板／肩位／後座／傳送。
- 七張離線地圖各執行 240 個 60 Hz 步驟及 PNG 擷取；每圖有射擊與 turf 格點，並對 GPU ownership 貼圖做三次完整回讀（射擊結果／清墨／敵隊重塗），與 CPU sample 逐像素相符。紀錄：`build/captures/practice/smoke-results.json`。全數使用 GTX 1650；Cargo 與 Zones 的練習組合在開窗前拒絕。
- 墨格上傳改為僅更新變動區域，避免每次落墨重算完整 4096² 貼圖；這仍是邏輯格視覺化，並非最終墨水材質。
- 獨立唯讀驗證：CONFIRMED，範圍限於上述離線練習切片。其發現的 PowerShell 5.1 預期 stderr 提早中止問題已修正；之後以 `powershell.exe` 重跑七圖腳本亦通過，PowerShell 7 的測試同樣通過。

仍未完成：來源 skinned character／程序動畫及動態槍口、完整 projectile／敵方傷害對照、全武器／副武器／特殊、AI、Turf／Zones／Boss 生命周期、正式選單與 Practice 工具、音效／連線／保存。滑鼠／暫停／焦點與選單互動仍需人工驗收；自動腳本不代表已測過人工操作。

## 2026-10-19 P3 角色／裝備／Turf AI 對照（尚未接入遊戲）

- 裝備資料保留來源 12 main／15 sub／19 special 的完整巢狀設定；資料存在不能代表行為完成。既有八種核心主武器有 992 筆 runner 來源取樣，副武器與四種 kit 主武器／特殊仍在逐項移植。
- 角色匯出與原始來源重建檢查通過：128 組 87-bone rig、411 model、558 共用 mesh；12 main／15 sub 模型、8 hair／9 skin／10 outfit／8 iris／4 hat／4 brow／10 preset 索引完整。五種 special prop 目前只匯出資料，未算入上述渲染接受。
- OpenGL 實際繪製來源幾何及 23 組展開 shader；12 主武器、15 副武器與 10 預設造型共 37 個固定 panel。原生為 NVIDIA GeForce GTX 1650 with Max-Q Design/PCIe/SSE2，OpenGL 4.5.0 NVIDIA 591.74；來源 WebGL 為 Edge／ANGLE Intel Iris Xe D3D11，沒有宣稱瀏覽器也使用 NVIDIA。
- 固定相機／燈光／1024×1200 framebuffer 下，37 panel 全部通過預先設定門檻 silhouette IoU ≥0.995、前景 RGB MAE ≤1/255；實測最小 IoU 0.998737、最大 MAE 0.688/255。報告為 build/character-render-comparison.json、character-sub-comparison.json、character-preset-comparison.json；原生截圖位於 build/captures/character/，來源截圖位於 output/playwright/。
- 新 preset 對照抓出墨槽材質缺少來源 onBeforeRender 的 world-up／液面縮放 uniform；已移植，並保留 squidGhost 的來源 GreaterDepth。未放寬畫面門檻。測試快照分為逐項函式，修正 MSVC Debug 將大量暫存塞入單一函式造成的 Windows stack overflow；這些快照只供 renderer 測試，遊戲不能以快照代替 live animation。
- 角色來源 pose fixture 的 32 場景／283 取樣可重現，idle 30／60／120 Hz 即時 bone／channel／face／hair 比較已通過；run 與其餘 live 動畫尚有第一差異，CHAR-03 未驗收。
- 七張合法離線 Turf 導航／21 路徑與兩個 corner-ray 回歸通過；三難度 stationary duel、paint、blocked LOS、low-ink refill、low-HP retreat、Waddle、held shield fight、canopy route 共 10 個 source BotBrain intent／aim 場景對照通過。七圖 × seeds 1／7／2026 的 4v4 各 30 秒模擬於擴充 AI 通過後重跑，全數通過（82.06 秒）；此紀錄綁定該次建置，後續 combat 修改須重跑受影響範圍。Booyah cheer 因真實特殊狀態未移植仍待驗收，Zone／Boss／online AI 不在本輪通過範圍。
- MSVC showIncludes UTF-8 前綴偵測已修正，Ninja depfile 可追蹤 PlayerSimulation／CombatWorld／WeaponRunner 標頭，避免介面改動後遺留舊 ABI 物件。

重現固定渲染比較：先執行 character_art_test，再用本機 HTTP server 開 tests/character-render-reference.html（無 query：main；?subs：sub；?presets：preset），以 Playwright 擷取 canvas；執行 tests/compare_character_renders.py SOURCE.png NATIVE.png [subs|presets]。原生靜態接受測試不需要瀏覽器；來源畫面比較需要未改動的 splat-ref/vendor/three。

限制：正常遊戲入口／--reference-game 仍是 P2 膠囊練習；此輪來源角色與模擬尚未整合。全 46 裝備行為、所有 live animation、完整視覺／環境／陰影／後製與 1080p high 的 60 fps／p95 量測均未完成，QA-06／QA-12 保持未驗收。

本輪回歸快照：完整程式重建成功；排除正在修正的 expanded AI oracle 及長時間 Turf 模擬後，CTest 15/15 通過（64.60 秒）。隨後 expanded AI oracle 的 10 場景全部通過；Waddle 裝置 fireFrames 55／40 差異的根因是原生重複等待 reaction gate，已依來源移除。15/15 本身不包含擴充 AI 與長模擬，不能單獨寫成完整 AI 接受。另以 Halyard seed 1 短模擬驗證 Practice 直接開始／暫停／時鐘／重開契約通過。來源 HOLD 表已直接從 Character.hold 匯出 12 種武器的 anchor／stance／recoil 與 family，新增原生資料檢查及 --check 重現檢查。

P3 整合檢查點：凍結本輪 Brolly／Seeker／副武器修正後，完整重建與 CTest 17/17 通過（150.56 秒，含七圖三 seed 的 Turf 84.27 秒與 10 個 AI 來源場景）。Brolly 檢查包含持盾閾值／位置、swept shot／ray 命中與 HP、爆風 quadratic 吸收／隊友防護、發射時序；Seeker 檢查衝刺 commitment／sidestep hold／恢復轉向；這些屬已測範圍，不是完整 15 sub 或 46 裝備完成聲明。

全新上下文 Luna 唯讀核查結果 CONFIRMED：來源匯出／23 shader／37 固定畫面與當時 10 場景 AI 可執行檔。其發現的 singular tank-parent 差異已補上來源 determinant=0 的 identity-rotation 行為；另以五組實際來源材質 onBeforeRender 產生的旋轉／非等比／零縮放 fixture，隔離原生 fill draw 並回讀 GPU uUpL／uSY，targeted check 通過。圖片報告含來源／原生 PNG 的 SHA-256；數值仍全部通過。獨立核查未涵蓋完整 live pose、遊戲入口、其餘裝備或完整視覺／效能。

對戰初始化新增 10 組實際來源 `Match.setup()` 對照：seeds 1／7／2026 的 Turf、Practice、autopilot，以及指定 Bow／Mine／Storm 與完整外觀的名單。來源使用真實 Actor／BotBrain，僅替換不參與玩法的 Character 繪圖殼，排除 Three.js 圖形 UUID 配置的亂數消耗。原生依來源順序洗牌名稱、分配主副武器／特殊、產生 Bot 外觀、初始化 brain，再統一出生；actor 與 bot 接續 Match 的同一亂數流。名單／裝備／外觀／位置／朝向、第一發子彈的亂數 seed 與 deterministic restart 均通過（6.12 秒）。fixture 的完整 explicit style 範圍不代表 Locker 的空外觀／名稱解析已移植。

Slam／Storm／Bubbler／Zooka 的獨立 WorldSpecials 模組接受測試已加入 CMake，C++17 編譯及測試通過（1.29 秒）。這個檢查點尚未驗收 Actor／CombatWorld 的整合接點，不能等同遊戲中可用。新共用亂數會改變完整 Turf 走向，先前 21 場長模擬紀錄不能直接套用到這次變更；整合後需重跑。

新增 `reference_restoration_exports` CTest：12 支現有匯出器以 `--check` 重建來源結果並核對已產生資料，涵蓋 arsenal、角色 assets／material／shader／grip／固定 render fixture、AI nav／brain、Brolly／主武器 kit／特殊。首次 12/12 通過（46.30 秒）；此項只驗證產物可重現，行為接受仍由原生測試判斷。

`.gitattributes` 為上述產生文字指定 LF、角色 `.bin`／`.mesh` 指定 binary，避免本機 `core.autocrlf=true` 在 checkout 後改寫 byte-for-byte 產物。以 `git check-attr` 核對 fixture／generated C++／catalog JSON／shader 的 LF 及 binary 的 `-text`；沒有重新正規化其他檔案，也未修改鎖定來源。

Match 已補上原生遺漏的 Judge 階段：Finish 等待 2.6 秒後擷取 FacePaint coverage，依來源規則判定勝隊（精確平手才使用共用亂數），待呼叫端完成判分呈現後才進 Results；restart 會清除結果。新增六組實際來源時鐘／暫停／Judge trace，涵蓋兩隊勝負排序、平手亂數兩個分支及結果快照不隨後續落墨變動。此處將 actor 停用以隔離時鐘，來源 paint stub 只提供勝負排序；未宣稱完整 180 秒戰鬥的逐幀對照或 Judge UI 動畫完成。10 組 setup 與這六組 trace 的合併原生檢查通過（1.39 秒）。

較早的 live pose 檢查點為 10/32 場景通過：idle、run、backpedal、strafe、treadmill、turn、weapon_roller、weapon_brush、idle_30hz、idle_120hz。修正包括 treadmill 接地座標滑動、roller flick／brush swipe 來源 pose、weapon pose 的套用順序，以及 bob 使用來源的平滑速度 gs；當時其餘 22 場景仍有差異。比較器涵蓋 form transforms／channels／local bones／skin matrices 與部分 face uniforms，尚未涵蓋完整 parts／muzzle／head／material，因此 CHAR-03 仍未驗收。

WorldSpecials 擴充為九個 ID：slam／storm／bubbler／zooka，以及 barrage／barrage_sticky／barrage_burst／barrage_seeker／barrage_mist。來源 release／gap／投擲速度快照與獨立原生檢查通過（6.15 秒）；其餘十個合法 ID 明確回傳未啟用，保留 gauge／ink／form／movement，不用通用攻擊代替。未知 ID 拒絕。已接入 Actor 的 activation edge／body／move／weapon／tick／pose／damage 及 World 的單次全域更新與清理；實際 Actor／World 整合檢查通過（0.89 秒），包含 Slam body／armor／生命結束的半量 gauge、Bubbler 與 active 期間一般落墨不充能、清理及重開。Slam frame fixture 的初始位置為地面上 1 公尺，原生整合測試已對齊該初始條件。COMBAT-06 的原生死亡清理比來源持續存活的部分 world object 嚴格，兩者分開驗證，沒有宣稱該生命結束行為完全相同。

本次整合快照 CTest 21/22 通過（176.05 秒，`build/p3-current-acceptance.log`）；唯一失敗項是 Turf：Kelpline seed 2026 的 charger 及 Halyard seed 7 的 bucket 出現持續移動輸入但五秒內水平前進不足 0.25 公尺。不能沿用舊亂數流的 21 場通過紀錄。其餘包含 594 筆 Bow／Blade／Mitts 狀態與事件、原有 992 筆 runner、350 Actor、10 AI 場景、來源產物及 GPU shader／固定模型檢查通過。

新獨立唯讀核查結果為 REFUTED，範圍限於本次 Match／特殊／kit 整合：指出 Bubbler 敵墨免疫缺漏，以及 Mitts 累積 leap 事件不應用當前 leaping 布林代替比較。已據來源補上傷害條件／敵墨回歸及真正的 leap 累積計數；新改動仍需編譯與 primary 驗證，尚不能以測試已通過為由關閉發現。

上述兩項發現隨後完成 primary：350 Actor／10 AI／594 kit／含敵墨的新 World 整合 4/4 通過（14.82 秒）；原核查者針對兩項修正唯讀複核並重跑 World／kit，確認 CONFIRMED resolved。未重新擴張為全遊戲接受。37 panel 的來源／原生 PNG SHA-256 仍與既有比較報告相符。

Bot watchdog 已對齊來源的 `_wiggle`：清除舊路徑、重設 goal／repath、站立時要求跳躍，並以實際 climb aim 取代殘留 climb timer 作為豁免條件。原 Kelpline 2026／Halyard 7 重現案例各 30 秒、AI10 與 Actor350 通過。完整 21 場重跑為 20/21（60.20 秒），新失敗為 Tidewater seed 1 的 Bow，15.1 秒於 (-8.52306, 0.786675, 38.0199) 水平卡住；正在排查。另有來源的重複高階差重試計數移植候選，但尚未證明可解除該案例，不能將其算為修正通過。

後續逐行對照發現 Actor 遺漏來源 `WeaponRunner.busy()` 的形態與站立回墨條件。已補上共用判斷，保留 Blade「蓄力可下潛、揮擊不可」的來源區別；992 runner 與 594 kit 來源 fixture 新增 busy 欄位，Actor 新增 Charger／Spinner 收火下潛場景及首次下潛影格，共 394 筆。三支匯出器可重建；原生編譯、行為與受影響 Turf 重跑仍待完成，不能沿用前述較舊快照。

補上 busy gate 後，Tidewater seed 1 的 30 秒原重現案例通過，無水平卡住（paint 4373／9969、移動 119.814／147.179、死亡 2）；未新增脫困規則。Bot 的 Bow charge／release 閾值本來已與註冊 kit 後的來源一致，缺漏在 Actor：來源會於蓄力仍 busy 時拒絕 squid 並脫離攀牆。此為程式條件對照與重現通過，沒有宣稱已還原出舊紀錄中的確切第一差異影格。新增 busy 對照另指出 Mitts 飛躍同樣屬 busy，已補上；Charger fixture 的來源固定 sample=0，原生測試須停用 floor paint，以免真實 charger beam 落墨造成來源 stub 沒有的游泳回墨。這兩項後續修正仍待 primary。

站姿對照另找出來源會於 updateFeet 前，每幀混合 idle／武器／lock stance；原生原本只保留 idle stance。依來源順序及 Float32 寫入補上後，隔離建置的嚴格 live pose 對照提升至 12/32，20 場仍有差異，CHAR-03 不變。站姿與 bob 的修正沒有調寬 0.001 門檻。來源匯出檢查最新 14/14 通過（50.37 秒）；該次不含接著新增的 Sonar／Wail 檢查。

Sonar／Wail 已接入 WorldSpecials，合計 11/19 special ID，原八個未移植 ID 仍明確拒絕啟用。Sonar 依來源標記敵隊（包含當時死亡的 actor）、0.6 秒 body cast、slow／額外耗墨與到期；Wail 包含 hold／嚴格 fire 閾值／Bot 及 timeout 放置、來源 speaker mouth／pitch、穿牆光束與 charge／blast／fade。Bot fixture 注入來源實際使用的 gameplay uniform，沒有宣稱圖形 UUID 配置之後的全域 seed 與原生相同。新測試涵蓋友方／偏軸排除、owner cleanup、Zooka／Wail 禁止 squid 與 4.2／3.2 移動速度；Actor 同時補上來源 body／aimFace／weapon firing pose 的朝向規則。

Wail 原本來源 fixture 一次呼叫 `Speaker.update(3.2)`、原生卻逐次 1/60 秒，導致測試對 blast→fade 邊界提出錯誤預期；沒有用 epsilon 或放寬誤差修改執行時。新增真實來源 60 Hz trace 為 78 charge／193 blast／36 fade frames、總傷害 836.333333333275；原生使用相同步數並以 1e-6 對照通過（7.24 秒）。原單步來源 832 傷害的 fixture 保留，兩種步長沒有混稱為同一契約。

本次獨立唯讀核查另找到共用 weapon input 的 raw firePressed 未遵守 emergence buffer。已按來源在 kidT 達 0.07 秒後產生同一份 fire／pressed 並清空 buffer，再交給特殊或 runner。新增真實 Actor `rollerPop`：前六幀 squid、第七幀單次 fire，至第十一幀保持 ink=100、第十二幀才消耗 flick 的 9 墨；合計 Actor fixture 已增至 438 筆，重建後 primary 通過。Wail 的 0.2 秒放置閾值使該特例不影響正常 Wail 按鍵，實際可重現的修正是 Roller 出墨時機，沒有誇大成所有 Wail 點擊都曾失效。

持續射擊 pose 另補上來源 fireWant／fireHold、放開反衝與 chest／spine／hips／neck／weapon anchor 姿勢。嚴格場景數仍為 12/32：idle、run、strafe、backpedal、treadmill、turn、aim、weapon_spinner、weapon_roller、weapon_brush、idle_30hz、idle_120hz。Shooter 已越過原 HipsP.y 差異，第一差異推進到 frame 15 左前臂 quaternion（0.0012189062），其餘 20 場尚未通過。

最新凍結檢查點：完整 MSVC／Ninja 建置成功；受 buffered input 修改影響的 9 項 CTest 全通過（91.68 秒，`build/p3-buffered-fire-acceptance.log`），包含七圖 × 三 seed 的 21 場 4v4（64.29 秒、零五秒水平卡住）、438 Actor、10 AI 場景、Match setup／Judge、11 special、992 runner／594 kit 及 World。其餘 14 項 CTest 全通過（106.37 秒，`build/p3-sonar-wail-remaining.log`），含來源產物、導航／幾何／墨／相機、23 shader／37 固定模型。兩批合計覆蓋目前已註冊的 23 項，沒有把仍失敗的 native live pose 32 場景算進 CTest 通過數。

Wail Bot／timeout 檢查另外移除原測試多餘的 ±1 frame 容許，改為精確等於來源的 28／361 frames；只重建該測試並再次通過（0.77 秒，`build/p3-wail-exact-primary.log`）。沒有變更執行時規則。獨立 Luna 核查已直接執行 Wail 測試及修正後的 Actor 測試，確認 bounded Sonar／Wail 與 buffered input 接點；其餘八個 special、完整 live pose、遊戲入口與 FPS 不在該核查範圍。

下一包 Kraken 已完成來源準備：`docs/kraken-source-contract.md`、真實 Actor／SpecialSystem exporter 與帶六個來源 SHA-256 的 fixture，涵蓋移動／敵墨免疫、地面攻擊／空中下墜／hop、落地 LOS 與 quadratic damage、trail、受擊 knock、421 幀到期及 owner splat。來源重建與 MSVC C++17 fixture syntax-only 檢查通過，並加入既有來源匯出測試（新增子項單獨通過，`build/p3-kraken-source-check.log`）。這些只是來源期望值與接點契約，Kraken 原生仍未啟用，不能增加 11/19 的完成數。

### 2026-10-19 live pose／其餘八特殊／FPS 接續驗收

完成後才提交的執行邊界已由獨立 plan-verifier 回覆 READY；這是計畫可執行性核查，並非完成驗收。尚未 commit。

- 原生 SuperJump 已接上來源固定地點的蓄力、飛行、落地及 home return；Actor oracle 擴為 2598 筆，包含 30／60／120 Hz。受影響的 Actor、runner 及 Kraken 三項已通過（`build/p4-special-wiring-primary.log`）。
- Kraken 原生逐幀來源測試通過：移動與 facing、敵墨免疫、41 幀地面跳擊、11 幀空中下墜、hop、受擊免疫與 knock、421 幀到期，以及近／邊緣傷害、LOS／隊友／死亡／範圍過濾。落地接受傷害總量精確對齊 368.987546063013；其他七種技能仍在對照驗收，不能宣稱 19/19 已通過。
- Actor 提供當幀 pose trigger／frame callback，事件在 pose 更新前送達；已接上 spawn、hit、jump、land、主副武器與特殊起落事件。完整 32 場景仍以原 0.001 門檻檢查，巢狀零件、完整 uniforms 與 dance 修正進行中。
- 新增 `--reference-match` 的真實八人 Turf 入口及逐幀 CPU／GPU capture。GTX 1650 Max-Q／NVIDIA 591.74／1920×1080／VSync off 的 2 warmup + 3 measured 短測試成功，134 實際 draws；Debug CPU 平均 42.5212 ms、GPU 平均 7.1841 ms。此短測試只確認接線，不能當作效能接受結果。完整 Release 七圖 120+600 測量尚未完成。
- FPS 報告明示未包含 projectile／special world meshes、particles、最終環境、陰影、後製及 Boss，`qa12_claim` 保持 false。

跳躍 pose 後續補上來源 jump trigger 的 spring／hair impulse、running-leap 狀態、空中姿勢混合及跳躍 voice micro-expression。隔離 MSVC 編譯成功；嚴格場景數保持 12/32、既有 12 場不退步。`jump_land/30` 第一差異從 kid.scale[1] 推進至 channels[26]（0.00358212），因此跳躍仍未驗收，沒有因套用來源層就宣稱整個場景完成。

本輪接續檢查點（尚未完成提交邊界）：

- 其餘七項特殊 Strike／Blower／Jetpack／Stamp／Booyah／Zipcaster／Crab 已通過真實來源時序與傷害 fixture，連同 Kraken 和既有十一項，19 個 ID 均已啟用並具核心原生測試。涵蓋 Bubble chain、Crab 540 幀到期、實際 prop 槍口／座位、Zip 510 幀到期與返航；這不代表全部特殊視效與模式互動完成。
- Tracer／Torpedo 的來源動態 hitR／hitH 已接至 Crab 膠囊；新增 Tracer 在一般玩家半徑外命中 Crab 裝甲的回歸通過（`build/p4-capsule-primary.log`）。Burst／Mist 的來源固定半徑不一併改動。
- Bot 特殊控制來源對照新增 11 場，使用主武器 specialCost 及來源 kind-or-ID，修正 Kraken readiness 與 Barrage 變體。所有意圖與移動以 1e-6 對照通過；測試凍結 actor pose，不冒稱整條 Actor 軌跡來源比對。Zone Strike 重新選點仍屬 AI-04 缺項。
- 本次 Release 建置後 21 項非 GPU／非來源產物 CTest 全通過，59.28 秒（`build/p4-runtime-current.log`）；其後仍在修改 live pose，最終提交前需重建受影響範圍。
- `--native --frames 3` 的 stage-select 與 `--reference-match tidewater --frames 3` 的真人操作入口均在 GTX 1650 開窗、繪製與正常結束。後者共用八名 actor 的即時角色與 FollowCamera。此短 smoke 不代表已人工驗收滑鼠、暫停、結果或整場對局。
- 嚴格 live pose 已擴至 parts／head／muzzle／完整 uniforms；新比較面使原有 12/32 與目前結果不可直接相減。魷魚活動、武器活動零件與 recoil analytic spring 差異正在修正，不能標 CHAR-03 完成。完整七圖 120+600 FPS 與最終獨立核查仍待此範圍穩定。
- 使用者允許在 restoration/ 設計新架構；spec.md 已記錄單一 gameplay owner／context 生命週期與舊碼移除條件。新入口在同一 context 內選圖→對局→返回；目前仍保留舊入口及其網路依賴，尚未刪除仍有 callers 的檔案。

### 2026-10-20 live pose 接續對照

- native pose 的基本 32 場景與新增 19 場景已列為必要 CTest；後者包含外觀索引、local／remote 變身、近遠 LOD 與持有／隱藏副武器。逐項檢查骨骼、skin matrix、126 channels、全域角色 uniforms、head／muzzle 及巢狀零件 transform；零件個別材質另由 weapon animation 與 GPU render 測試覆蓋，不能視為任意材質狀態的窮舉驗收。
- quaternion 比較接受 q 與 -q 的相同旋轉，仍維持逐分量 0.001 門檻，skin matrix 逐項對照不變。這修正特殊動畫中的符號誤判，沒有略過該場景或放寬數值誤差。
- 本次來源重建 22 個子測試全數通過（62.11 秒，`build/p6-exports-primary.log`），包括原始 Character 32 場景、19 個 variant、角色資產／shader／render fixture、裝備、AI 與特殊技能來源產物；276 個鎖定參考檔 SHA-256 仍相符。此紀錄只證明 fixture 可由未改動來源重建，不能取代原生執行時接受。

目前尚在修正嚴格 live pose 差異；七圖正式效能與最終獨立核查尚未完成，保持未提交。

P7 檢查點：Release 全程式重建成功；23 shader 與 37 固定 panel 重驗通過（`build/p7-gpu-primary.log`，9.28 秒），三份畫面對照報告為 `build/p7-character-{render,sub,preset}-comparison.json`。最小 IoU 0.998736975、最大 RGB MAE 0.688382542/255，未放寬門檻。八項原生入口 CLI 衝突組合亦通過。一次測試與 relink 重疊造成 Windows 執行檔鎖定／暫時不存在，已改為建置完成後再循序跑 CLI；該次排程失敗不計為接受結果。

P11 live pose 接受：32 基本場景與 19 外觀／形態／LOD 代表場景全數通過（`build/p11-pose-primary.log`，14.31 秒）；保留 0.001 門檻與全部原有比較面。修正來源 Float32 timer、受擊／發射表情、斜坡 target normal、空中落地腳位記憶、出墨頭髮／墨槽衝量與遠距細節保留。此接受涵蓋 fixture 指定的 live 計算，不把代表樣本寫成整份 CHAR-03 或完整遊戲視覺完成；目前 Turf 繪圖仍固定 game body tier，投影尺寸選級與 dither fade 保持缺項。

最終 Release 回歸：31/31 CTest 全數通過（150.02 秒，`build/p11-final-ctest.log`），含 51 live pose 場景、2598 Actor 樣本、19 special ID 核心測試、來源 AI／21 場 4v4、23 shader／37 固定模型與全部已註冊來源重建。

其後 benchmark 視窗的實際 framebuffer 尺寸檢查在量測前拒絕有邊框視窗；改成僅 benchmark 使用無邊框後，七圖各 120+600 影格均於 GTX 1650／NVIDIA 591.74／1920×1080／VSync off 完成。平均 301.8–484.9 FPS，CPU p95 2.58–4.26 ms，GPU p95 1.12–1.62 ms；4200 幀原始 CSV 與完整 metadata／hash 保留於 [效能紀錄](benchmarks/turf-2026-10-20/README.md)。這次視窗修正另以實際七圖量測及 CLI 檢查驗證，沒有重算或降低解析度。QA-12 仍未通過完整工作量。

### 2026-10-20 退役舊程式，restoration 成為唯一入口

使用者明確要求移除舊 OpenGL 程式、整理後連同 review.md 刪除一起 commit/push，並不分派 agent 驗證。本輪由主 agent 自行檢查，歷史遷移段落中「保留舊入口」的安排已被此決策取代。

普通啟動與 `--frames N` 現在直接進入 restoration 原生選圖，`--native` 保留為相容別名；明確地形／練習／Turf／benchmark 指令維持。刪除舊 engine／components／gameplay／splat／scene／gui／network 及未引用資產，將 NVIDIA 提示移至 restoration，移除 fmt 依賴。依使用者補充，GameNetworkingSockets 保留於 vcpkg 供後續多人連線使用，目前不連結／初始化；舊網路 manager／protocol 仍移除。共用模擬庫命名為 restoration_core；個人 CMakeUserPresets 留在本機並忽略。

本次是程式與建置整併，不增加玩法完成數。完整選單、音效、保存、網路、Zones／Boss、最終視效與 LOD 切換仍保持未完成；先前 benchmark 數據綁定前次建置，不冒稱本輪已重測最終效能。

本輪自行驗證：MSVC x64 Release 全目標建置成功；32/32 CTest 通過（179.06 秒，`build/cleanup-ctest.log`），包含新預設選圖開窗測試、11 項原生 CLI 規則、51 pose 場景、裝備／AI／GPU／來源重建。GameNetworkingSockets 宣告保留後重新設定／建置成功，Ninja 無額外工作；執行檔的 import 檢查不含 GameNetworkingSockets、fmt、OpenSSL 或 Protobuf。

另以僅含 Tiny-Splatoon.exe、glfw3.dll 與 assets/reference 的獨立資料夾，在該資料夾作為工作目錄執行預設選圖與直接 Turf，各三幀均正常結束；實際 renderer 是 GTX 1650 Max-Q／OpenGL 4.5／NVIDIA 591.74。Halyard 地形 PNG、60-step 練習 PNG 與 GPU 墨回讀亦通過；276 個來源基準雜湊未變更。發佈流程改用 executable 旁由 vcpkg 部署的 runtime DLL，不直接打包整個套件庫目錄。

依使用者要求，本輪未分派 agent 驗證；以上為主 agent 的建置／測試證據。批次刪除本機建置快取被工具政策拒絕，故保留 Git 忽略的既有快取，改用上述獨立資料夾檢查乾淨 runtime；來源程式／依賴／版本控制的清理已完成。
