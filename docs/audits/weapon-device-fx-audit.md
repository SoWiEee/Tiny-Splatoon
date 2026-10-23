# 武器、裝置與特殊武器 FX 路徑盤點

**狀態：原生程式路徑已接入；53 配方數值 oracle、裝置 phase 與六類粒子 GPU／清場驗證通過。** 本文記錄目前註冊表、`WorldEffects` hooks/recipes 與 `DeviceRenderer` 中實際存在的行為，不代表已完成來源逐幀視覺 parity。

## 已註冊裝備

`ArsenalData.cpp` 目前登記 12 種主武器：`shooter`、`twins`、`brolly`、`blaster`、`spinner`、`charger`、`bow`、`roller`、`brush`、`blade`、`mitts`、`bucket`；15 種副武器：`bomb`、`sticky`、`burst`、`shaker`、`seeker`、`waddle`、`torpedo`、`tracer`、`boomerang`、`scan`、`curtain`、`sprinkler`、`mine`、`beacon`、`mist`；19 種特殊武器：`slam`、`storm`、`barrage`、`barrage_sticky`、`barrage_burst`、`barrage_seeker`、`barrage_mist`、`bubbler`、`sonar`、`strike`、`zooka`、`wail`、`kraken`、`blower`、`jetpack`、`stamp`、`booyah`、`zipcaster`、`crab`。這些是裝備登記數量，不代表每個 ID 都有獨立粒子 hook。[主武器資料](../../src/game/ArsenalData.cpp#L8) · [副武器資料](../../src/game/ArsenalData.cpp#L495) · [特殊武器資料](../../src/game/ArsenalData.cpp#L771)

## 已連接的粒子 hook 與 recipes

`WorldEffects::Update` 取得 combat visual events 後，依序呼叫 weapon、weapon-device、kit、special 與 warning hooks，再處理一般 visual event。device/special 類事件會略過一般 dispatch，由對應專用 hook 消費；beam event 也略過一般 dispatch。[更新與 dispatch](../../src/render/WorldEffects.cpp#L186)

主武器路徑目前實際呼叫：

- 活躍 projectile kind `shot`、`blast`、`drop`、`slosh` 依移動距離及每幀預算觸發 `shotTrail` 或 `sloshTrail`。
- 蓄力中的 `charger`、`spinner` 觸發 `chargeGlow`，滿蓄邊緣觸發 `chargeFull`；`charger` 瞄準射線命中時觸發 `laserDot`。
- beam visual event 觸發 `beamTrail`，射線命中時再觸發 `beamImpact`。

對應配方定義於 [`WorldFxWeaponRecipes.inc`](../../src/render/WorldFxWeaponRecipes.inc#L5)，觸發條件位於 [`WorldFxWeaponHooks.inc`](../../src/render/WorldFxWeaponHooks.inc#L2)。配方中也定義了 `sloshImpact`、`spinUp`、`spinStream`、`spinFull`；目前這些名稱沒有被該 hook 呼叫，定義存在不等於執行路徑已接通。

武器與裝置事件路徑目前實際呼叫：

- `lowink` 事件觸發 `dryFire`，同一 actor 以 0.4 秒間隔節流。
- 射擊計數變化可觸發 `muzzleBlaster`、`muzzleCharger`；`roller`、`bucket`、`brush` 的開火邊緣可觸發 `flickCurtain`。
- `twins` 閃避狀態觸發 `dodgeSplash`、持續 `dodgeSkid`、結束時 `dodgePlant`；角色處於 rolling、著地且移動足夠快時觸發 `rollerSpray`。
- 普通 `bomb` 有飛行 `bombTrail`、落地/反彈 `bounceSplash`、著地後 fuse `beepPulse`；`stormBomb` 有飛行 trail 與反彈 splash。

上述觸發都在 [`WorldFxWeaponDeviceHooks.inc`](../../src/render/WorldFxWeaponDeviceHooks.inc#L2)，相應配方位於 `WorldFxWeaponRecipes.inc`。這段程式沒有 seeker 掃描或 pellet fan 配方呼叫。

特殊武器路徑目前實際呼叫：

- `stormCloud` 狀態觸發 `rainSheet`，以地面 raycast 節奏觸發 `stormPuddle`，並依計時器觸發 `stormFlash`。
- `speaker` 處於 `blast` phase 時沿 mouth/direction 產生 `wailRing`。
- 活躍 `jetpack` 使用兩個動畫 tank attachment nozzle，以 20 Hz 觸發 `jetpackExhaust`；附近的 `special_audio` / `zip_pull` 事件在找到活躍 jetpack 時觸發 `jetpackBoost`。
- `bubble_pop` 且來源為 `blower` 時觸發 `bubblePop`；`special_audio` 且來源為 `shield_pop` 時觸發 `shieldPop`。

觸發條件見 [`WorldFxSpecialHooks.inc`](../../src/render/WorldFxSpecialHooks.inc#L2)，配方見 [`WorldFxSpecialRecipes.inc`](../../src/render/WorldFxSpecialRecipes.inc#L5)。這裡描述的是目前原生 event/state 接線，不據此宣稱來源事件時序或粒子序列已完全一致。

## 其他原生狀態視覺與 DeviceRenderer

部分視覺直接由 `WorldEffects.cpp` 的 actor/object 狀態處理，不經上述 recipes：例如 `slam` 啟動的 rings/drops/puffs 與落下 drops、`sonar` 啟動的三道 rings、`bubbler`/`kraken` 啟動爆散、蓄力中 `booyah` orb，以及 `zipcaster` zip phase 的 tether beam。[actor/object 狀態視覺](../../src/render/WorldEffects.cpp#L252)

`DeviceRenderer` 負責裝置及部分特殊角色的模型與動態視覺，並由 `StageViewer` 接入 `WorldEffects`。[DeviceRenderer 更新與繪製](../../src/render/DeviceRenderer.cpp#L198) · [StageViewer 接線](../../src/app/StageViewer.cpp#L258) 可確認的行為包括：scan/mist cloud 的成長與淡出、waddle 感知 ring、torpedo target ring、curtain sheet 的 HP 顯示、tracer ribbon，以及 shaker/boomerang 等裝置動態和 Kraken、Crab、Jetpack 特殊角色模型。這些模型或狀態視覺與粒子 recipes 是不同路徑，不單獨作為來源粒子 parity 的證據。[裝置繪製路徑](../../src/render/DeviceRenderer.cpp#L283)

本輪主 agent 已完成針對性原生 GPU／UI／玩法檢查，並擷取正式對戰畫面；詳見[驗收紀錄](../acceptance/acceptance-2026-10-23.md)。完整來源逐影格畫面 parity 仍未驗收，本文不以登記數量推定每個 ID 都有獨立粒子或已通過全場景對照。
