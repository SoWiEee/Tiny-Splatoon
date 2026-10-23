# 測試導覽

測試由 [CMakeLists.txt](../CMakeLists.txt) 註冊，名稱保留以供針對性回歸。

| 檔案 | 用途 |
| --- | --- |
| `*_test.cpp` | 原生玩法、保存、網路、音訊、UI 與 GPU 驗證 |
| `*_eval.cpp` | 輸出原生狀態，供來源 oracle 比對 |
| `*.test.mjs` | 來源資料、匯出器與跨程序驗證 |
| [fixtures/](fixtures/) | 固定的來源期望值、來源 hash、C++ headers 與 JSON |
| `character-render-reference.html`、`compare_character_renders.py` | 手動來源／原生角色畫面比對工具 |

```powershell
ctest --test-dir build -C Release --output-on-failure -R 'reference_worldfx|reference_projectile'
```

完整指令與驗收邊界見[開發指南](../docs/development.md)。只修改文件或版面時，選擇受影響測試；完整來源匯出驗證仍需要鎖定的 `splat-ref/`。
