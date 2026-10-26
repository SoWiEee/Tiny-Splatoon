# Tiny Splatoon v0.8.8

## 更新內容

- GTAO 與降噪改為半解析度計算，以完整解析度深度導引上採樣；補上細前景、深度邊界、天空及極小尺寸的 GPU 檢查。
- 地圖陰影於覆蓋判斷後提前結束 opaque depth-only 路徑，省去地面／濕墨材質運算；格柵保留原遮罩，陰影尺寸、範圍、caster 及更新頻率不變。
- 精簡停用的地面 baked AO、light UV 與未使用中間值；benchmark 新增陰影 pass 的 GPU 平均、p95 及逐幀時間。
- 收入 v0.8.7 之後的空中墨水 VFX：連射槍局部 SDF、撞擊水花、Spinner 連續 Tube，以及潑桶／滾筒／筆刷分層墨滴與次級飛濺。保留原有傷害、耗墨及 RT 塗地判定。
- 投射物準備結果由色彩與陰影 pass 共用；拆分對戰 Session、HUD 呈現資料及 CombatWorld 內部責任，整理共用型別與建置依賴。
- 資產改按 models／world／effects 分類，整合規格、領域契約與還原進度，清除停用的來源匯出工具。
- 一併提交 README 參考連結及 Tracy 本機工具目錄的忽略規則。

## 本機效能紀錄

GTX 1650 Max-Q、1920×1080、Tidewater、固定 seed、player camera；暖身 120 frame 後量測 240 frame：

- AO／降噪優化的相鄰 Medium 短測，整幀 GPU 平均 20.91 → 14.98 ms，FPS 47.69 → 66.62。
- 再獨立比較地圖陰影優化：Medium 陰影平均 2.00 → 1.44 ms，High 3.75 → 1.87 ms；該組整體 FPS 分別約 74.21 → 75.40、50.02 → 51.59。
- 上述為不同階段的本機比較，受溫度、時脈與場景影響，不能相乘或當作跨地圖／硬體的效能承諾。

## 安裝

Windows：完整解壓 Tiny-Splatoon-v0.8.8-windows-x64.zip，執行 Tiny-Splatoon.exe。

Linux：解壓 Tiny-Splatoon-v0.8.8-linux-x64.tar.gz，執行 ./Tiny-Splatoon-linux-x64/Tiny-Splatoon。目標為 Ubuntu 22.04 或更新的 x64 系統，需 OpenGL 4.5 core 驅動及系統圖形／音訊執行庫；保留 lib 與 assets 目錄，詳見 README 與開發指引。

玩家資料格式保持相容，一般遊玩不需要 splat-ref/。Windows／Linux 安裝包由版本標籤的發布流程產生。

## 驗收界線

Windows Release 受影響目標建置成功；AO／材質的 6 項針對性回歸與陰影的 3 項回歸通過。固定場景的完整 1024×1024 陰影深度圖在 shader 替換前後逐位元一致，六張材質畫面逐像素相同；另有乾地／濕墨投影深度不變及低畫質關閉陰影的檢查。

本次依要求未執行 Linux 實機或 WSL 驗證，也未重跑現有全套測試或完整跨地圖／裝備／網路矩陣。半解析度 AO 可能減少細小接觸陰影；新增空中墨水 VFX 不宣稱與商業版逐像素一致。詳細證據與限制見 docs/restoration-progress.md 及 docs/contracts/rendering-stage-environment.md。
