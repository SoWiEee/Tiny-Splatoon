# 環境、裝置、角色動作與音訊整合驗收

本輪依 `spec.md` 延續環境／光照及裝置美術，追加使用者要求的潛水／潑墨動畫確認與音訊細節驗收。來源 `splat-ref/` 及 `docs/reference-baseline.sha256` 保持不變。此紀錄區分已執行的檢查與尚未達成的全專案完成門檻。

## 角色與墨水

- 實際遊戲將 squid／submerged／climbing 狀態映射至 Kid／Squid／Swim／Climb，武器事件在同一模擬步的 pose 更新前送入。
- 原版 Character.update 的來源 fixture 重建通過；新增 envelope 斷言確認潛水、游泳、爬牆使用魷魚模型，roller／brush／bucket／blade 的潑墨動作有持續骨架變化。
- `reference_character_art` 另外連續執行人形→魷魚→游泳→爬牆→人形，檢查模型可見性、材質顏色與像素，並輸出 `build/captures/character/live-dive-return.png`。主 agent 已檢視該畫面。
- 墨水既有測試涵蓋 atlas 塗色、濕乾變化、擴散／下滴與來源粒子；角色姿勢、投射物與視效各自有來源 oracle。這些是動作／數值與繪製證據，不等同任意整場畫面的像素一致。

## 環境與裝置

天空、日照、三套主題、雲、海面、泡沫、水線、遠景與港區反射改用來源幾何／shader／資料。PMREM 與霧共用於場景、角色、Boss、裝置及投射物。場景布條、旋轉道具、燈光及遠景船鳥持續更新。

Halyard 與 Lockgate 的泡沫包含實際 PropKit 水線輪廓；超過來源矩形 uniform 容量的地圖使用來源距離場 shader 在原生 GPU 烘焙。反射、距離場烘焙與資源載入會還原呼叫端 GL 狀態。

裝置目錄包含 29 類／雙隊 58 個來源模型及材質，另補 Tracer 光帶與結束後壽命。Jetpack 替換墨槽時同步隱藏舊墨槽的顏色及陰影，Crab／Waddle 以實際移動速度驅動動作。來源與保留差異見 [環境契約](../contracts/environment-source-contract.md)、[裝置契約](../contracts/device-visual-source-contract.md)、[場景契約](../contracts/stage-visual-source-contract.md)。

## 畫質選擇

使用者追加要求的低／中／高預設及陰影／反射／後製開關已接至 RmlUi 設定與實際 renderer。預設中；內部解析度縮放不縮小 HUD／文字，關閉陰影會略過其繪製，關閉反射會略過港區反射及遠景 cubemap 烘焙，關閉後製會略過 AO／bloom／screen FX。基本 HDR 色彩輸出與遊戲數值保持一致。個別修改顯示自訂，設定在下一場對局套用並保留於本次執行期間。畫質設定的落盤保存仍屬未完成的 SAVE 範圍。

## 全專案完成邊界

`spec.md` §15.5 的全專案 100% 門檻仍包含 Zones、線上、保存／完整玩家流程及跨網矩陣，不能由本輪的元件／離線回歸代替。主觀聽感、全場景瀏覽器像素比對與最壞特殊視效／品質矩陣需獨立證據；效能報告保留 `qa12_claim=false`。

本輪不分派提交前驗證 agent，由主 agent 執行整合建置、CTest、畫面檢視與量測；實作 agent 僅處理各自的來源資產及程式範圍。

## 最終執行結果

- MSVC x64 Release 全目標建置成功。68 項 CTest 的整批執行為67通過／1失敗（296.99秒）；唯一失敗為音訊測試混入尚未播完的一次性開場音。修正測試的量測區間後，嚴格循環音斷言補驗1/1通過（13.61秒）。未重複跑未受影響的67項。
- 隨後修正裝置與環境材質的內容索引：索引包含 uniform 預設值，避免相同 shader 程式覆蓋橘／藍隊材質參數。裝置測試會放大小型道具、檢查炸彈隊色及六類模型的兩隊像素差異。受影響的環境／裝置 GPU、來源重建與原生入口六項回歸全數通過（27.16秒）。
- 最終八圖環境測試量到287,046個動態差異像素；58個裝置模型共411,979個可見像素，Tracer光帶473個像素，並驗證結束後壽命與清場。均使用GTX 1650；這些是功能及可見性檢查，不是全畫面像素一致的宣告。
- `node tools/export-audio-source.mjs --check` 最終單次完整執行exit 0：201音效、31循環音床、9首配樂及manifest／hash／frame／loop point／registry檢查通過。原版menu重複渲染的校準及錯開一個frame的負向控制另有紀錄；未重新替換WAV／FLAC。見[音訊報告](audio-acceptance-2026-10-21.json)與[來源重複性報告](audio-menu-repeatability-2026-10-21.json)。
- 1080p玩家跟隨視角、原生HUD、八名actor、六組低／中／高Turf與Boss量測已記錄CPU／GPU／driver、p95、記憶體、draw與particle數。低畫質兩個案例均超過60FPS；中／高未達標，QA-12保持未完成。詳見[原始報告與表格](../benchmarks/environment-2026-10-21/README.md)。
- 已檢視設定頁、連續潛水／回人形、一般Turf、Halyard港區、Boss及雙隊裝置截圖。來源基準檔未變。依使用者追加要求，本輪提交並推送，透過既有 Windows 發布流程建立 v0.7.0 與遊戲 ZIP。
