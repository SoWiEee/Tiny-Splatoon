# 場景、環境與渲染契約

原生場景使用已提交的地圖幾何、PropKit 道具、來源材質與環境資產。遊戲執行讀取原生資產；`splat-ref/` 不參與執行。測試證據及驗收邊界集中於[還原進度](../restoration-progress.md)。

## 地圖與材質

八張地圖共有 23 個合法模式幾何包。來源 bevel、PropKit 網格與 placement 轉為原生資產；場景貼圖以 14 個壓縮資料包保存，包含 46 層 albedo、normal、ORM 貼圖、PropKit 圖集、Canvas 紋理與地圖壁畫。角色、場景及裝置共用載入後的 mesh、材質 shader 與 DFG 查找表。地面材質已移除一直停用的舊 baked AO 分支、light UV 介面與貼圖綁定，保留現有 clearcoat、墨水濕度、微表面、泳姿尾流及 ripple；二進位網格格式與欄位偏移保持相容。

## 環境

環境資料提供 day、sunset、golden 三種光照主題、來源天空與海面 shader、雲、霧、波浪、泡沫、遠景及日光。Halyard Marina 與 Lockgate Canals 使用港區水面和反射，其餘地圖使用開放海面。遠景與港區活動物件依匯出的場景資料更新；渲染器提供來源光照給場景、角色、投射物、裝置及 Boss。

## 墨水與畫面管線

- `FacePaint` 的 CPU 格子是遊戲塗地、計分、隊色歸屬和模式規則的權威資料。正式 4096×4096 GPU 連續墨圖集負責 splat 展開、濕度衰減、保留乾燥後覆蓋及可見表面細節。GPU 回讀只供診斷，不作玩法狀態。
- 場景在半浮點 HDR 目標繪製，完成後依序進行可選 GTAO、bloom、色彩分級、傷害／特殊 Screen FX、Neutral tone mapping 及 sRGB 輸出；GUI 在後製管線外繪製。方向陰影使用來源範圍、偏移與 PCF 設定。
- GTAO 與空間降噪各使用內部場景寬、高的向上取整一半；完整解析度深度以 nearest 取樣，四點深度加權上採樣後合成至 HDR。降噪半徑由 6 改為 3，以保留近似原有畫面範圍。天空與找不到相近深度的細前景回退為無遮蔽，避免背景 AO 滲入物件輪廓；此空間方案未加入時間累積。
- 低／中／高畫質與陰影、反射、後製及反鋸齒設定接入原生 UI；設定只改變呈現成本，不改玩法判定。視窗大小或取樣數變更時重建相依 GPU 目標。
- 泳姿尾流、浮出／入墨、腳步飛沫及表面 ripple 以正式場景 shader 呈現。Transient ripple 沿用來源 24 槽優先級與到期／重用規則。Cosmetic speck 與 ripple 不得新增計分格或重複傷害。

目前畫質預設為低／中／高：低使用 65% 內部解析度並關閉陰影、反射及後製；中使用 85%、1024 陰影與後製，關閉反射；高使用完整解析度、2048 陰影、港區反射及後製。HUD 仍以視窗解析度繪製。個別畫質選項在 UI 可覆寫；畫質只改變呈現，不改玩法數值。


## 本機效能檢查點，2026-10-26

GTX 1650 Max-Q、NVIDIA 591.74、1920×1080、Tidewater、Medium、player camera、seed 1；固定 60 Hz 模擬、關閉垂直同步，暖身 120 frame 後量測 240 frame。最後一組相鄰執行結果如下：

| 指標 | 原 AO 管線 | 半解析度 AO 與最終材質清理 |
| --- | --- | --- |
| GPU 平均時間 | 20.91 ms | 14.98 ms |
| GPU p95 | 24.88 ms | 18.50 ms |
| 平均 FPS | 47.69 | 66.62 |

另一次隔離 AO 的短測為 20.00 → 13.49 ms、49.91 → 73.81 FPS。RenderDoc 同場景 frame 180 的暖機回放中，GTAO 約 3.60 → 0.74 ms，降噪約 3.76 → 1.08 ms，新的深度上採樣約 0.33 ms；AO 目標由 1632×918 改為 816×459。回放 counter 用於定位 pass 成本，不能當作遊戲 FPS。

Windows Release 受影響目標建置成功；pipeline、stage visual、stage export、environment、Zones visual、UI interactions 共 6/6 回歸通過（30.03 秒）。AO GPU 數值檢查涵蓋深度邊界、細前景回退、天空、奇數尺寸及 1×1；六張乾／濕墨、overview、泳姿尾流與 ripple 固定畫面在最終材質清理前後逐像素相同。嘗試跳過零貢獻 clearcoat 及無墨區域計算的分支未顯示穩定加速，已撤回。

以上為本機短測，受筆電溫度、時脈及地圖刷新影響；收益主要來自 AO，未證明材質清理有獨立 FPS 收益。半解析度 AO 仍可能犧牲細小接觸陰影；跨地圖、長時間鏡頭運動、跨 GPU 與 Linux 本輪未補驗。

## 陰影成本與優化檢查點，2026-10-26

同一份 Tidewater 擷取含 107 筆陰影繪製、約 260 萬個索引。地圖主網格的投影只有 22,380 個索引，卻覆蓋約 74 萬個 fragment；原本要先算完地面貼圖、程序細節及濕墨，才到共用材質的 depth-only return。個別只有 0–2 個 fragment 的繪製在 RenderDoc 重播被歸因 1–3 ms，且多 counter／單 counter 回放結果不一致，因此不以這些單筆或總和當作遊戲陰影成本。

地圖 shader 現在於 see-through／LOD 覆蓋判斷後提早結束 opaque depth-only 路徑，跳過地面與墨水材質計算。`GRATE` 保留完整孔洞／貼圖遮罩路徑；角色、裝置、投射物與其他自訂 alpha 材質的共用 return 位置不變。陰影解析度、投影範圍、PCF、caster 集合及逐幀更新均未縮減。

正式 benchmark 以 `GL_TIMESTAMP` 在陰影 pass 前後記錄，僅在 benchmark 啟用，完成擷取後回讀；JSON 新增 `shadow_gpu_ms_mean`／`shadow_gpu_ms_p95`，CSV 新增 `shadow_gpu_ms`。關閉陰影時回報 0；保留既有整幀 elapsed query。以下使用同一執行檔與計時器，只交換原／優化地圖 shader；GTX 1650 Max-Q、1920×1080、Tidewater、player camera、seed 1、120 frame 暖身及 240 frame 量測：

| 畫質 | 陰影平均，原 → 優化 | 陰影 p95，原 → 優化 | 整幀 GPU 平均，原 → 優化 | FPS，原 → 優化 |
| --- | --- | --- | --- | --- |
| Medium，1024 陰影 | 2.00 → 1.44 ms | 2.39 → 1.63 ms | 13.42 → 13.20 ms | 74.21 → 75.40 |
| High，2048 陰影 | 3.75 → 1.87 ms | 4.19 → 2.31 ms | 19.97 → 19.35 ms | 50.02 → 51.59 |

RenderDoc 同 capture 的 shader 替換比較中，地圖主網格投影約 0.67 → 0.16 ms，完整 1024×1024 D32F 陰影圖的 4,194,304 bytes 相同。六張既有材質固定畫面逐像素相同；新增 256×256 地圖陰影覆蓋與同動畫時間下乾地／濕墨的完整深度相等檢查。Windows Release 建置、stage visual、Zones visual、stage export 共 3/3 通過；benchmark 的 JSON／CSV 均檢查陰影時間範圍與平均值一致，另驗證低畫質關閉陰影回報 0。

此項只省去投影階段的材質計算，並未降低所有接收陰影材質的取樣成本。剩餘 caster 幾何、skin／姿勢及 draw submission 仍需成本；未引入靜態陰影快取、降低角色 LOD 或減少 caster。整幀短測受溫度與時脈影響，不能把陰影 pass 的下降比例當作全遊戲 FPS 增幅；未補驗全地圖長局、跨 GPU／平台或 GRATE 實體場景矩陣。
