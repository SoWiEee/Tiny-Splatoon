# 手動清理清單

盤點日期：2026-10-20。專案根目錄：`C:\Users\MintIce\Programming\Tiny-Splatoon`。

自動刪除被工具政策拒絕；以下尚未刪除。清理前關閉 Visual Studio，避免 IntelliSense 資料庫被占用。UI／音效目前仍使用 `build/` 建置，請不要刪掉整個 build。

## 優先清理

以下路徑皆相對專案根目錄。

| 項目 | 大小約 | 原因 |
| --- | ---: | --- |
| `.vs/Tiny-Splatoon/v17/ipch/` | 3,406 MiB | 舊 Visual Studio IntelliSense PCH，可重建；不是 Ninja 的編譯物件。 |
| `.vs/Tiny-Splatoon/v17/Browse.VC.db` | 121 MiB | 舊 Visual Studio 程式碼索引，可重建。 |
| `build-combat/` | 101.5 MiB | 舊 Debug 建置樹；目前使用 build 的 Release 與完整測試。 |
| `build/cleanup-package/` | 316.1 MiB | 前次清理的舊封裝快照；執行檔和資產均已有較新的目前版本。 |

根目錄以下 12 個手動編譯中間檔可刪除，合計約 12.7 MiB：

- [ ] `.pose_debug_eval.obj`
- [ ] `character_pose_eval.obj`
- [ ] `CharacterData.obj`
- [ ] `CharacterPose_charger.obj`
- [ ] `CharacterPose_throw.obj`
- [ ] `CharacterPose.obj`
- [ ] `GpuPreference.obj`
- [ ] `performance_capture_smoke.obj`
- [ ] `PerformanceCapture.obj`
- [ ] `vc140.pdb`
- [ ] `worldfx_eval.obj`
- [ ] `WorldFxRecipes.obj`

## 可選清理

- `build/pose-iterate/` 內的 `.exe`、`.obj`、`.pdb`、`.ilk`、`.lib`：約 143.3 MiB，舊 pose 診斷編譯產物。暫留其中 `.cpp`、`.mjs`、trace JSONL 和 log，避免混刪診斷來源。
- `output/performance_capture_smoke.exe`
- `output/performance_capture_smoke.exp`
- `output/performance_capture_smoke.lib`
- `output/PerformanceCapture.obj`
- `output/glfw3.dll`：只供上述舊 smoke executable 使用；**不要刪 build 根目錄的 DLL**。
- `.playwright-cli/`：舊瀏覽器 console/page 暫存記錄，約 1 KiB。
- `build/specials-iterate/`：空目錄。

## 請保留

- `build/Tiny-Splatoon.exe`、build 根目錄目前的測試 `.exe` 及 runtime `.dll`。
- `build/assets/`、`build/vcpkg_installed/`、`build/CMakeFiles/`、`build/CMakeCache.txt`、`build/build.ninja`、Ninja metadata 與 `CMakeUserPresets.json`。
- `build/player-oracle-runtime/`：仍用於來源對照測試。
- `build/checkpoint-*.log`、`build/checkpoint-*.json`／`.csv`、`build/captures/`，以及 output 中的 JSON／CSV／PNG 驗收資料。
- 目前使用的 `build/devbuild.cmd`、`build/checkpoint-verify.cmd`、`build/checkpoint-gpu.ps1`。
- `src/`、`tests/`、`tools/`、`assets/`、`spec.md`、`architecture.md`、`PRODUCT.md`、`DESIGN.md`、來源契約與驗收文件。
- `splat-ref/` 和 `docs/reference-baseline.sha256`：不可更動的參考來源與基準。

不建議只因副檔名是 `.json`、`.bin`、`.mesh`、`.lib` 就跨目錄刪除；目前的資產、字型、來源 fixture 與必要依賴也使用這些格式。
