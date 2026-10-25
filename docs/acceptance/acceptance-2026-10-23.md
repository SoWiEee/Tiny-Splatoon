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


## 戰鬥 HUD、回饋與通知補完

- 新增獨立、有界且不耗盡的戰鬥事件紀錄：接受傷害後記錄實際 HP 損失，擊倒只記一次，重開清空並更換 epoch；移除角色清除事件裸指標。乾射、副武器部署／耗墨失敗與特殊技能否決各由實際玩法分支產生，成功消耗到零墨不會誤報。
- 線上命中／擊倒沿用可靠視效封包，由受擊角色擁有者回報；接收端核對 sender 擁有權、角色與隊伍及數值邊界，遠端事件不重新廣播。loopback 檢查確認命中後才顯示與無回送重複。
- 原生 RmlUi geometry 補液態特殊球、gain／ready／active／denial、出生盾環、命中／擊倒標記、傷害方向弧與復活環；來源 15 副武器／19 特殊技能圖示由新匯出器逐位元核對。五種副武器沿用來源本身的 bomb fallback。
- UI 接入個人塗地分數及浮動加分、擊倒／助攻卡、最多五筆擊倒紀錄、首次／多重／全隊／復仇／終結連殺提示、實際攻擊來源死亡卡與彈射提示、隊伍名單／開始／結束及最後十秒演出。覆蓋暫停／地圖、重開、低動態效果設定與角色狀態生命週期。
- 初次 UI 執行曾崩潰，後續檢視發現特殊球液面邊緣在無交點時仍輸出極大座標；已跳過該片並夾限液面。新增真實 framebuffer 背景條帶斷言，修正後正常 CTest UI 通過；未以此斷言宣稱已證明先前崩潰的唯一成因。
- MSVC Release 全目標建置成功；戰鬥事件／武器／網路／UI **4/4**（27.28 秒）與 UI renderer／來源裝備資產 **2/2**（6.77 秒）通過。包含實際命中像素、特殊球邊界、否決、首次／雙重／全隊擊倒／助攻、死亡原因、暫停及重開斷言；不是完整測試集重跑。
- `build/captures/ui/battle-hud-{ready,hit,wipeout,death,720,1080}.png` 保留本機驗收畫面；720p／1080p 與死亡卡已檢視，修正副武器章／百分比位置、特殊球越界、死亡期間通知重疊。世界死亡標記、3D diorama／登場、完整結果與 Boss 專屬演出仍未全量移植；復仇／終結連殺、主觀動畫時序及跨機 HUD 未逐項目視驗收。
- 正式 `Tiny-Splatoon.exe --reference-match tidewater --frames 120` 擷取並檢視完整戰場合成；NVIDIA GTX 1650 Max-Q／OpenGL 4.5／591.74，輸出實際 framebuffer 1920×1055（`build/captures/ui/native-battle-hud.png`）。來源目錄與 hash 基準未變更。


## 世界標記、玩家流程、投射物與音訊補驗（介面再遷移前）

- 完整 MSVC Release 建置 153/153 完成。第一批 12 項整合回歸中 11 項通過（66.14 秒）；音訊新增 Boss 臨界案例先失敗，原因是測試只前進模擬時計，未經過音訊引擎依真實時計執行的素材最短重播間隔。依素材 metadata 校準等待後，`reference_audio_engine` 通過（29.21 秒）；保留最初失敗紀錄，沒有弱化模擬時間的 60／70／300 ms 門檻斷言。
- 通過範圍包括網路同步、控制器、角色步行／瞄準、投射物數值與 GPU、UI renderer／互動、CombatRunner／World、角色姿勢與變體。這是受影響範圍的分批檢查，非完整測試集重跑。紀錄：`build/logs/round-details-release-build.log`、`round-details-integration.log`、`round-details-audio-tests4.log`。
- 世界／地圖死亡標記改用事件位置快照及追蹤權限，五秒到期、最後 1.3 秒淡出、重開清理；預排彈射圈選、來源結果獎章資格與排序、角色預測槍口接入。來源死亡圖示依六組色盤／雙隊匯出，逐位元 `export-hud-icons --check` 通過。圖示換入後與使用者新要求的原版介面一併進行後續 UI 補驗。
- 音訊加入 300 ms 階段 roar、350 ms 暈眩 loop、恢復／階段／暫停／死亡／同物件重開清理，本機普通／弱點與遠端弱點命中門檻。主觀聽感、完整 HRTF／壓縮器、Boss 精確 foot／impact 事件及被同一快照合併的命中仍未全量還原。


## 來源選單、地圖與 Boss 介面整合（10 月 23 日至 24 日）

- 選單採使用者確認的暖紙色／深墨描邊／橘藍色盤及純色背景；十二種武器卡片、完整副／特殊切換、繁中名稱／簡介與四類設定頁已接入。預覽僅裝備／造型顯示，其餘頁面填滿。720p／1080p 卡片與設定捲動、保存、返回入口通過互動驗證。
- Tab／M 使用原生 3D 場地相機，四輪 fit 與藍隊 post-fit 翻轉通過 source math；小地圖為 256×384 原生 FBO，首次後至少 125 ms 更新間隔，墨量版本改變不繞過節流。RmlUi 借用 GL 紋理，釋放及解構不刪除外部所有權紋理。
- 地圖來源箭頭、隊友圓點、合法追蹤敵人圓角菱形、白框同色武器圖釘及圓角長框接入。關閉 Tab 時立即重建小地圖標記，消除原 100 ms 快取殘留。未支援的 box-shadow 層造成白色方塊／左上雜框，改為 solid borders；補 stencil clip mask，像素確認圓角外無紋理、中央保留圖像，前後面的 stencil 參數與原有 GL 狀態均還原。
- SourceResults 採 2.3 秒勝敗／地圖揭曉，再顯示排行／四人台座、分數／XP 時序；首個 Enter 跳過演出，下一次選動作。來源頒獎台八個網格逐位元匯出檢查通過。修正 flex 造成姓名寬度歸零、中文道具說明溢出按鈕區與 Boss class 影響排行的選擇器衝突。
- Boss HUD 接來源正常／破殼圖徽、三階段菱形、66／33% 刻度、實際 HP、怒吼／弱點開放／破殼狀態；來源原有 16 組姿勢 oracle 通過。Boss 結算以實際勝利／超時生命週期產生，顯示 HP、時間、階段、擊破／逃脫與八人傷害排序雙欄，另列弱點、擊倒、死亡與塗地。正常對戰可選五組含粉紅／薄荷綠色盤；Boss 八人皆 team 0，按本輪使用者要求 Boss 墨色固定藍色（色弱模式保留其藍色），弱點材質跟隨己方墨色。
- Release 全目標建置通過，最終記錄 `build/logs/source-commit-build.log`。受影響 11 項分批驗收：`source-final-acceptance1.log` 中 10 項通過，UI 新 fixture 誤用僅供線上的狀態入口先失敗，改走真實勝利／超時生命週期後通過（`source-final-ui2.log`，15.19 秒）。後續地圖／裁切修正的 UI 與 renderer 2/2 通過（`map-clip-tests.log`，12.73 秒），獨立新增圓角像素斷言的 renderer 通過（`map-round-pixel-test.log`，4.63 秒），最新圖示匯出 `--check` 通過（`source-final-icons-check.log`）。最新 Boss 四人台座與八人排行的 720p／1080p 場景合成新增檢查通過（`boss-final-composition-test.log`，17.05 秒），確認戰鬥血條隱藏且雙欄保持可讀寬度。未重跑未受影響的完整 99 項測試。
- 正式 `Tiny-Splatoon.exe --native --frames 3` 及 `--reference-match tidewater --reference-mode boss --frames 600` 完成啟動／擷取；GTX 1650 Max-Q、OpenGL 4.5、591.74。純色標題畫面 1600×900，實際 Boss 戰 1920×1055；檢視確認左上無空黑框、小地圖圓角裁切、同色八人及 Boss 階段條。UI 合成另以準確 1280×720／1920×1080 檢查，含 native Tab、minimap、裝備、設定及完整結果。

仍未驗收／未完成：圖釘重疊避讓與引線、來源游標／視差及完整 3D 登場；Boss title card／連擊數字等完整通知美術、結果印章動態／彩紙物理、來源 PBR 逐像素對照；投射物陰影與預測槍口數值 oracle；音訊精確腳步／impact、主觀定位／HRTF／壓縮器；跨 GPU／品質／最壞負載及跨網矩陣。以上保留在 backlog，不以本輪 UI／元件通過宣稱全遊戲 100%。鎖定的 `splat-ref/` 與來源 hash 基準不修改。


## v0.8.5 第一批修正（2026-10-24）

- 真實 click target 回歸重現開始按鈕內層 `start-label` 被誤當動作 ID；事件改解析到外層 button。點文字與點背景均可發出 Start。
- Intro 不再呼叫 menu 音樂；BeginMatch 直接播放 battle／Boss 當階段 BGM，Playing 保留開始號角。測試在 Intro 檢查實際解碼檔案為 Splattack，修正前失敗、修正後通過。
- 結算按鈕使用獨立文字 span 與 flex 置中；720p／1080p 比對 border box 與文字中心。裝備頁補各武器來源耗墨模型與數字；未修改已正確的來源玩法值。
- 初始四項回歸 UI／音訊失敗，Arsenal 與 992 組 WeaponRunner 來源狀態通過（`v085-red-tests.log`）。修正後音訊通過 31.84 秒、UI 通過 11.29 秒（`v085-fixes-tests.log`、`v085-fixes-final-ui.log`）；Release 目標建置通過。僅執行受影響檢查。


## v0.8.5 互動演出、音訊與裝備驗收（2026-10-24）

- 地圖加入六輪 pin 避讓、引線、吸附游標／手把確認、滑鼠視差及彈射弧。幾何測試保留真實投影座標並獨立檢查顯示避讓，避免以移動世界座標掩蓋重疊。
- Boss 的實際傷害 journal 驅動本機連擊／弱點回饋，補繁中登場、階段、招式與離屏提示。畫面檢視抓到 phase 橫幅與一般「開始」重疊，已在 Boss 專屬提示出現時隱藏一般橫幅並加入斷言。遠端計數仍可能合併同一快照多次命中。
- 頒獎台接來源升起、角色依序落下與落地伸縮、彩紙／敗北墨滴與印章；跳過及低動態效果直接呈現穩定姿勢。首次入場已核對來源為站立出生／2D 名單，不添加虛構飛入。
- AA 可保存並即時切換：0 samples 使用 FXAA，原生解析度保留 MSAA；關閉兩者停用。原生 shader 位於 `assets/shaders/post/`，未加入來源生成目錄；來源 post export 檢查曾因目錄多出檔案失敗，移出後恢復通過。
- 音訊接實際 BossPose crossing（intro、鉗擊、六足落地）及 source death guard；XP 改經詳細 UI sink 傳入來源選單 65 ms／0.05／pitch 1。普通 projectile impact 不再多播牆音，充能槍與迴旋鏢改由明確牆面事件產生。
- 新增 Bomb／Waddle／Mine 真實 tick 引爆、Brolly 11 顆主攻擊、scan／mist／curtain／sprinkler 生命週期與潛水 press ordering／emerge buffered shot。新增 Bomb 測試初跑失敗：先修正裝置刪除後 fixture 越界，再確認 source 從 `b.pos` 計算衰減而 native 偏移 0.2 m；修正後 targeted devices 通過（0.82 秒）。這些檢查與既有 source fixtures 合成 12 main／15 sub／19 special 覆蓋，矩陣見[裝置生命週期盤點](../audits/device-special-lifecycle-audit.md)。
- 完整套件前的音訊／玩家／裝置初跑 2/3，音訊 28.94 秒、玩家 3.60 秒；裝置失敗及根因如上。保留 `v085-audio-device-targeted.log`、`v085-device-rerun.log`，不刪除失敗紀錄。
- GTX 1650 Max-Q／OpenGL 4.5／NVIDIA 591.74 正式 Boss 程式啟動與擷取完成（`v085-final-native-boss.log`）；另有 720p／1080p 的 UI、地圖及結果元件畫面檢查。

音訊邊界：來源匯出器已將每個 cue 的 shared reverb、glue compressor 與 limiter 烘焙進 stereo PCM；原生使用 miniaudio spatialization。實驗可從 Edge OfflineAudioContext 擷取 HRTF impulse response，但對現有素材再套 HRTF 會連濕聲一起處理，不能當作正確還原。真正 parity 仍需 dry/wet 分離及 runtime 共用匯流排；本輪未實作，亦未宣稱主觀聽感／全場景逐影格或跨 GPU／跨網已通過。


完整 99 項首輪耗時 377.50 秒，97 項通過（`build/logs/v085-full-tests.log`），兩項失敗保留原紀錄：`reference_combat_devices` 的新 blast fixture 未進入預期引爆狀態，以及 `reference_audio_export` 的舊文字契約仍要求 native 路由到 legacy `menu`。同批音訊播放／動畫 crossing、武器來源數值、潛水狀態、Boss、UI、GPU、網路與其他來源匯出檢查均通過。使用者其後追加選單圖示、返回鍵、預備視角鎖定與 README 截圖；最終只補跑失敗項及這些新增變更影響的測試，不將首輪失敗隱藏或宣稱首次全綠。


### v0.8.5 最終補驗與來源獨立性

- 首輪失敗均已修正並補驗：裝置 fixture 改用實際碰牆引爆與安全 ID 查找；音訊契約明列現用 bundled music，保留來源資產 inventory 檢查。`v085-device-fixture-tests.log`、`v085-final-confirm-tests.log` 保留結果。地圖吸附改讀 RmlUi 滑鼠事件，避免隱藏 GLFW 視窗無焦點時游標設定無效，`v085-map-pointer-tests.log` 通過。
- `build/independent-v085/source` 實際不含 splat-ref，也不是 symlink；冷建置 267 步完成。全部 73 項原生測試通過，290.03 秒（`v085-no-source-tests.log`）；最終固定 LOD fixture／來源匯出另通過，fixture-only oracle 不再讀取來源檔案。來源可選匯出檢查仍保留於主目錄。
- Zones 減少常駐文字：計數位於時鐘兩側；區域圖示、雙色控制條及 80/20 閾值保留，擁有者以顏色及 accessible label 表達。零懲罰不顯示，輪換提示限最後 5 秒，延長賽／事件提示保留。主目錄區域計分、AI、音效、GPU 與 UI 6/6 通過，55.65 秒（`v085-zones-final-tests.log`）；同一補丁複製至無來源副本重建後，UI 1/1 通過，12.10 秒（`v085-zones-independent-tests.log`）。
- 720p／1080p 合成畫面已檢視，計數／名單／時鐘無重疊。README 更新原生設定選單、塗地及最新 Zones 截圖；實際遊戲擷取由無來源副本在 GTX 1650 Max-Q 執行。選單為原生 UI 合成，戰鬥為完整場景，不把合成背景當成完整遊戲渲染。
- Impeccable 分別對選單與本次 Zones 範圍進行一次 detector；僅回報使用者指定 Inter 的 overused-font 警告（選單 2、HUD 6），保留指定字體。鎖定的 splat-ref 與 baseline 未修改；未重跑未受本次 UI 變更影響的完整套件。

- 實際 Zones 擷取發現 RmlUi 不接受 `font-variant-numeric`／`border-style`，移除無效宣告並補跑 UI 確認（1/1，11.27 秒，`v085-zones-style-confirm.log`）；採既有 border 寬度／顏色，沒有加入瀏覽器專用 CSS。


## v0.8.6 先行發布與未通過項目（2026-10-24）

- 依使用者最新指示停止本輪修正，先 commit／push 並發布 v0.8.6；以下保留實際失敗，不宣稱全綠或完整還原。
- Release 遊戲及受影響目標建置成功（`build/logs/v086-latest-build.log`、`v086-boss-ui-build.log`）；包含正式執行檔與最新部署資產。18 項回歸 16 項通過，75.81 秒（`v086-latest-acceptance.log`）：GNS 兩／三客戶、角色展示、大廳 CPU／GPU／FX、控制器、裝置／投射物 GPU、Boss 生命週期、AI、特殊控制、World FX oracle、角色姿勢／變體及 pose bridge。
- World FX GPU 補驗通過，8.25 秒（`v086-boss-worldfx-ui-confirm.log`）：小螃蟹來源低面數 physical 材質修正 interleaved mesh 及 DFG LUT 綁定；HDR 真實燈光下確認可見像素與有限數值。共用 CharacterShader 解構先解除自身仍在使用的 program，修正後製 program 被刪除後跨渲染器還原失效 ID 的 GL 錯誤。
- 部署目錄 `build/` 不含 splat-ref，從該目錄以 `lobby_visual_gpu_test.exe . captures/lobby-staged` 完成大廳原生場景／UI 擷取；檢視 1280×720 畫面，來源光影、八人角色與暖紙色 UI 正常。既有大廳 GPU 測試含 720p／1080p、反射／陰影開關及泳跡像素斷言。
- Boss GPU 未通過：自然模擬仍未選到合法 Charge（`alive=1,target=0,clock=125.25,moves=2`），尚待建立 source-valid 跑道／朝向前置條件；不能據此宣稱六類招式已驗收完成。
- UI 首次因新加 Kelpline 選單測試誤讀 select 本體的 InnerRML 失敗，改讀實際 option 後繼續；其後舊復活 fixture 未進入 Playing，再隔離調整。最新執行仍在結果獎章 `塗地之王／擊倒之星` 斷言失敗（`v086-ui-phase-confirm.log`，15.18 秒）；暫停全按鈕測試位於後段，尚未完成執行。程式已移除局內設定、操作說明隱藏整份 HUD、返回主選單清除暫停／確認層，仍需下一輪完整補驗。
- 玩家 Shift 回歸尚未通過（`v086-player-override-confirm.log`，2.85 秒）：格柵測試、chargerDive 墨量及己墨牆面落地斷言仍存在差異。乾地／敵墨維持人形為使用者明確指定的來源差異，但目前未完成相關 fixture 與實作的最終判定。
- 未重跑完整測試套件；Bot 自主隊友超級跳躍仍保留待辦，Kelpline／海帶線貨運站已提供三模式。未修改鎖定的 splat-ref 或 baseline。發布後不追蹤 GitHub CI 進度。


## v0.8.6 發布後補驗（2026-10-25）

- UI 原失敗已重現（`v086-ui-resume-red.log`）；結果獎章測試原以 1500／7 假設最高分，實際 Bot 可超過。改以所有演員當前最高塗地／擊倒加值建立獎章資格，不改生產規則。完整 `reference_player_ui_interactions` 通過，15.28 秒（`v086-ui-resume-confirm.log`），包含真實點擊暫停操作說明、Back／Esc、返回取消／確認、Resume、清除主選單殘留層及復活 Intro／Playing／Finish／Results。
- 玩家來源 fixture 原允許乾地變魷魚，與使用者指定規則不同。保留所有不受影響的來源斷言；乾地移動對 run oracle、格柵獨立驗證健康站立人形、charger／spinner／roller 對同武器無 Shift 控制組檢查耗墨／姿態／發射，另檢查 roller windup、己墨牆面攀爬／空中魷魚／乾地落地及強制彈射。`reference_player_simulation` 通過，0.76 秒（`v086-player-resume2-confirm.log`）。
- Boss 的來源出生朝向不保證符合衝撞導航；一般目標也不保證 chargeLane 非空。GPU fixture 掃描實際 Tidewater plan cells／完整朝向，要求 poseOk／turnOk／cast >= 6 m，經權威狀態入口採用合法位置並放置存活目標，再經真實 forceMove／tick 到前兆與活動期。六招式與小螃蟹含死亡延遲通過，3.90 秒（`v086-boss-resume2-confirm.log`）；不是 AI 自然選招統計。
- 本批僅修改測試與驗收文件，未修改生產玩法、來源樹或固定 fixture；既有未受影響的完整測試集不重跑。

## 華康／BlitzMain 字體切換（2026-10-25）

- 依使用者指示由 Luna xhigh sub-agent 實作字體變更。RmlUi 主 family 改 `Splatoon2`（BlitzMain.otf 的真實 family）；華康以 `DFPZongYiW5-B5` memory face 載入，font bytes 由 GameUi::Impl 保存到 Shutdown。Windows 中文路徑透過 filesystem::u8path＋ifstream 讀取；備援順序為華康、Noto Sans TC、Inter，保留動態 Unicode 玩家姓名能力。
- 全 UI 常用文字離線 cmap 檢查：BlitzMain 覆蓋所有 95 個可列印 ASCII，華康支援常用繁中，缺的 6 個非 ASCII 字元均是特殊符號。U+265B／U+2726／U+27A4 在舊／新四字體中都缺，本輪記錄既有差距，不擴張改圖示。
- Release `Tiny-Splatoon`、`game_ui_test`、`rml_renderer_test` 建置成功（`font-swap-build.log`）。`reference_player_ui_renderer` 2.51 秒、`reference_player_ui_interactions` 21.70 秒，2/2 通過，24.27 秒（`font-swap-tests.log`）。Renderer fixture 同時驗證新英文／數字、繁中 fallback 可見像素及 GL 狀態；互動測試含 720p／1080p 設定、裝備、地圖、HUD、結果及文字置中檢查。
- 已檢視 1080p 設定、戰鬥 HUD 與完整結果像素。正式 native menu 啟動成功，擷取 `font-swap-menu-1080.png`；實際視窗內容為 1920×1055，測試 FBO 為 1920×1080，兩者不混稱（`font-swap-native.log`）。部署兩字體 hash 與來源一致；未更改字級或遊戲玩法，未重跑未受影響的全套測試，未 commit／push。


## v0.8.7：UI／字體、墨水實驗撤回與 Linux（2026-10-25）

- 使用者指定本輪以暖紙色＋深墨描邊＋橘／藍整理 layout、typeset、bolder、animate、polish，並包含 HUD。保留華康／BlitzMain 與 Unicode 備援；加強裝備卡片、設定列表、結算表格／CTA、計時器與通知層級，互動顏色採短轉場，沿用減少動態設定。小地圖維持圓角長方形。結算／方向提示既有缺字改為可用符號。
- 本輪未提交的長條墨片、Lush 動畫、water-jet／metaball 實驗全部撤回，投射物與 World FX 生產檔案相對 HEAD 無差異；備份置於忽略的 build/reports/ink-experiment-reverted-20261025，不進發布包。RT 墨面與既有遊戲玩法保留。
- Windows Release 遊戲及受影響測試建置成功。首批 `native_player_profile`、`reference_projectile_gpu`、`reference_player_ui_renderer`、`reference_player_ui_interactions`、`reference_worldfx_gpu` 5/5 通過（58.41 秒，v087-windows-tests.log）。最後 UI 修正後完整互動確認通過（23.37 秒，v087-ui-confirm.log），含 720p／1080p、設定捲動、slider 數值對齊與結果 CTA 置中。
- UTF-8 資產路徑的部署／移除／保留未知本機檔案回歸通過（12.31 秒，v087-assets-test.log）；獨立 GNS 房間服務跨程序流程通過（6.13 秒，v087-network-test.log）。
- GTX 1650 Max-Q 正式入口、塗地及 Zones 擷取成功。實際視窗內容 1920×1055，UI fixture 為 1920×1080／1280×720。README 三張圖更新為本版裝備 fixture 與實際塗地／Zones 畫面。Windows 發布目錄從其他工作目錄啟動成功（v087-windows-package.log）。

- Linux：Ubuntu 22.04／GCC 11.4、CMake 3.22、Ninja 與鎖定 vcpkg；全部原生遊戲、房間服務及測試目標完成 Release 編譯。修正 GCC 不接受的巢狀 LOD 預設引數，及三份 fixture／產生器的相對 include 路徑；三個來源重匯出檢查通過，數值未更動。
- 首批 Linux CPU／Node 90 項回歸為 83 通過、7 失敗（linux-cpu-tests.log）。其中 5 項需要 Edge 的來源重匯出（reverb、Boss 材質、音訊、HUD 裝備、選單圖示），本機未安裝 Edge，列為環境未完成；正式遊戲與發布包不需要瀏覽器。
- Boss 差異定位為 GCC 最佳化略去 GLM float／double 往返，破壞來源 Float32Array 的鏈條粒子儲存精度；明確以 volatile float 儲存三個分量，保留 Release -O3。來源 oracle 16 情境／3925 更新／280 完整骨架快照通過（linux-boss-rounding-probe.log、linux-final-audio-boss.log）；Windows 同一修正亦通過。
- Linux 音訊驗收使用 PulseAudio 48 kHz null sink、norewinds=1；最小播放間隔改等待真正的音訊引擎時鐘，保留即時重播拒絕與完整聲部／事件斷言。未修改音訊播放規則。使用 Linux 已部署素材執行完整 audio_engine_test 通過，含 201 cues、全音樂／循環、MP3 真實尾端跨越、事件路由、48 聲部及清理（linux-audio-staged-assets.log）。從 Windows 掛載來源目錄串流 MP3 的 0.5 秒非同步 seek 驗收仍不穩定，未放寬此讀取門檻，不能將首批 CTest 稱為全綠。
- Linux 軟體 Mesa llvmpipe 23.2.1／LLVM 15，實際 OpenGL 4.5 core：UI renderer、完整 UI 互動、正式入口、render pipeline、CLI 5/5 通過，58.60 秒（linux-gl-ui-tests.log）。720p Linux 設定字體／排版亦檢視。不是實體 GPU 效能驗收。
- Windows 最終可攜性 5/5 通過，36.83 秒；最後 Boss oracle／完整音訊 2/2 通過，41.22 秒（v087-windows-portability-tests.log、v087-windows-final-tests.log）。
- Linux 安裝包以 $ORIGIN/lib 尋找隨附 GNS，兩個 ELF 的 ldd 無缺漏；從家目錄啟動安裝遊戲成功，獨立安裝服務可實際監聽（linux-package.log、linux-installed-smoke.log）。Windows／Linux 發布包均隨附 assets 與房間服務。實體 Linux GPU、手把、跨機／跨網與完整效能矩陣仍未驗收；發布後不代為追蹤 GitHub CI。
