# 音訊契約

原生音訊使用 `miniaudio` 與已匯出的 48 kHz 素材。`AudioEngine` 管理音效、loop、音樂串流、音量 bus、位置、淡入淡出及清理；`GameAudio` 將角色、裝備、世界事件、模式與 Boss 狀態接到音訊。對照證據與主觀驗收限制見[還原進度](../restoration-progress.md)。

## 資產與播放

音訊目錄目前含 201 個 cue、31 個 loop bed 與九首來源程序配樂。音效及 loop 以 48 kHz stereo PCM 保存，音樂以 FLAC 串流並沿用來源 loop 點。引擎設有 48 個 one-shot 與 24 個 loop voice 上限，實作來源最短重播間隔、音高抖動、位置與 listener、music/SFX/master 音量、ducking 及跨曲淡入淡出。`Clear()` 與場景結束會停止或淡出管理中的聲音。

Title／menu 與 battle／Boss 曲目可使用隨附 MP3 覆蓋；設定指定的自訂曲只供 battle 與 battle-final 使用。缺少或無法解碼的自訂曲回退至套件曲目。結果依勝負播放來源結果曲。配樂依狀態切換，當來源 BPM 相同時等待小節邊界；最後一分鐘與 Boss 階段別名不會重啟相同串流。

## 事件路由

路由涵蓋移動、跳躍、武器發射與蓄力、命中、死亡／復活、低墨、特殊技能、裝置狀態、Zones 控制事件、Boss 動作／階段／受擊、結果 XP 與場景氛圍。移動、Tracer、裝置及 Boss 音效依來源位置與距離規則播放；ZoneHum 由狀態／控制張力即時合成，並使用共享混響返回。Boss 的 20 個命名 socket 依姿勢提供定位。

暫停、死亡、物件移除、場景切換、Boss 結束與結果會處理相應 loop、待播聲音及重複音效節流。離線 Zones 的控制、懲罰、輪替、延長及填墨事件已連入音訊。

## 混音實作邊界

音效素材已包含來源混響與母線處理，原生不再分離濕聲並套用 HRTF；把 HRTF 套在 stereo 素材會同時處理乾濕聲。音樂 intensity 沒有 gameplay 呼叫端，因此播放匯出時的完整混音。通用 beam event 沒有 firing-actor 身分時不播放 Charger 專用命中音；來源 loop 參數未完整暴露時使用素材預設音色。聽感驗收狀態見[還原進度](../restoration-progress.md)。
