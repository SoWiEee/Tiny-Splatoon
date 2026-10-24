# 下一輪候選工作

本清單對齊 `spec.md`，區分已知未實作與尚未完整驗收。本輪已處理的範圍見 [2026-10-22 驗收](acceptance/acceptance-2026-10-22.md)；以下保留尚未完成的來源差異。

使用者指定優先記錄的三個候選：

1. 音訊剩餘來源差異：結果 `xp_tick` 的 65 ms 節奏與 Boss 動態 sockets 已接入；XP 音高／音量、Boss impact／腳步／階段事件、主觀定位與 HRTF／完整壓縮器仍需對照；Shaker／Tracer／Wail 事件和 `zone_hum` 混響已補驗。
2. 道具特效與潛水特效盡可能還原：正式墨面游泳尾流、隆起、入墨／出墨／水花及 paint-ripple 回呼已接入；15 副武器／19 特殊技能已逐項核對來源生命週期，完整逐影格視覺矩陣仍待補齊，見[道具與技能檢查表](audits/device-special-lifecycle-audit.md)。
3. HUD／UI 已放大字級並壓縮 Zones 面板，保留繁中及 Noto Sans TC／Inter；完整來源版面與所有模式／解析度仍需比對。

| 候選範圍 | 目前缺口 | 規格／證據 | 建議驗收 |
| --- | --- | --- | --- |
| 環境與光照 | 天空／海面／泡沫／水線／港區反射／PMREM／遠景／霧／鳥已接入來源；仍需全場景固定畫面比對、跨 GPU 與最壞負載驗收 | RENDER-02、RENDER-04；[場景契約](contracts/stage-visual-source-contract.md) | 日間／黃昏與 Halyard 水域的固定視角來源對照，加上玩家視角效能 |
| 道具與潛水特效 | 60 個雙隊來源裝置／特殊道具模型、材質、Tracer 光帶與場景道具動畫已接入；飛行 spin、Shaker 手持／落地及六類粒子來源 shader 已補接；預測瞄準槍口已接原生角色姿勢與合法性回退，仍缺直接來源數值 oracle。投射物陰影與 Boss hazard／小螃蟹已於 v0.8.6 接入；隨機雲團、視覺 RNG 與潛水主觀觀感仍需完整對照 | FX-01–03；[世界視效契約](contracts/worldfx-source-contract.md)、[投射物契約](contracts/projectile-source-contract.md) | 主／副／特殊近鏡發射到清場，以及連續潛水／回人形的來源對照 |
| 保存、設定與完整單人介面 | 原生 profile／設定原子保存、裝備／外觀／XP、首頁／置物櫃／3D 預覽／操作說明／製作名單／結果頒獎台已接入；觸控與震動後端、實體手把手感仍未驗收 | SAVE-01–05、FLOW-01–05／09–10 | 首次啟動、重開還原、舊資料遷移、寫入失敗及每個入口往返 |
| HUD／UI 排版與字級 | 繁中名稱、Inter／Noto Sans TC、頂列存活名單、墨管、十二種武器準心、隊友標記、五秒死亡標記及結果武器／隊伍／統計／來源獎章規則已接入；原生地圖 diorama、分段結算與 Boss 階段／結果已接；地圖避讓／視差／彈射弧、Boss 通知與結算動態已補；仍需來源隨機視效／PBR 與遠端精確命中比對 | HUD-01–08、FLOW-01–10；[翻譯表](ui-translations.md)、[HUD 差異](audits/hud-source-audit.md) | 1280×720 與 1920×1080 的 HUD、選單、暫停和結果畫面來源對照及可讀性 |
| Zone 模式與正式進度 | 離線七圖已接入 Zone 規則／逆轉／延長、AI／HUD／場地標記／音效與 XP；仍需完整人工視覺／聽感及跨 GPU 驗收，線上 follower 同步屬網路待辦 | MODE-Z01–07、AI-04、AUDIO-03、SAVE-04 | 三難度對戰、領地切換、逆轉／延長、結算只執行一次 |
| 線上連線 | GNS 房間服務／房碼／RmlUi 房間、同步、延遲緩衝、換主、斷線 Bot 接手與回同房已接入；來源 3D hub／lobby 自動演出已接入；跨機／跨網與公開服務負載待完成 | NET-01–12；[多人契約](contracts/network-source-contract.md) | 大廳長時間加入／離開與重入、跨網三模式、長局／最壞持續裝置與延遲矩陣 |
| 音訊的剩餘來源差異 | 選配歌曲、Zone／XP 事件及 20 個 Boss 動態 sockets 已接入；v0.8.5 補來源選單 XP 節奏／音量、Boss 動畫 impact／腳步與充能槍／迴旋鏢牆面事件。Shaker／Tracer／Wail 及 ZoneHum convolution 已補驗；主觀定位、WebAudio HRTF 與同時多聲部總匯流排壓縮仍有差異，現有 PCM 已包含各聲音單獨匯出的來源混響／壓縮，不可直接再疊一層 | AUDIO-01–04；[音訊契約](contracts/audio-source-contract.md) | 裝備與 Boss 的主觀聽感／定位、XP 節奏、loop 參數變化與切場清理 |
| 全量驗收 | 元件 oracle 通過不代表全場景 100%；Boss hazard／live crablet 元件已補接，仍需跨圖／全局視覺矩陣，音訊仍需主觀聽感比對；QA-12 最壞視效負載與品質矩陣未完成 | QA、規格第 15 節 | 固定 reference captures、長局重入／清場、各品質與 GPU 完成時間量測 |

環境與裝置來源資產及離線 Zones 已接入；其餘差異、效能優化與完整玩家流程仍按候選範圍安排。線上連線涉及權責與傳輸契約，適合獨立一輪處理。

## 移除 splat-ref 的完成門檻（2026-10-21 核對）

一般建置、遊戲執行與 Windows 發布已使用提交於 `src/`、`assets/` 的原生程式及資產，不需要 `splat-ref/`、瀏覽器或 Node.js。這代表執行環境已獨立，並不代表原版全部功能已移植。

已完成的前置工作：profile／設定原子保存、裝備／外觀／XP、Locker／預覽與玩家入口已接入（提交 2ee0241）；七張離線地圖的 Zones 規則、AI、HUD、音效、延長與 XP 已接入。Boss 主體姿勢通過 16/16 嚴格 oracle，見[目前姿勢契約](contracts/boss-pose-source-contract.md)；早期 0/16 只是歷史檢查點。

退役來源前仍須完成：

1. **原生測試獨立已遷移**：World FX 的 53 配方與 LOD 的 2,288 狀態改以已提交、固定來源 hash 的 fixture 驗證；來源重匯出僅在參考樹存在時執行。一般執行、建置及原生 fixture oracle 不讀取參考版程式。無來源目錄的隔離建置／驗收結果見本輪驗收紀錄。
2. **剩餘視聽與完整單人驗收**：完成上述道具／潛水、HUD／UI、音訊候選，並做 Boss hazard／live crablet、跨 GPU 與最壞負載矩陣。
3. **原生多人連線**：GNS 房間、同步、斷線／換主／Bot 接手與本機三模式已接入；完成來源 3D hub／lobby 及三模式跨機／跨網矩陣。僅本機測試通過不算全量完成。
4. **最終獨立驗收及來源退役**：v0.8.5 已在沒有 `splat-ref/` 的隔離專案完成全目標建置與 73 項原生／fixture 回歸。後續仍需完整人工玩家流程、各畫質及最壞負載矩陣。來源專用匯出器／瀏覽器比對工具可在退役時封存或移除，但需先保留固定 fixtures、資產、來源 hash、授權及必要的比對證據；不能讓缺少來源資料夾默默變成「完整還原通過」。

完成上述來源比對前，保留鎖定的參考版；退役後專案內不再要求這個目錄。此核對未刪除或改動 `splat-ref/`。

- Bot 目前不會主動選隊友進行超級跳躍；需加入目標／危險判斷、重新驗證及冷卻策略，依使用者允許留待後續。Kelpline Terminal 已提供於三種模式，繁中名稱為「海帶線貨運站」。


## v0.8.6 發布後優先修正

- Boss GPU fixture 的 source-valid Charge 跑道／朝向前置條件尚未建立，需確認來源限制及實作，保留六類招式完整驗收要求。
- UI 結果獎章斷言仍失敗；隔離早期 respawn Playing fixture，確認結果統計後，再跑完所有暫停按鈕的真實點擊回歸。
- 新 Shift 規則的玩家回歸仍有牆面／格柵／chargerDive 差異，需區分刻意偏離來源與實際錯誤，不能只放寬斷言。
