# v0.8.0 玩家視角短測

GTX 1650 Max-Q／1920×1080／低畫質，暖機 30 幀、量測 120 幀，固定 seed 1；包含真實 FollowCamera、八名角色、RmlUi HUD 與近鏡特效。

- GPU 完成後吞吐量平均 **91.61 FPS**；GPU frame 平均 **6.35 ms**、p95 **9.33 ms**。
- CPU frame 平均 **10.66 ms**、p95 **18.66 ms**；平均每幀 206.95 粒子、129.76 draw calls。
- [原始報告](v080-player-low.json) · [逐幀資料](v080-player-low.csv)

```powershell
.\build\Tiny-Splatoon.exe --reference-match tidewater --benchmark-camera player --benchmark-warmup 30 --benchmark-frames 120 --graphics-quality low --width 1920 --height 1080 --benchmark-report build/v080-player-low.json
```

這是短時間固定負載的整合檢查，不含全地圖／全畫質／最壞視效矩陣，也不代表最低 FPS 穩定超過 60。其他品質沿用既有選項。
