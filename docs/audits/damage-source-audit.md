# 武器傷害來源核對

這份靜態對照用 `splat-ref/src/config.js` 的來源數值核對原生 `ArsenalData`、`WeaponRunner`、`CombatWorld` 的傷害路徑。表內擊數以敵方起始 100 HP、每一顆必要命中都命中為條件；它不是實戰 TTK，也不是所有武器的 runtime parity 證明。玩家 100 HP、敵墨每秒 20 傷、累計上限 40 傷且不致死（最低 1 HP）、離開敵墨 1.3 秒後開始回血（乾地 22 HP/s、潛墨 60 HP/s）在來源與原生調校中一致：[`config.js`](../../splat-ref/src/config.js#L22)、[`ArsenalData.cpp`](../../src/game/ArsenalData.cpp#L1062)、[`PlayerSimulation.cpp`](../../src/game/PlayerSimulation.cpp#L681)。

| 主武器 | 來源傷害 | 100 HP 下的條件結果 | 原生路徑／注意事項 |
| --- | --- | --- | --- |
| shooter | 每發 36；間隔 0.10 s | 3 發；從首發到第三發約 0.20 s | [`config.js`](../../splat-ref/src/config.js#L95) → `WeaponRunner` 子彈、`CombatWorld::applyHit`。 |
| twins | 每發 30；間隔 0.085 s，雙手輪替 | 4 發；需讓四發都命中 | [`config.js`](../../splat-ref/src/config.js#L158) → 雙側發射；輪替不會把單發傷害合併。 |
| brolly | 每顆近距離彈丸 8，共 11 顆；最大射程傷害 2.5 | 同一輪 11 顆全中理論值 88，單輪不足 100 | [`config.js`](../../splat-ref/src/config.js#L180) → `WeaponRunner` 彈丸與距離衰減；散射或距離會減少命中傷害。 |
| blaster | 直擊 125；爆風 70→30 | 直擊可一發；遠近爆風均不足 100 | [`config.js`](../../splat-ref/src/config.js#L126) → 直擊與 `CombatWorld::burst`。爆風迴圈略過 `direct` 受害者，直擊對象不會再吃一次同爆風傷害（[`CombatWorld.cpp`](../../src/game/CombatWorld.cpp#L685)）。 |
| spinner | 每發 26；間隔 0.05 s | 4 發；連續射擊首發到第四發約 0.15 s | [`config.js`](../../splat-ref/src/config.js#L148) → 連射彈丸。 |
| charger | 充能傷害 40→160 | 充滿可一發；低傷害端需 3 發 | [`config.js`](../../splat-ref/src/config.js#L117) → `WeaponRunner` 依蓄力比例算傷害；原生在充滿閾值給 160。 |
| bow | 點按直擊 14–20、環爆 30–38；滿蓄中央 55、側箭 45；插地爆發中央 45、邊緣 15 | 一輪多箭可疊加；只看單支箭或環爆不能推定擊殺 | [`config.js`](../../splat-ref/src/config.js#L199) → 三箭 volley、箭環與插地爆發分別結算；原生未在此文件宣稱逐種命中組合 parity。 |
| roller | 滾動接觸 140；甩墨 125 近端→30 遠端，共 9 滴 | 接觸或近端 125 傷甩墨可一發；遠端需多次命中 | [`config.js`](../../splat-ref/src/config.js#L106) → 接觸與甩墨是不同路徑。 |
| brush | 滾動每 tick 30；揮擊每滴 26→12，共 5 滴 | 需多次滾動 tick 或多滴揮擊命中 | [`config.js`](../../splat-ref/src/config.js#L169) → 揮擊滴墨與接觸分開結算。 |
| blade | 輕擊近戰 35；滴墨 40→20；重擊 130；波刃 70→50 | 重擊可一發；輕擊加同次滴墨最高 75 | [`config.js`](../../splat-ref/src/config.js#L220) → 近戰、滴墨、波刃為不同命中來源。 |
| mitts | 直拳 38；拳波 22→10；跳砸中心 100→邊緣 35，另有手套彈 40 | 3 拳可擊殺；跳砸僅中心值可單發擊殺 | [`config.js`](../../splat-ref/src/config.js#L239) → 跳砸與手套彈不同路徑；爆發傷害同樣略過直擊對象。 |
| bucket | 每次 slosh 55 | 兩次同目標命中可擊殺 | [`config.js`](../../splat-ref/src/config.js#L137) → 每輪每個目標最多取一次該輪命中。 |

原生射線與來源的遮擋次序一致：先處理沿途角色，再判斷可破射擊物與牆面；這項差異不是目前能解釋「死得太快」的證據。傷害數值也沒有顯示可直接提出 nerf 的來源／原生落差。若要定位一次實戰死亡，需要該次錄影或重播中的攻擊者武器、命中數、受擊前 HP、敵墨狀態與時間；表中數字本身只能說明幾種常見的快擊殺路徑確實符合已設定傷害。

此核對只證明來源調校與可讀到的原生結算路徑相符；沒有聲稱 12 種主武器的所有實際命中組合、距離衰減、遮擋與動畫視覺都已逐項 runtime 對照。
