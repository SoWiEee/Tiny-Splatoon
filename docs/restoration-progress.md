# OpenGL 還原進度

基準：2026-10-17，依 [spec.md](../spec.md) 與[來源雜湊](reference-baseline.sha256)。以下按階段記錄已完成且有證據的範圍。✅ 只代表列出的實作與檢查通過，不代表整項規格或全遊戲 100% 驗收。

## 第一階段：來源資料、地形與墨水基礎（2026-10-18）

- ✅ 從鎖定來源匯出 config、layout、dressing／placement，以及 23 個合法地圖／模式的標準化幾何。原生幾何以 double 對照 JavaScript Number；851 筆地面高度、920 筆射線與道具碰撞查詢通過（當時 CTest 3/3）。
- ✅ 建立 face-space CPU 墨格、dead cells、seeded splat、coverage 與 atlas packing；兩張地圖的格點對照通過。CPU ownership grid 是計分資料，後續 GPU 墨材質另行呈現。
- ✅ 八張 Turf 地圖完成 1600×900 診斷 PNG 擷取，renderer 紀錄為 GTX 1650 Max-Q／OpenGL 4.5。此時的擷取只驗證地形與診斷流程，未作視覺一致性或 FPS 驗收。

## 第二階段：離線練習與玩家模擬（2026-10-18）

- ✅ 七張離線地圖的固定 60 Hz 練習、移動／跳躍／射擊／下潛、相機與暫停流程接入。350 個來源 Actor 取樣、20 個 CameraRig／Physics 連續影格，以及控制器幾何樣本通過（當時 CTest 6/6）。
- ✅ 七圖各跑 240 步並擷取 PNG；GPU ownership 貼圖的射擊／清墨／敵隊重塗三次回讀與 CPU sample 相符。此階段仍使用邏輯格顯示，不等於後來的連續 GPU 墨材質。

## 第三階段：角色、裝備、Turf AI 與模式核心（2026-10-19）

- ✅ 匯出 128 組 rig、411 model、558 共用 mesh；23 組 shader 與 37 個來源／原生固定 panel 通過門檻，最小 silhouette IoU 0.998737、最大前景 RGB MAE 0.688/255。這是固定畫面對照，完整遊戲視覺另驗。
- ✅ 12 主武器、15 副武器、19 特殊技能的資料與核心接點逐步接入；19 個特殊技能 ID 最終均有原生核心測試。來源 Actor、runner、kit、特殊時序與 Match setup／Judge fixture 覆蓋各自列出的案例，未將其推論成所有視效／模式互動完成。
- ✅ 七圖導航／21 路徑、來源 Bot intent／aim、七圖 × 三 seed 的 4v4 回歸通過；來源初始化名單與共用 gameplay RNG 對照通過。這些紀錄對應 Turf，Zones、Boss 與線上策略各有自己的驗收範圍。
- ✅ 後續修正 Bot 卡住、Actor busy gate 與出墨緩衝後，當時已註冊的 23 項 CTest 以兩批全數通過；完整 live pose 的早期差異未計入這個數字。

## 第四階段：原生入口、動態姿勢與渲染（2026-10-20）

- ✅ restoration 程式按職責移到 src/app、src/game、src/world、src/render、src/gui；舊 OpenGL 入口退役，普通啟動進入原生選圖。搬移與清理後各有 Release 建置及當時完整 CTest 回歸。GameNetworkingSockets 保留依賴供未來連線，目前未初始化網路。
- ✅ 角色 live pose 的 32 個基本與 19 個外觀／形態／LOD 代表場景以原 0.001 門檻通過，含骨骼、skin matrix、126 channels、head／muzzle 與零件 transform；當時 Release CTest 31/31 通過。來源 LOD 的 2,288 次更新、互補 dither 與 GPU 前／中／後影格檢查通過。代表場景不等於所有動畫狀態的窮舉驗收。
- ✅ HDR／陰影／後製、Screen FX、World FX 與 BossNav 的來源 oracle 及 GPU 元件檢查通過；當時 Release CTest 46/46 通過，後加的近鏡 World FX GPU 項目另行通過。來源與限制見[渲染契約](render-pipeline-source-contract.md)。
- ✅ GTX 1650／1920×1080／VSync off 的七圖 Turf 固定負載量測完成，見[效能紀錄](benchmarks/turf-2026-10-20/README.md)；這批負載未涵蓋全部後續環境與最壞視效，不能作為完整 QA-12。
- 歷史檢查點：Boss 當時只有 rest rig，嚴格姿勢 oracle **0/16**，見[當時失敗報告](boss-pose-checkpoint-report.json)。這不是目前狀態；後續已修正並通過 16/16。

## 第五階段：場景、連續墨水、投射物、音訊與 Boss（2026-10-21）

- ✅ 場景接入來源 bevel、PropKit 幾何、46 層 albedo／normal／ORM 與 Canvas 招牌等貼圖，共 23 個合法地圖／模式 pack、14 個壓縮貼圖 pack；來源重建與尺寸／內容／雜湊檢查通過。詳見[場景契約](stage-visual-source-contract.md)。
- ✅ 來源 GPU paint shader 的連續墨水外觀接入遊戲，CPU ownership grid 繼續負責玩法與計分；濕度衰減／覆蓋保留及 GPU 可見像素檢查通過。
- ✅ 墨滴變形／衛星滴、弓箭、拳套、刀波、Charger ribbon 與 live rig 槍口接入；24 組真實 JavaScript _draw fixture 和 GPU 像素檢查通過，詳見[投射物契約](projectile-source-contract.md)。
- ✅ 來源音訊資料擴為 201 個 cue、31 組 loop、九首 FLAC，接入角色／世界／對局／Boss；原生播放、轉場與清理測試通過。來源重建後的 manifest hash 問題已修正並作資產／索引及比對器檢查，完整渲染指令未在該次修正後重跑；詳見[音訊契約](audio-source-contract.md)。
- ✅ Boss 動態姿勢修正 rig 雙精度匯出及 Float32 讀取後的混合運算，原 0.001 容差 oracle **16/16**、3,925 次更新／280 份完整快照通過；材質與受擊 GPU 項目亦通過。此結果取代第四階段的 0/16 歷史狀態，完整 Boss 視聽相似度仍待獨立驗收。
- ✅ 此整合點 Release 全目標建置、CTest **61/61** 通過；GTX 1650 完成實際 Turf／Boss 擷取。當時的遠距 overview 效能見[量測紀錄](benchmarks/source-scene-2026-10-20/README.md)，不能代表近鏡最壞負載。

## 第六階段：保存、玩家流程與繁中 UI（2026-10-21）

- ✅ 玩家 profile／設定採原子保存，裝備／外觀／設定重開還原、一次性 XP 結算接入；保存範圍及失敗保護見[玩家流程](save-player-flow.md)。保存實作已提交於 2ee0241。
- ✅ 標題／首頁、置物櫃、3D 換裝預覽、練習、離場確認、四人結果頒獎台、操作說明與製作名單接入；色盤／色覺輔助、控制器設定和繁中名稱表接入。UI 保留 Inter／Noto Sans TC，名稱依[翻譯表](ui-translations.md)。
- ✅ 當時 MSVC Release 全目標建置、CTest **73/73** 通過；繁中讀寫錯誤前綴更新後 UI 互動測試補驗 1/1。GTX 1650 原生標題與 120 幀練習入口擷取成功。

## 第七階段：環境、裝置、畫質與玩家視角（2026-10-21）

- ✅ 來源環境／動態遠景、29 類雙隊裝置、Tracer 光帶、角色連續下潛／回人形及裝置／蓄力音訊接入；低／中／高畫質與陰影／反射／後製選項接入。來源資產與材質回歸、GPU 截圖及六組玩家視角效能量測見[本輪驗收](acceptance-2026-10-21.md)。
- 待改善：GTX 1650 的中／高畫質未達 60 FPS；玩家認為目前下潛視效仍不像來源。畫質、主觀視覺及音訊差異留在[下一輪候選](next-round-backlog.md)。

## 第八階段：離線 Zones 與穩定性（2026-10-21）

- ✅ 七張離線地圖接入 Zones 規則、控制區幾何、計時／逆轉／延長與結算；固定 60 Hz 的 native_zone_matches **9 場通過**，涵蓋七圖 normal 與 Tidewater easy／hard（28.36 秒）。控制幾何／規則、AI 與 zone_hum 的單項檢查亦通過。
- ✅ Zones AI、retake、GUI／HUD、SourceZoneMarks 場地標記、flood／wipe 視效、連續 zone_hum 所有權音色／張力及 Zone／XP 事件接線完成。XP 採保存流程的一次性結算，線上 follower 同步仍屬網路待辦。
- ✅ 修正 PlayerSimulation 來源非有限狀態復原與 NaN 傷害過濾；受影響長局已通過。Release build、nativeZoneGPU 與 UI／audio 單項檢查通過；GTX 1650 原生 Zones 180 幀截圖已擷取。
- ✅ MSVC Release 全目標建置與完整 CTest **81/81** 通過（319.20 秒），涵蓋保存、UI、音訊、Zones、GPU 與來源匯出回歸；此為 v0.7.3 提交驗收。

## 第九階段：潛水、音訊、介面與診斷退役（v0.7.5）

- ✅ 正式墨面 shader 接入來源游泳尾流、隆起與波峰，加入進出墨水／變形的獨立轉換及游泳水花；固定 60 Hz 角色姿勢沿用已驗收實作。
- ✅ Boss 命名 socket 擴為 20 個，來源覆寫與動畫座標經嚴格 16/16 姿勢 oracle 對照；原有十個戰鬥碰撞點不變。部分開場／招式音效補回來源延遲，結果 XP 加入 65 ms 節奏音效。
- ✅ HUD／選單／結果放大文字，Zones 面板更緊湊，小地圖移除開發座標；720p／1080p 互動、字級與版面檢查通過。
- ✅ 依使用者指示移除舊 ImGui 診斷頁、相依與兩支專用腳本；保留最新原生練習、三模式、擷取及 benchmark 入口，CLI／預設入口回歸通過。
- ✅ README 改為置中標頭、技術 badges、Old Demo 與六個簡潔章節；三張 GTX 1650／1600×900 真實畫面已擷取並檢視，本地連結檢查通過。
- ✅ 最終整合補驗 **12/12** 通過（32.16 秒），正式墨面尾流 GPU 補驗另通過（9.00 秒）。來源粒子十個配方的嚴格 oracle 通過，來源 JSON 負零序列化與 C++／JS 運算精度差異已修正；受影響檢查以分批補驗記錄，並未宣稱重跑全部 83 項。細節與剩餘差異見 [2026-10-22 驗收](acceptance-2026-10-22.md)。以上不表示所有道具視效與音訊差異已完成。
## 尚未完成的驗收與工作

- 線上房間／同步／斷線與換主等 NET-01–12 尚未移植；GameNetworkingSockets 依賴存在不等於連線可用。
- 固定來源畫面、完整 Boss hazard／live crablet 視覺、主觀音訊、跨 GPU／品質及最壞視效負載仍待驗收；QA-12 尚未完成。實體手把手感、觸控與震動後端也尚未驗收。
- splat-ref 仍是來源對照依據；原生遊戲執行已獨立，但測試／fixture 與來源退役條件見[下一輪候選](next-round-backlog.md)。