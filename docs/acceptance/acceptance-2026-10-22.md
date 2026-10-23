# 音訊、潛水／道具特效與 HUD 驗收（2026-10-22）

本輪承接 v0.7.3（0427310），按使用者指定的三項工作進行。來源 `splat-ref/` 與 `docs/reference-baseline.sha256` 保持鎖定。以下記錄本輪實作與受影響回歸，未列出的全遊戲視聽差異不視為完成。

## 音訊

- 對照來源的結果 XP 動畫與 `xp_tick` 節奏，沿用 UI sound sink；重複繪製或設定獎勵不得重複結算玩家 XP。
- Boss 聲源使用同一次固定步姿勢運算的動畫 socket，保留缺少視覺姿勢時的合理 fallback；戰鬥 hit shapes 不因音訊擴充改變。
- 明確記錄仍有差異的混響／空間化與主觀聽感，不能以原生播放成功推論 Web Audio 逐樣本一致。

## 潛水與道具

- 分開驗證魷魚變形與實際進出墨水，檢查入墨／出墨演出、游泳墨面隆起、波紋與尾流。
- 正式地形 shader 必須收到游泳資料；暫停、離場與換圖的時鐘和狀態需正確清理。
- 裝置以來源生命週期為準；僅將有具體對照與驗證的修正列為完成，不宣稱全技能效果矩陣已通過。

## HUD／UI

- 對齊來源的時間、隊伍計數與目標資訊層級，保留簡約風格，減少遮擋遊戲視野。
- 放大次要資訊與選單文字，保留繁中、Noto Sans TC／Inter。
- 在 1280×720 與 1920×1080 檢查選單、Zones HUD 與結果；檢查文字裁切、重疊、操作流程與結果動畫。

## 舊診斷介面退役

- 依使用者指示移除舊 `PlayLoop`／stage lab、ImGui 初始化與套件相依，以及只服務舊入口的兩支 PowerShell 腳本。
- 保留最新原生練習、Turf／Boss／Zones、選單／對局擷取與 benchmark 工具；退役參數在建立視窗前明確拒絕。
- 更新 README、架構說明與 CLI 回歸。v0.7.3 發布標籤維持其已驗收內容，本輪後續變更另行提交。

## 整合結果

- 最終整合補驗 **12/12** 通過（32.16 秒），包含音訊、UI、預設入口／CLI、Boss 資產／姿勢／來源重建、World FX GPU／oracle／來源重建與 wake／轉換狀態。較早正式墨面尾流 GPU 補驗通過（9.00 秒）。
- MSVC x64 Release 全目標建置通過；vcpkg／CMake／src 不再引用 ImGui。
- 受影響檢查採分批執行：首批 19 項有 5 項失敗，均保留原驗收要求修正後補驗；未把首批結果寫成全數通過。v0.7.3 的完整 81/81 基準另見進度文件，本輪沒有宣稱重跑完整 83 項。
- Boss 嚴格姿勢 oracle 16/16 通過（3,925 更新／280 快照），新增 20 個 socket 的動畫座標亦比對；修正來源建構子對 body socket 的覆寫。
- 正式地形 GPU 檢查以相同墨水與 shader 時鐘，比較無游泳與七步持續移動尾流，保留原可見像素差門檻並通過。
- CPU wake 的最小半徑、暫停、過期與槽位重用通過；測試將仍在游泳的 Bot 與已過期槽位分開，避免將有效重用當成洩漏。
- World FX 嚴格 oracle 的 10 個來源配方通過。修正 ring 的 JS 雙精度運算順序；匯出器保存 JSON 負零符號，避免 JSON.stringify 抹除來源資料。來源檔案與基準未修改。
- 音訊測試通過 Boss socket 缺省路徑及延遲 Crablet／Barrage；fixture 等待毫秒量化後的 telegraph，並尊重實際音訊時鐘的重播間隔。
- UI 720p／1080p 的互動、XP 重入、字級、面板高度與小地圖元素排序通過。1080p 使用無邊框隱藏視窗並檢查真實 framebuffer，避免工作區裁切。
- 三張 README 畫面皆由 GTX 1650 Max-Q／OpenGL 4.5 的最新版原生執行檔擷取，1600×900；標題 3 幀、Turf 360 幀、Halyard Zones 480 幀。已檢視圖像並檢查 README 本地連結。

## 保留差異

- 入墨／出墨使用來源 ring fallback，正式 paint-ripple 回呼尚未接入；道具旋轉、次級動作與全武器／特殊技能的視效矩陣仍未完成。
- Boss impact／腳步／階段和暈眩事件時序、Barrage 隨機細節、XP 音高／音量、Zone hum 混響／空間化及主觀定位仍待來源對照。
- 線上多人、觸控／震動後端、跨 GPU 與最壞負載 QA 未完成。保留 splat-ref，沒有宣稱 100% 還原。

## 後續整合：道具預警、步行濺墨與復活彈射

- 全部 15 副武器與 19 特殊技能已建立[來源生命週期檢查表](../audits/device-special-lifecycle-audit.md)。表內明列哪些是來源程式核對、哪些有行為 fixture，哪些只有模型 GPU 檢查；不據此宣稱全裝備完整視效一致。
- 本輪修正黏彈預警、地雷動畫時鐘、Booyah 光暈與迴旋鏢接觸／引爆，補齊安放、預警、命中、破壞與實際 Bomb Rush 投擲事件。音訊測試逐事件隔離計數，包含零聲部的 Crab／Jetpack impact 與雙聲部的 Torpedo，保留來源 cue 的最小重播間隔。
- 步行不再使用爆炸殼或落地圓環 fallback；落地墨滴只寫 cosmetic Speck，ownership grid／計分版本維持不變。入墨／出墨、腳步與墨滴碰撞已接正式 paint-ripple，取代前述「保留差異」中的未接回呼。來源配方擴為 13 個，原容差與來源基準不變。
- Glow 的 source kinds 改走加色繪製，並完整還原呼叫端 RGB／alpha blend 狀態。負向控制把加色暫改一般 alpha 時，GPU 檢查確實失敗；恢復正確程式後通過。正式地形 GPU 檢查亦驗證相同墨水／時鐘下的 transient ripple 可見像素差。
- 彈射地圖採按住 Tab／M；有效／失效隊友、訊標、生點、陣亡預選／清除／復活發射及訊標落地扣次數均有控制器／UI 檢查。README 與遊戲操作說明同步更新。
- MSVC x64 Release 全目標建置通過。CPU／來源檢查 **28/28** 通過（80.00 秒，三項並行）；GPU／UI／音訊／正式入口 **9/9** 通過（55.71 秒，依序執行）。紀錄在 `build/acceptance-device-respawn-cpu.log` 與 `build/acceptance-device-respawn-gpu-audio.log`。來源檔案與 `reference-baseline.sha256` 未變更，未重跑未受影響的全 83 項或重新匯出音樂／貼圖。
- 人工看圖另發現 map marker 陰影合成會造成原點殘影與白色方塊；移除標記／本機 Actor 的裝飾陰影後，UI 互動補驗 **1/1** 通過。新測試同時檢查實際 framebuffer 中的綠色標記、白色數字、原點無殘影及標記座標。已檢視 `build/captures/ui/jump-map-1280x720.png` 與 `jump-map-1920x1080.png`，三名隊友、生點與訊標均正常顯示；面板改為不透明，避免背後計分穿透。

上述檢查點仍保留的 Shaker／Tracer／Wail／ZoneHum 音訊差異，於下節補驗；Boss 細分事件、其他距離 gating，以及檢查表內的材質／動作與完整生命週期圖像差異仍保留。

## 後續整合：充能、尾跡、持續音與即時混響

- Shaker 使用實際玩法充能狀態控制本機 fizz，50%／100% 跨門檻發出 clink，死亡中斷由模擬產生 fizzle。測試涵蓋音訊尚未觀察到手持狀態就死亡，以及暫停／恢復／清場。
- Tracer 直接與尾跡命中分開路由；尾跡僅在持有者或受擊者為本機時發聲，直接命中另保留 30 m 相機距離條件。本機受擊聲不定位，其餘尾跡使用最近接觸點與來源音量／音高。
- Wail 每座喇叭有獨立且不重用的 ID，charge→blast 啟動 loop，blast→fade／死亡清理採 0.3 s 淡出；快照補齊無事件清場及暫停恢復。兩座同一持有者喇叭不互相覆蓋。
- ZoneHum 接入前置於乾聲空間化的 send 和共用 stereo plaza return；來源 impulse／normalize／距離 send 及原生 music riser 的 wet tail 已接入。已烘焙處理的音訊資產不再額外混響。完整瀏覽器壓縮器、HRTF 與主觀定位仍未宣稱一致。
- 獨立 Web Audio fixtures 已重新唯讀渲染檢查；原生 raw IR 容差 1e-7、wet 容差 3e-6，不允許位移。乾／濕分流、零 send、非有限輸入、重置、重複清理、256／480-frame quantum 與不規則讀取通過。480-frame graph 渲染 73,023 frames 花 0.0347 秒；這是離線 convolution 校準，不能代表完整遊戲 FPS 或音訊裝置的最壞 callback 延遲。
- MSVC Release 全目標建置通過；受影響玩法／來源資產回歸 **17/17**（44.61 秒）、GPU／原生入口 **3/3**（15.07 秒）、WeaponRunner **1/1**（2.75 秒）、音訊引擎 **1/1**（28.29 秒）、原生混響 **1/1**（2.40 秒）及來源混響 fixture 唯讀檢查通過。整合時修正節點重複 uninit 與測試把模擬時間當音效播放時間的問題；沒有放寬比對容差。
- 紀錄：`build/acceptance-audio-finish-regression.log`、`build/acceptance-audio-finish-render-entry.log`、`build/acceptance-audio-finish-engine.log`；混響 fixture 來源 hash 與匯出指令見[fixture 契約](../../tests/fixtures/audio-reverb-source.md)。未重跑完整測試集或整庫音樂重新合成；參考來源及基準未修改。

## 多人整合（2026-10-22／23）

- 原生 GameNetworkingSockets 房間服務、五碼房碼、八人房間、隊伍／準備／房主設定、載入同步與同房再戰已接入正式入口。房主離線後由仍在線的成員接手；一般模式接管離線角色為 AI，Cargo 移除對應有效槽位。
- 真實 GNS 整合涵蓋移動、墨水格、命中去重、狀態／推力、訊標、超級跳躍、投射物呈現、Zones 計數／罰則、Boss 狀態與換主。八角色持續射擊、35 ms 延遲、15% 重排／複製、過期 epoch 與偽造 owner 拒絕均有測試；這些是本機故障注入，並非跨網際網路驗收。
- MSVC Release 全目標建置通過。房間三／八人及獨立伺服器程序檢查通過；最後心跳 fixture 原先使用非法房碼，改用合法字元後二人房與 160 秒虛擬心跳／150 秒靜默逾時通過（4.44 秒）。紀錄：`build/acceptance-network-room-final.log`、`build/acceptance-network-heartbeat-final.log`。
- 受影響玩法第一批 28/30 通過；兩項失敗指出離線裝置 getter 改回傳複本而破壞既有穩定參照。修正共用 getter 後，控制器／裝置、二／三／八人傳輸與完整 GNS 同步補驗 6/6 通過（20.09 秒）。GPU／UI／音訊與正式入口 8/8 通過（64.76 秒）；房間 UI／authority 另 2/2 通過。紀錄：`build/acceptance-network-gameplay.log`、`build/acceptance-network-supplement.log`、`build/acceptance-network-gpu-audio.log`、`build/acceptance-network-ui-authority.log`。
- 已檢視 1280×720 與 1920×1080 房間截圖，八人清單可捲動且準備／開始按鈕可見。Inter 的字體風格提示依既定字體選擇保留。參考來源與 `reference-baseline.sha256` 未修改。
- 保留範圍：公開伺服器／NAT 穿透、實機跨網路與長時間負載尚未驗收；來源 3D 大廳互動尚未全量遷移。同步呈現每位擁有者有 24 投射物／12 裝置／12 特殊物件與每裝置 64 個尾跡點上限；可靠墨水與命中不使用此呈現預算。未宣稱完整測試集或 NET 全項通過。
