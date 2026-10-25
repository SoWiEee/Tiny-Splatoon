# 開發與驗證

## Windows 建置

需要 Windows x64、Visual Studio C++ Build Tools、CMake 3.20+、Ninja、vcpkg 與支援 OpenGL 4.5 的 GPU。在 x64 Native Tools 終端執行：

```powershell
$env:VCPKG_ROOT = 'C:\path\to\vcpkg'
cmake --preset vcpkg -DCMAKE_BUILD_TYPE=Release
cmake --build --preset vcpkg --parallel 4
.\build\Tiny-Splatoon.exe
```

遊戲與素材檢查由 `Tiny-Splatoon-assets` target 部署 `assets/` 至執行檔旁。純素材修改不會重新連結遊戲；部署清單以外的本機檔案會保留。玩家資料位於 `%LOCALAPPDATA%/Tiny-Splatoon/profile.json`，詳見[玩法與玩家流程契約](contracts/gameplay-player-flow.md)。

## Linux（Ubuntu 22.04 x64）

需要 OpenGL 4.5 core profile 驅動、CMake 3.20+、Ninja、C++ 編譯器與 vcpkg：

```bash
sudo apt-get update
sudo apt-get install --no-install-recommends build-essential cmake ninja-build pkg-config curl zip unzip tar libgl1-mesa-dev libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libxkbcommon-dev libxkbcommon-x11-dev libwayland-dev wayland-protocols libx11-xcb-dev libxxf86vm-dev libasound2-dev libpulse-dev libssl-dev libcurl4-openssl-dev
git clone https://github.com/microsoft/vcpkg.git "$HOME/vcpkg"
git -C "$HOME/vcpkg" checkout b1b19307e2d2ec1eefbdb7ea069de7d4bcd31f01
"$HOME/vcpkg/bootstrap-vcpkg.sh" -disableMetrics
export VCPKG_ROOT="$HOME/vcpkg"
cmake --preset vcpkg -DCMAKE_BUILD_TYPE=Release
cmake --build --preset vcpkg --parallel 4
./build/Tiny-Splatoon
```

發布包請保留 `Tiny-Splatoon`、`Tiny-Splatoon-Server`、`assets/` 與 `lib/` 的相對位置。玩家資料使用絕對路徑 `XDG_DATA_HOME`，否則存於 `$HOME/.local/share/Tiny-Splatoon/profile.json`。

## 測試與擷取

```powershell
ctest --preset vcpkg
.\build\Tiny-Splatoon.exe --frames 3 --ui-capture build/captures/menu.png
.\build\Tiny-Splatoon.exe --reference-practice tidewater
.\build\Tiny-Splatoon.exe --reference-match tidewater --frames 180 --match-capture build/captures/turf.png
.\build\Tiny-Splatoon.exe --reference-zones tidewater
.\build\Tiny-Splatoon.exe --reference-boss tidewater --duration 240
.\tools\Measure-ReferenceTurf.ps1 -BuildDirectory .\build
```

`ctest --preset vcpkg` 執行目前建置目錄登記的原生檢查；來源 oracle 只有在存在 `splat-ref/` 時才可執行。GPU 測試、遊戲擷取與效能量測應依序執行，避免同時佔用 GPU。短時檢查不代表全遊戲畫面或效能矩陣已驗收；結果與限制見[還原進度](restoration-progress.md)。

### 空中墨水診斷

連射槍局部 SDF 與撞擊檢查：

```powershell
cmake --build build --target shooter_ink_capture shooter_impact_test
.\build\shooter_ink_capture.exe --assets assets --output build/captures/shooter-sdf --frames 180
ctest --test-dir build --output-on-failure -R '^(native_shooter_impact|reference_projectile_visuals|reference_projectile_gpu|reference_worldfx_gpu)$'
```

Spinner Stream／Jet 使用獨立 120 Hz 發射時間線；正式 Spinner 呈現使用 Tube，Capsule SDF 與螢幕空間深度重建為診斷模式：

```powershell
cmake --build build --target ink_stream_test ink_stream_gpu_test
ctest --test-dir build --output-on-failure -R '^native_ink_stream'
.\build\ink_stream_gpu_test.exe assets/shaders/ink-stream build/captures/ink-stream 180
```

這些畫面測試檢查停射、速度／方向變化、斷裂與碰撞；它們不驗證 RT 塗地比例，也不代表多人、Boss 或跨 GPU parity。

## 可選建置工具

`vcpkg-sccache` preset 使用獨立的 Release 建置目錄；`sccache` 必須在 `PATH`：

```powershell
cmake --preset vcpkg-sccache
cmake --build --preset vcpkg-sccache --parallel 4
ctest --preset vcpkg-sccache
```

MSVC Debug／RelWithDebInfo 若使用 `/Zi` 不在此 preset 的快取範圍；若改用 `/Z7`，須另行驗證。

`vcpkg-clang-tidy` preset 只建置 `Tiny-Splatoon`，並執行三項指定檢查。使用 Visual Studio 內附的 clang-tidy 19 或更新版本：

```powershell
cmake --preset vcpkg-clang-tidy -DCMAKE_BUILD_TYPE=Release
cmake --build --preset vcpkg-clang-tidy
```

此 preset 匯出 Ninja 編譯資料庫；其他 MSVC 旗標仍可能無法由 clang-tidy 解析。它不包含在一般 `vcpkg` preset 中。

整體實作範圍見[目前實作規格](spec.md)；[場景與渲染契約](contracts/rendering-stage-environment.md)、[戰鬥視覺契約](contracts/combat-visuals.md)與[多人連線契約](contracts/network.md)列出相關系統邊界。
