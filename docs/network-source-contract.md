# 原生多人連線契約

依 2026-10-22 的使用者決策，傳輸直接使用已安裝的 GameNetworkingSockets 1.4.1（vcpkg revision 1）。此決策取代參考版的 WebSocket wire protocol；房碼、房間與遊戲規則仍對齊 [spec.md NET-01–12](../spec.md#14-線上房間與同步)。

## 第一階段範圍

- `src/network/GnsTransport.*` 管理 GNS 初始化、IP listener／connection、可靠訊息、事件與資源生命週期；應用層在主迴圈非阻塞輪詢。
- 單一 runtime 可服務同一程序中的 server 與多個 client。連線及訊息佇列有容量限制，超出限制會回報失敗，不以無限累積掩蓋過載。
- `native_network_transport_2/3/8` 使用真實 loopback sockets 驗證訊息與斷線；這不是跨網或完整遊戲驗收。執行 `ctest --test-dir build -C Release --output-on-failure -R '^native_network_transport_'`。

目前底層傳輸獨立建置為 `game_network`；正式選單尚未連接，不能據此宣稱線上遊玩可用。沒有新增臨時假房間或把 IP 位址當成房碼。

## 本機驗收（2026-10-22）

MSVC x64 Release 建置及 `native_network_transport_2/3/8` **3/3 通過**（3.50 秒）。測試使用真實 UDP/IP loopback sockets，確認 client 身分對應與可靠回覆、64 KiB 完整訊息、送出及收到超長封包拒絕、240-message 事件佇列滿載後關閉來源連線、第九條連線拒絕、遠端斷線通知、待處理回呼期間銷毀端點／重新連線，以及兩次 Init／Shutdown 生命週期。SDK 保留預設加密設定。

GNS 1.4.1 的 listener 要求非零 port，測試在 localhost 的 30000–39999 範圍最多嘗試 32 次 bind，並由 SDK 查詢實際位址；不使用僅測試記憶體傳輸的 socket pair。收訊每次輪詢最多 64 則，單則最多 64 KiB，事件總量最多 256／1 MiB，傳送 pending＋未確認可靠資料限制為 256 KiB。紀錄位於 `build/acceptance-gns-transport.log`。

以上僅驗證同機傳輸；未驗證跨程序／跨網、封包遺失／抖動、公開服務負載或遊戲同步，沒有達成 NET-01–12 的完整驗收。

## 後續接入順序

1. 原生房間服務：五字元房碼、最多八人、穩定名單、隊伍／準備／換裝／設定、錯誤與最早加入成員換主；加入房間前驗證訊息格式與公開玩家欄位，保留來源速率與 idle 限制。
2. 選單與房間入口，再接入來源 3D hub／lobby；比賽開始須等同步實作可用。
3. 本機玩家與 host 的權責、20 Hz 狀態／事件、插值、墨水重播、命中、三模式與 Boss；remote 呈現不重算傷害或墨水。
4. 斷線／換主／Bot 接手、結果回同房，以及延遲／抖動／跨網矩陣。

GNS direct-IP 不會自動提供房碼查找、公開 relay、Steam SDR、NAT 穿透或帳號認證。開發測試不部署公開服務、不改防火牆；正式跨網部署需另外實作及驗收。
