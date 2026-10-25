# 工具

原生版本直接使用已提交的資產。`tools/` 只保留目前測試或功能會用到的工具。

| 工具 | 用途 |
| --- | --- |
| `export-audio-source.mjs` | 音訊資產檢查與音樂比較負向測試，由 `tests/audio_source_render.test.mjs` 使用。 |
| `reference-loader.mjs` | 為戰鬥來源 oracle 與 Three.js loader 測試解析 vendored Three.js。 |
| `import-music.mjs` | 匯入自訂音樂；由 `tests/import_music.test.mjs` 覆蓋路徑與增益驗證。 |
| `Measure-ReferenceTurf.ps1` | 原生參考場景的七圖效能量測工具。 |
