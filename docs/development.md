# 開發與驗證

## 建置與執行

Windows x64、支援 OpenGL 4.5 的 GPU、Visual Studio C++ Build Tools、CMake 3.20+、Ninja、vcpkg。在 x64 Native Tools 終端執行：

```powershell
$env:VCPKG_ROOT = 'C:\path\to\vcpkg'
cmake --preset vcpkg -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
.\build\Tiny-Splatoon.exe
```

`assets/` 由獨立的 `Tiny-Splatoon-assets` target 部署到執行檔旁，一般 build 已包含此相依。純素材修改不會重新連結遊戲；新增／刪除素材會更新清單，刪除僅限先前成功部署所記錄的檔案，目錄中的其他本機檔案會保留。一般建置、遊玩與發布不需瀏覽器、Node.js 或 `splat-ref/`。玩家資料位於 `%LOCALAPPDATA%/Tiny-Splatoon/profile.json`，詳見[保存契約](save-player-flow.md)。字體授權位於 `assets/fonts/`。

Optimus 入口會請求 NVIDIA GPU；啟動紀錄的 `GPU` 行顯示實際 renderer，Windows／驅動偏好可能覆寫。低／中／高畫質可在選單設定；完整效能矩陣尚未驗收。

多人服務與房碼流程見[多人連線契約](contracts/network-source-contract.md)。建置遊戲目標也會建置同目錄的 `Tiny-Splatoon-Server.exe`；服務獨立於玩家程序，需使用相同服務位址才能查找房碼。單獨驗證網路流程：

```powershell
ctest --test-dir build -C Release --output-on-failure -R '^native_network_'
```

## 原生擷取與效能

```powershell
.\build\Tiny-Splatoon.exe --frames 3 --ui-capture build/captures/menu.png
.\build\Tiny-Splatoon.exe --reference-practice tidewater
.\build\Tiny-Splatoon.exe --reference-match tidewater --frames 180 --match-capture build/captures/turf.png
.\build\Tiny-Splatoon.exe --reference-zones tidewater
.\build\Tiny-Splatoon.exe --reference-boss tidewater --duration 240
.\tools\Measure-ReferenceTurf.ps1 -BuildDirectory .\build
```

`--native` 為預設入口別名。`--graphics-quality low|medium|high` 可用於直接對局／擷取；benchmark 報告記錄實際設定。Boss 時間可選 180／240／300 秒。舊 ImGui stage lab／控制器診斷頁及其專用腳本已退役，正式介面統一使用 RmlUi。

## 回歸與來源對照

```powershell
ctest --test-dir build -C Release --output-on-failure
```

若有 Node.js 22.17+，CMake 會加入固定 fixture oracle／CLI 檢查；World FX 與 LOD oracle 不需要 `splat-ref/`。參考樹存在時，另外註冊來源重匯出檢查；重建來源資料仍需符合[基準雜湊](reference-baseline.sha256)，不得改寫基準掩蓋差異。一般建置、執行及原生回歸已獨立，來源比對與退役條件見[待辦](next-round-backlog.md)。

GPU 測試、遊戲擷取與效能量測應依序執行，避免同時佔用 GPU。完整測試與視覺／聽感驗收的界線，見[還原進度](restoration-progress.md)、[最新驗收](acceptance/acceptance-2026-10-22.md)與[規格](../spec.md)。元件測試通過不代表全遊戲像素或音訊一致。


## 地圖互動與資產部署

- `GameUi::SetMapFrame` 一次提交地圖開關、跳躍目標／排程、世界及小地圖投影與借用紋理；`MapInteraction` 擁有圖釘、快取、選取驗證、layout 後命中判定與重開重設。`PlayerController` 仍判定彈射是否合法，`StageMapView` 仍擁有 GL 紋理。世界投影同步供準心、隊友與 Boss HUD 使用。
- 地圖的固定來源數學測試、真實 RmlUi 互動與紋理所有權測試保留；新增關閉／立即重開、移除／失效目標、過期游標確認與相同 MatchSimulation 重開的檢查。
- `cmake/RuntimeAssets.cmake` 宣告部署相依；`StageRuntimeAssets.cmake` 執行複製與已記錄檔案清理。設定與日常 build 命令不變，發布流程仍從執行檔旁的 assets 打包。CMake 3.20+ 用於依組態區分部署 stamp 與輸出路徑。
- `native_runtime_asset_staging` 使用隔離的小型可執行檔專案，檢查初次部署、無變更、修改／新增／大小寫改名／刪除、未知本機檔案保留、清單越界拒絕，以及素材變更前後執行檔雜湊／時間戳不變。此測試需要可用的 C 編譯環境；Windows 請在 x64 Native Tools 終端執行 CTest。

2026-10-24：地圖相關四項測試通過（21.82 秒），包含 720p／1080p 真實 UI 場景；資產部署回歸通過（9.28 秒），同一隔離專案亦在 Ninja Multi-Config 的 Release 組態通過。正式遊戲目標的素材新增／刪除／無變更建置確認執行檔雜湊及時間戳不變，GTX 1650 Max-Q 原生塗地場景啟動與擷取完成。完整未受影響套件未重跑，未量測 FPS 或完整建置加速幅度。


## 環境描述資料與對戰輸入

- `EnvironmentRenderer` 的 wave／cloud／PMREM 尺寸與位元深度取自既有 `textures.json`，解壓後長度及 `TSENV001` 標頭必須一致；foam 保留原本獨立的場地描述資料。來源二進位與描述檔未更動，SHA256 仍由來源匯出驗證負責，沒有宣稱 runtime 重新計算 SHA256。
- `src/data/Json.hpp` 重用原本玩家保存使用的嚴格 JSON／UTF-8 讀取器，供保存與環境兩個實際讀取端使用；保存格式、正規化與錯誤處理不變。
- `MatchInputState` 接收 GLFW 的一份輸入快照，集中預備／Playing／結算、暫停／恢復、失焦、地圖、滑鼠累積量與按鍵鎖存。主迴圈執行游標／音訊／時鐘效果；controller 仍負責移動、視角與合法彈射。切換互動狀態時清除手把視角濾波，避免恢復後殘留轉動。
- `native_match_input` 以連續快照驗證固定步長只消耗一次滑鼠／按鍵邊緣、預備不積存輸入、地圖抑制操作、按住／釋放副武器的恢復處理、特殊技能鎖存、失焦、練習裝備與線上開選單不中止模擬。
- 2026-10-24：環境 GPU／非法素材描述、玩家保存、玩家控制、輸入狀態及 UI 互動 5/5 通過（34.01 秒）；追加濾波與按住／釋放案例後，輸入／控制 2/2 通過（2.45 秒）。Release 遊戲與測試目標建置通過。原生練習場啟動已確認；Computer Use 因本機使用者按 Escape 停止，未將實機 Tab／Esc 操作列為通過。完整套件、實體手把與跨網操作未補跑。
