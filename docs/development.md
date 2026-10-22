# 開發與驗證

## 建置與執行

Windows x64、支援 OpenGL 4.5 的 GPU、Visual Studio C++ Build Tools、CMake、Ninja、vcpkg。在 x64 Native Tools 終端執行：

```powershell
$env:VCPKG_ROOT = 'C:\path\to\vcpkg'
cmake --preset vcpkg -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
.\build\Tiny-Splatoon.exe
```

`assets/` 會複製到執行檔旁。一般建置、遊玩與發布不需瀏覽器、Node.js 或 `splat-ref/`。玩家資料位於 `%LOCALAPPDATA%/Tiny-Splatoon/profile.json`，詳見[保存契約](save-player-flow.md)。字體授權位於 `assets/fonts/`。

Optimus 入口會請求 NVIDIA GPU；啟動紀錄的 `GPU` 行顯示實際 renderer，Windows／驅動偏好可能覆寫。低／中／高畫質可在選單設定；完整效能矩陣尚未驗收。

多人服務與房碼流程見[多人連線契約](network-source-contract.md)。建置遊戲目標也會建置同目錄的 `Tiny-Splatoon-Server.exe`；服務獨立於玩家程序，需使用相同服務位址才能查找房碼。單獨驗證網路流程：

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

若有 Node.js 22.17+，CMake 會加入來源 oracle／CLI 檢查。重建來源資料另需符合[基準雜湊](reference-baseline.sha256)的 `splat-ref/`；不得改寫基準掩蓋差異。參考資料夾尚未退役，部分測試仍讀取來源，移除門檻見[待辦](next-round-backlog.md)。

GPU 測試、遊戲擷取與效能量測應依序執行，避免同時佔用 GPU。完整測試與視覺／聽感驗收的界線，見[還原進度](restoration-progress.md)、[最新驗收](acceptance-2026-10-22.md)與[規格](../spec.md)。元件測試通過不代表全遊戲像素或音訊一致。
