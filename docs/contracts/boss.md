# Boss 協力模式契約

Boss 模式支援最多八名同隊玩家對抗大型 Boss。房主持有 Boss、AI、時計及勝負；每位玩家控制自己的角色。地圖合法性、同步與換主細節見[多人契約](network.md)。整合測試證據與限制列於[還原進度](../restoration-progress.md)。

## 姿勢、傷害與材質

Boss 動畫沿固定步進計算，不依畫面繪製次數更新。原生 100 骨骼 rig 輸出蒙皮矩陣、十個戰鬥 hit sphere、來源材質 uniform 與命名 socket；socket 用於音效與效果定位，不替代碰撞 hit shape。姿勢包含根節點移動／轉向、瞄準、登場、階段切換、受擊、暈眩、死亡及六種招式的預兆、動作、恢復。

模型、六組鋼材／外殼／肉體／眼睛／廢料／小蟹材質、UV、蒙皮資料、來源 stencil 與物理材質 shader 接入原生 renderer。生命量達約 66% 與 33% 時切換階段；實際受擊更新材質 flash。HUD 顯示 Boss 生命、三階段、弱點開放／破殼狀態、八人名單與專用結果。

Boss 生命週期、姿勢、傷害 hit shapes、材質變化、HUD 結算及危險區域均由比賽狀態驅動。來源六種招式包括 Crusher Slam、Container Barrage、Ink Cannon Sweep、Hull Charge、Brood 與 Shell Frenzy；小螃蟹另有來源姿勢、材質及死亡效果。

