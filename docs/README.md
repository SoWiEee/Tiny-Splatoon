# 文件導覽

- [目前實作規格](spec.md)：產品流程、可用模式與系統範圍。
- [還原進度](restoration-progress.md)：唯一的進度與驗收證據紀錄，包含仍待驗收的限制。
- [架構說明](../architecture.md) · [開發與驗證](development.md) · [發布流程](release-workflow.md) · [繁中翻譯](ui-translations.md)

## 現行契約

| 契約 | 範圍 |
| --- | --- |
| [玩法與玩家流程](contracts/gameplay-player-flow.md) | 模擬、模式、地圖、角色輸入、玩家資料與 UI 流程 |
| [場景與渲染](contracts/rendering-stage-environment.md) | 地圖幾何、環境、連續墨水、HDR 與陰影管線 |
| [戰鬥視覺](contracts/combat-visuals.md) | 投射物、裝置、特殊技能與 World FX |
| [Boss](contracts/boss.md) | Boss 姿勢、材質、招式與限制 |
| [音訊](contracts/audio.md) | 資產、播放生命週期、事件路由與差異 |
| [多人連線](contracts/network.md) | 房間、服務、權責、同步及部署限制 |

## 參考資料

`benchmarks/` 保存固定負載的量測資料與條件；`images/` 保存 README 使用的原生截圖。局部測量與整體驗收結論以[還原進度](restoration-progress.md)為準。

建置、遊玩與一般原生驗證不需 `splat-ref/`。來源目錄仍保留作為對照依據；不得將單項 fixture 或單次截圖推論為全遊戲完成。
