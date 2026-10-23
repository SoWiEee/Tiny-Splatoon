# 選單、戰鬥名單與設定捲動驗收

- 依使用者 2026-10-23 更新，取代先前簡約方向；選單採暖紙色、深墨描邊、橘色主操作與鈷藍次要操作，保留繁體中文、Noto Sans TC／Inter、角色預覽及現有玩家流程。來源烏賊與十五個武器圖示經 HTML 正規化／SVG rasterization 匯出為本機 PNG，全部可選主武器都有對應素材；`node tools/export-hud-icons.mjs --check` 通過。
- 戰鬥頂列依實際 active 名單顯示我方／對方存活及總人數、武器、本機標記、陣亡叉號、向上取整復活倒數與特殊就緒。訪客以自己的隊伍為左側；離線移除角色不計入；Boss 八人協力分列於時計兩側並使用同一隊色。練習隱藏競技名單。既有 Zones／Boss 資訊往下排列以避開頂列。
- 設定裁切根因是 RmlUi 的自動內容高度撐開 flex 區域，讓 scroll height 與 client height 相同。內容區改由 94dp 頁首／46dp 固定底列限制，欄內獨立捲動；720p／1080p 實際測試滑鼠滾輪、最後「減少畫面動態」控制項位置／焦點、返回鈕可見及回到頁首。
- MSVC x64 Release 全目標建置通過。UI renderer、UI interactions、正式入口與 CLI **4/4 通過（17.31 秒）**，見 `build/acceptance-bold-ui-final.log`。最後僅調整測試的捲輪動畫時序以確保頂部截圖有效，UI interactions 補驗 **1/1 通過（10.64 秒）**，見 `build/acceptance-bold-ui-capture-final.log`；原生遊戲的平滑捲動設定未因此改變。
- 已檢視 720p／1080p 設定頁上／下緣、八人房間、Turf／Zones 名單，以及陣亡／Boss 狀態；正式入口另擷取 `build/captures/ui/native-menu-bold.png` 與 `native-battle-bold.png`，確認使用 GTX 1650 Max-Q／OpenGL 4.5。UI fixture 的純色底用於隔離介面，不能當成完整遊戲 FPS 證據。
- 風格檢查只回報既定 Inter 字體的六處提示，依使用者字體選擇保留。最終介面與畫面驗收由主 agent 完成。`splat-ref/` 與來源基準未修改。
- 此段為選單階段的歷史檢查點；墨槽、十二種武器準心與隊友名牌的後續完成範圍見下節。特殊球、復活環、擊殺通知與完整進出場演出仍未全量移植。

## v0.8.0 武器、道具、音樂與 HUD 整合

- 十二種可選主武器準心以實際玩家狀態更新；測試逐種擷取、區分準心族、實際充能與潛水隱藏。墨管涵蓋 720p／1080p、耗墨刻線、低墨、滿墨淡出及暫停隱藏。
- 活隊友頭頂名牌、武器、特殊就緒、遠距淡出與離屏箭頭接入；追蹤敵人顯示與一般敵人隱私、陣亡／暫停隱藏均有互動測試。依使用者補充，尖端與本體採同隊色、粗深色框線；已檢視隔離 UI 及正式對戰畫面。
- 普通炸彈模型改用實際 `Projectiles.throwBomb` 來源球體與金屬蓋；危險範圍在模型啟用時仍繪製。新增測試曾先重現舊版缺圈，再驗證實際爆炸半徑、倒數進度、可見像素與清場。
- 53 個來源配方嚴格 oracle 通過，比對生成記錄、第一幀 render attributes 與到期清理；除明列三角函式向量容許最多 1 Float32 ULP，其餘數值保持精確。
- 六類粒子改接來源 GLSL、mesh 與 instance buffer；分別要求可見像素、無 GL error 及恢復 caller GL state。裝置 GPU 覆蓋十五種副武器的主要 phase、地雷敵墨隱藏／觸發／隊色與動態清場。波刃 GPU 驗證牆面／射程停止、0.14 秒消退與 owner 銷毀後的安全存活。
- 選單音檔解碼長度 150.653 秒、對戰音檔 194.142 秒；依實際長度循環，測試在結尾前八個 frame 解碼跨界，並驗證最後一分鐘／Boss 階段別名不重啟同曲。原生混響與事件路由回歸通過。
- 玩法／同步與來源回歸初跑 16/17；唯一失敗是舊 Tracer 測試要求共享事件的位置為原點。為供命中特效定位，事件現在保留世界接觸點；GameAudio 仍以 `sourceVictimLocal` 取消音訊位置。更新接觸座標斷言後補驗通過，既有本機受擊音訊路由測試亦通過。
- 原生 GPU／UI／音訊／入口 **9/9**（63.68 秒）、名牌／Tracer 補驗 **2/2**（12.10 秒）、來源資產匯出 **5/5**（2.34 秒）通過。各批紀錄保留在 `build/logs/acceptance-v080-*.log`，未以這些結果宣稱重跑完整 96 項測試。
- 已更新 README 三張 1600×900 正式選單／Turf／Zones 截圖；使用 GTX 1650 Max-Q／OpenGL 4.5 NVIDIA 591.74。1080p 低畫質玩家視角短測平均 91.61 FPS，GPU frame 平均 6.35 ms；[報告與逐幀資料](../benchmarks/v080-2026-10-23/README.md)保留測量邊界。

- 最後 Shaker 充能燈與 grip 共用來源 kick，並修正未充能時殘留 meter 不應觸發燈光；針對性 GPU 補驗 **1/1**（4.74 秒）通過。

## 專案整理

- 清除根目錄五個 `.obj`、100 個舊 `p*` 輸出、36 個舊瀏覽器／匯出暫存目錄與 205 份歷史 build logs，再移除四個未登記的舊 debug 執行檔與兩個 build 根目錄物件檔，另清除 33 個舊手動探測腳本、物件／音訊與已解包的下載檔，合計約 1.6 GiB；保留目前 executables、測試、CMake／vcpkg、資產及本輪驗收輸出。
- 文件分為 `contracts/`、`audits/`、`acceptance/`、`archive/`，README 與相對連結同步更新；現行指南保留在 docs 根目錄。
- 21 個 fixture headers 與 11 個 JSON 集中至 `tests/fixtures/`，同步更新測試、匯出器、CMake 與手動畫面比較入口。工具盤點未找到可確認已退役的腳本，保留現有匯出器並新增用途導覽。
- 38 份本輪／現存 logs 集中到 `build/logs/`，22 份 JSON／CSV 報告集中到 `build/reports/`；保留目前 CMake 登記的 66 個遊戲／服務／測試／evaluator 執行檔。
- 整理後 Release 全目標建置與引用路徑回歸 **14/14**（153.78 秒）通過，涵蓋角色姿勢／變體、來源 fixtures／資產、瀏覽器／原生資料及整體 restoration exports。237 個本地文件連結檢查通過；fixture 的 LF 屬性同步移到新路徑，生成 GLSL 保留來源格式，由匯出器做逐位元檢查。
- 修正共用來源瀏覽器 helper 關閉後未刪除 profile 的根因；HUD 圖示實際重匯出檢查通過，`build/hud-icons-*` 殘留數為零。

剩餘邊界：來源全域視覺 RNG、完整場景逐影格對照、主觀聽感、跨 GPU／品質／最壞負載與跨網仍未驗收。正式裝置與粒子已接入不等於整個參考遊戲已達 100% 視覺還原；來源目錄與 hash 基準未修改。
