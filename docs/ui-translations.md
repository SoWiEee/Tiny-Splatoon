# Traditional Chinese UI labels

`src/gui/UiText.cpp` provides Traditional Chinese display labels for the native UI. The functions look up the existing equipment and stage IDs and style indices; they do not change stored IDs, ordering, or gameplay data. An unknown ID, kind, or index returns an empty `std::string_view` so callers can fall back to the source name.

## Verified Nintendo terminology

Nintendo Taiwan's official *Splatoon 3* pages use the equipment-part labels **主要武器**, **次要武器**, and **特殊武器**. Its official weapon-class names include **射擊槍**, **滾筒**, **蓄力狙擊槍**, **旋轉槍**, **爆破槍**, **毛筆**, **潑桶**, **防空傘**, **刮水刀**, **機動槍**, and **獵魚弓**. The official Taiwan terminology can be checked on Nintendo's [weapon customization page](https://www.nintendo.com/tw/switch/av5ja/customize/index.html); Nintendo Hong Kong's [weapon report](https://www.nintendo.com/hk/switch/av5ja/report/index.html) and [game page](https://www.nintendo.com/hk/switch/av5ja/) provide additional official Traditional Chinese usage.

Those generic category terms are useful reference terminology. The item, stage, and character names below belong to this project and are **custom translations**, not Nintendo translations or official Splatoon item names. Some labels use familiar generic terms from Nintendo's catalog, but the full project-specific names are independently written for this game.

## Main weapons

| Stable ID | Source name | Traditional Chinese display label | Status |
| --- | --- | --- | --- |
| `shooter` | Spritzer | 潑墨射擊槍 | Custom translation |
| `twins` | Twinfire Pistols | 雙焰手槍 | Custom translation |
| `brolly` | Canopy Brolly | 天篷防空傘 | Custom translation |
| `blaster` | Popper Blaster | 爆點爆破槍 | Custom translation |
| `spinner` | Squall Spinner | 暴風旋轉槍 | Custom translation |
| `charger` | Glint Charger | 閃光蓄力槍 | Custom translation |
| `bow` | Tideline Bow | 潮線弓 | Custom translation |
| `roller` | Swell Roller | 湧浪滾筒 | Custom translation |
| `brush` | Swish Brush | 刷刷毛筆 | Custom translation |
| `blade` | Brine Cutlass | 鹽潮彎刀 | Custom translation |
| `mitts` | Sponge Mitts | 海綿拳套 | Custom translation |
| `bucket` | Bilge Bucket | 艙底潑桶 | Custom translation |

## Sub weapons

| Stable ID | Source name | Traditional Chinese display label | Status |
| --- | --- | --- | --- |
| `bomb` | Splat Bomb | 濺墨炸彈 | Custom translation |
| `sticky` | Cling Charge | 黏著蓄爆彈 | Custom translation |
| `burst` | Pop Pellet | 爆彈丸 | Custom translation |
| `shaker` | Shaker Bomb | 搖晃炸彈 | Custom translation |
| `seeker` | Skitter Bomb | 爬行追蹤炸彈 | Custom translation |
| `waddle` | Waddle Bomb | 蹣跚炸彈 | Custom translation |
| `torpedo` | Tide Torpedo | 潮汐魚雷 | Custom translation |
| `tracer` | Tracer Bolt | 追跡彈 | Custom translation |
| `boomerang` | Whirl Boomerang | 旋返迴力鏢 | Custom translation |
| `scan` | Echo Orb | 回聲偵測球 | Custom translation |
| `curtain` | Drip Curtain | 滴落墨簾 | Custom translation |
| `sprinkler` | Twirl Sprinkler | 旋轉灑墨器 | Custom translation |
| `mine` | Lurk Mine | 潛伏地雷 | Custom translation |
| `beacon` | Hop Beacon | 跳躍信標 | Custom translation |
| `mist` | Murk Bomb | 濃霧炸彈 | Custom translation |

## Special weapons

| Stable ID | Source name | Traditional Chinese display label | Status |
| --- | --- | --- | --- |
| `slam` | Tidal Slam | 潮汐重擊 | Custom translation |
| `storm` | Ink Tempest | 墨汁風暴 | Custom translation |
| `barrage` | Splat Bomb Barrage | 濺墨炸彈連發 | Custom translation |
| `barrage_sticky` | Cling Charge Barrage | 黏著蓄爆彈連發 | Custom translation |
| `barrage_burst` | Pop Pellet Barrage | 爆彈丸連發 | Custom translation |
| `barrage_seeker` | Skitter Bomb Barrage | 爬行炸彈連發 | Custom translation |
| `barrage_mist` | Murk Bomb Barrage | 濃霧炸彈連發 | Custom translation |
| `bubbler` | Bubble Guard | 泡泡護盾 | Custom translation |
| `sonar` | Deep Sonar | 深海聲納 | Custom translation |
| `strike` | Vortex Strike | 漩渦打擊 | Custom translation |
| `zooka` | Twister Zooka | 旋風火箭炮 | Custom translation |
| `wail` | Howl Box | 嚎叫音箱 | Custom translation |
| `kraken` | Kraken | 海怪 | Custom translation |
| `blower` | Bubble Blower | 泡泡吹風器 | Custom translation |
| `jetpack` | Ink Jet | 墨汁噴射器 | Custom translation |
| `stamp` | Mega Stamp | 巨型印章 | Custom translation |
| `booyah` | Cheer Orb | 歡呼能量球 | Custom translation |
| `zipcaster` | Zipline | 觸手飛索 | Custom translation |
| `crab` | Crab Rig | 螃蟹機甲 | Custom translation |

## Stages

| Stable ID | Source name | Traditional Chinese display label | Status |
| --- | --- | --- | --- |
| `tidewater` | Tidewater Plaza | 潮汐廣場 | Custom translation |
| `kelpline` | Kelpline Terminal | 海帶線貨運站 | Custom translation |
| `halyard` | Halyard Marina | 升帆遊艇港 | Custom translation |
| `saltpan` | Saltpan Basin | 鹽田盆地 | Custom translation |
| `crossmarket` | Crossroads Market | 十字路口市場 | Custom translation |
| `lockgate` | Lockgate Canals | 閘門運河 | Custom translation |
| `terraces` | Terrace Heights | 台地高地 | Custom translation |
| `cargo` | Cargo Terminal | 貨運碼頭 | Custom translation |

## Appearance options

The `StyleLabel(kind, index)` kinds correspond to the append-only style arrays in `splat-ref/src/game/character-style.js`. Each list below is in the source index order; all labels are custom translations.

| Kind | Source names by index | Traditional Chinese display labels by index |
| --- | --- | --- |
| `hair` | Tide; Spike; Twin; Bob; Pony; Crest; Knot; Swoop | 潮汐；尖刺；雙辮；波波頭；馬尾；冠羽；髮髻；側梳 |
| `skin` | Rosy; Peach; Tan; Cocoa; Porcelain; Honey; Olive; Chestnut; Ebony | 玫瑰調；蜜桃；古銅；可可；瓷白；蜂蜜；橄欖；栗棕；烏木 |
| `outfit` | Basic Ringer; Night Pinstripe; Raglan Runner; Chevron Tee; Breton Stripe; Splatter Tee; Splat Camo; Dip-Dye Tee; Pro Jersey; Track Top | 滾邊T恤；夜色細條紋；插肩運動衫；V字條紋T恤；海魂條紋；飛濺T恤；飛濺迷彩；漸層染色T恤；專業球衣；運動外套 |
| `eyes` | Amber; Lagoon; Violet; Lime; Rose; Hazel; Frost; Ember | 琥珀；潟湖；紫羅蘭；萊姆；玫瑰；榛果；冰霜；餘燼 |
| `hat` | None; Snapback; Beanie; Bucket Hat | 無；後扣棒球帽；毛帽；漁夫帽 |
| `brows` | Classic; Bold; Arched; Straight | 經典；濃眉；彎眉；平眉 |
| `preset` | Rookie; Dash; Pip; Coral; Marlo; Riptide; Nori; Suki; Kelp; Skipper | 新手；疾速；皮普；珊瑚；馬洛；激流；諾麗；蘇琪；海帶；船長 |
