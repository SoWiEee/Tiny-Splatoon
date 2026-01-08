# Tiny-Splatoon

## 0. Introduction

Tiny-Splatoon 是一個用 **C++17 + OpenGL** 製作的 3D 小型多人連線遊戲。玩家分成紅/綠兩隊，在地圖上移動與射擊，並透過「塗地（ink）」機制影響比分與移動行為。多人連線使用 **Valve GameNetworkingSockets**，採用 **listen server**（Host 同時是玩家），由一台玩家電腦同時扮演 Server + 本機玩家，其他玩家以 Client 連線加入（最多 8 人房：Host + 7 clients）。

程式在架構上使用：
1) **主迴圈（Game Loop）**：固定每幀進行 Update + Render  
2) **Scene（場景）**：Login → Lobby → Game  
3) **GameWorld（遊戲世界）**：集中管理玩家、子彈、物理、塗地、分數  
4) **NetworkManager（網路層）**：負責連線、收包、送包、廣播  
5) **GUIManager（UI 層）**：使用 ImGui 繪製登入/大廳/HUD/狀態顯示  
6) **Splat 子系統**：用貼圖（texture/FBO）+ grid 統計來表示塗地與計分

---

## 1. Directory Structure

```
src/
  main.cpp

  engine/
    core/Window.*  core/Input.*  core/Timer.h  core/Logger.h
    rendering/Shader.* Mesh.* Texture.* ModelLoader.h
    audio/AudioManager.*  audio/miniaudio.h
    scene/Scene.h  scene/SceneManager.*

  scene/
    LoginScene.h
    LobbyScene.h
    GameScene.h / GameScene.cpp
    Level.h
    Entity.h
    FloorMesh.h

  gui/
    GUIManager.h / GUIManager.cpp

  network/
    NetworkProtocol.h
    NetworkManager.h / NetworkManager.cpp

  components/
    Camera.h
    MeshRenderer.h
    Health.h
    HUD.h
    Scoreboard.h

  gameplay/
    GameWorld.h
    Player.h
    Enemy.h
    Weapon.h + ShooterWeapon.h / BrushWeapon.h / SlosherWeapon.h
    Projectile.h
    Item.h

  splat/
    SplatMap.h
    SplatPainter.h
    SplatPhysics.h
    SplatRenderer.h
```

建議閱讀順序（最符合「理解架構」的方式）：

1. main.cpp（主迴圈）
2. engine/scene/Scene + SceneManager（場景切換框架）
3. scene/LoginScene（如何 host / connect）
4. network/NetworkManager + NetworkProtocol（封包協定與收發方式）
5. scene/LobbyScene（大廳同步與 UI）
6. scene/GameScene + gameplay/GameWorld（遊戲核心與網路資料流）
7. splat/*（塗地與計分的資料結構）  

---

## 2. System Architecture

### 2.1 Game Modules Relationship

```
+-----------------------------+
|           main.cpp          |
|  - Window/GLFW/GLAD         |
|  - 主迴圈 dt/Update/Render   |
+--------------+--------------+
               |
               v
+-----------------------------+
|        SceneManager         |
|  - current Scene            |
|  - Update/Render/DrawUI     |
|  - HandlePacket(pkt)        |
+--------------+--------------+
               |
     +---------+----------+
     |         |          |
     v         v          v
+---------+ +---------+ +---------+
| Login   | | Lobby   | |  Game   |
| Scene   | | Scene   | | Scene   |
+----+----+ +----+----+ +----+----+
     |           |           |
     |           |           v
     |           |     +-----------+
     |           |     | GameWorld |
     |           |     +-----+-----+
     |           |           |
     v           v           v
+-----------------------------+
|         GUIManager          |
|  - ImGui Login/Lobby/HUD    |
|  - lobbySlots / ipBuffer    |
+-----------------------------+

+-----------------------------+
|       NetworkManager        |
|  - GNS init/connect/listen  |
|  - Receive -> PacketQueue   |
|  - Send/Broadcast           |
+-----------------------------+
```

重點：  
- **NetworkManager 是全域 singleton**，main.cpp 每幀呼叫 `NetworkManager::Update()` 把收到的封包放進 queue。  
- **SceneManager 把 queue 裡封包逐一交給目前 Scene 的 OnPacket**，因此「封包如何影響 UI/遊戲」由 Scene 決定。  
- **GameScene 把封包轉交給 GameWorld**，所以遊戲中絕大部分網路同步都集中在 GameWorld 處理。

### 2.2 Game Loop (dataflow)

main.cpp 的 while loop（概念化）：

```
while (running):
  dt = Timer.Tick()

  NetworkManager.Update()                // 1) 收包/RunCallbacks
  while NetworkManager.HasPackets():
      SceneManager.HandlePacket(pkt)     // 2) 交給目前 Scene/World

  SceneManager.Update(dt)                // 3) 遊戲邏輯
  SceneManager.Render()                  // 4) OpenGL 渲染
  GUI.BeginFrame()
  SceneManager.DrawUI()                  // 5) ImGui
  GUI.Render()
  SwapBuffers + PollEvents
```

## 3. Module Overview

這一章將「工程拆解」成大模組，並在每個模組底下列出主要類別與互動方式。

---

### 3.1 engine/core：視窗、輸入、時間

#### Window（engine/core/Window.*）

- 職責：封裝 GLFW window 建立、SwapBuffers、PollEvents。  
- 互動：
  - main.cpp 建立 `Window window(...)`，每幀呼叫 `window.SwapBuffers()`、`window.PollEvents()`。

#### Input（engine/core/Input.*）

- 職責：提供 `GetKey(...)`、滑鼠座標等輸入查詢的靜態介面。  
- 互動：
  - main.cpp 設定 mouse callback（`glfwSetCursorPosCallback`）更新滑鼠位置。
  - Player、Camera 等在 Update 讀取 `Input::GetKey(...)` 來控制角色與相機。

#### Timer（engine/core/Timer.h）

- 職責：計算 delta time (dt)
- 互動：main.cpp：`timer.Tick()` → `timer.GetDeltaTime()`。

---

### 3.2 engine/rendering：OpenGL 基礎渲染封裝

#### Shader（engine/rendering/Shader.*）

- 職責：讀取 shader 檔、compile/link、提供 `SetMat4/SetInt/SetVec3...`。  
- 互動：
  - GameScene.OnEnter 建立 shader：`Shader("default.vert","default.frag")`
  - SplatPainter/Renderer 也會用 Shader 設定 inkMap/uniform。

#### Mesh（engine/rendering/Mesh.*）
- 職責：封裝 VAO/VBO/EBO，提供 Draw。  
- 互動：
  - MeshRenderer component 會持有 Mesh（或 Model），在 Draw 時呼叫 Mesh.Draw。
  - FloorMesh 會有自己的 Draw。

#### Texture（engine/rendering/Texture.*）

- 職責：載入貼圖、產生 OpenGL texture。  
- 互動：
  - Level/Floor 或 UI 可能使用貼圖。
  - SplatMap 自己也會建立 texture 作為 ink map。

#### ModelLoader（engine/rendering/ModelLoader.h）

- 職責：載入模型資料（通常是 obj/mesh），供 MeshRenderer 使用。  
- 互動：
  - GameWorld/Level 建立物件時可能用 ModelLoader。

---

### 3.3 engine/audio：音效

#### AudioManager（engine/audio/AudioManager.*）

- 職責：初始化音效系統、LoadSound、PlayOneShot。  
- 互動：
  - main.cpp 啟動時 LoadSound（shoot/hit/superjump...）。
  - Player、GameWorld 在射擊/死亡/技能時播放音效。

---

### 3.4 engine/scene：Scene 與 SceneManager（遊戲流程骨架）

#### Scene（engine/scene/Scene.h）

- 職責：抽象介面，規範場景生命週期：
  - OnEnter / OnExit
  - Update / Render / DrawUI
  - OnPacket（可選）
- 互動：
  - SceneManager 只知道「目前 Scene」，會轉呼叫其方法。

#### SceneManager（engine/scene/SceneManager.*）

- 職責：
  - 管理目前場景 `m_CurrentScene`
  - 提供 `SwitchTo(...)`
  - 每幀轉呼叫 Update/Render/DrawUI
  - `HandlePacket(pkt)`：把網路封包交給目前場景
- 互動：
  - main.cpp：`SceneManager::Instance().SwitchTo(LoginScene)`
  - main.cpp：每幀 `SceneManager.Update/Render/DrawUI` 與 HandlePacket

---

### 3.5 gui：GUIManager（ImGui）

#### GUIManager（gui/GUIManager.*）

- 職責：集中管理所有 UI 畫面與 UI 狀態（UIState）。
- 主要資料：
  - `UIState currentState`：LOGIN / LOBBY / HUD...
  - Login：`ipBuffer`（server address）、`hostPort`、`joinPort`
  - Lobby：`lobbySlots[8]`（玩家 ID、隊伍、武器、ready）
- 主要方法：
  - `DrawLogin(outStartServer, outConnectClient)`
  - `DrawLobby(outStartGame)`
  - `UpdateLobbyState(PacketLobbyState)`：收到 server 廣播後更新 lobbySlots
- 互動：
  - LoginScene.Update：呼叫 DrawLogin，依按鈕回傳值決定 StartServer/Connect。
  - LobbyScene.DrawUI：呼叫 DrawLobby，若 Server 按 StartGame 則 broadcast GameStart。
  - LobbyScene / GameWorld：收到 LobbyState 或 GameState 時更新 GUI 顯示。

---

### 3.6 network：NetworkProtocol 與 NetworkManager（多人同步的核心）

#### NetworkProtocol（network/NetworkProtocol.h）

- 職責：定義封包 enum 與封包 struct（序列化格式）。  
- 重要觀念：
  - 所有封包都有 `PacketHeader { PacketType type; }`。
  - NetworkManager 收包時會把 `PacketHeader.type` 取出，填到 `ReceivedPacket.type`，因此「**送封包前一定要設 header.type**」。
- 主要封包：
  - Join：`C2S_JOIN_REQUEST`、`S2C_JOIN_ACCEPT (yourPlayerID, yourTeamID)`
  - Lobby：`PacketLobbyState { slots[8] }` 對應 `S2C_LOBBY_UPDATE`
  - Player sync：`PacketPlayerState`（位置、速度/旋轉、isSwimming/isDead...）
  - Shoot：`C2S_SHOOT`、`S2C_SHOOT_EVENT`
  - Game score/state：`S2C_GAME_STATE`、`S2C_GAME_START`
  - Kill event：`PacketKillEvent`

#### NetworkManager（network/NetworkManager.*）

- 職責：包裝 GameNetworkingSockets，提供「連線 + 收包 queue + 送包 API」。
- 關鍵欄位：
  - `ISteamNetworkingSockets* m_pInterface`
  - Server：`m_hListenSock`（listen socket）、`m_ClientConnections`（每個 client 的 connection handle）
  - Client：`m_hConnection`（連到 server 的 connection handle）
  - 收包：`std::queue<ReceivedPacket> m_PacketQueue`
  - 狀態：`m_IsServer / m_IsConnected / myPlayerID / myTeamID / playerWeaponMap ...`
- 關鍵方法：
  - `Initialize()`：初始化 GNS（Windows 也會做 WSAStartup）
  - `StartServer(port)`：建立 listen socket（UDP）
  - `Connect(host, port)`：DNS/IPv4/IPv6 resolve → 連線
  - `Update()`：
    - `RunCallbacks()`（處理連線狀態回呼）
    - `ReceiveMessagesOnConnection` 輪詢收包
    - 將每個包 push 進 `m_PacketQueue`
  - `Send(conn, data, size, reliable)`：傳送給指定 connection
  - `SendToServer(data, size, reliable)`：Client 端送給 server
  - `Broadcast(data, size, reliable, except)`：Server 廣播給所有 clients
- 互動方式：
  - main.cpp 每幀呼叫 `NetworkManager.Update()`
  - main.cpp 用 while loop 把 queue 拿完 → SceneManager.HandlePacket
  - Scene/World 在適當時機呼叫 SendToServer / Broadcast

---

### 3.7 gameplay：GameWorld/Player/Weapon（遊戲核心）

#### GameScene（scene/GameScene.*）

- 職責：負責「進入遊戲場景時」建立渲染資源與 GameWorld，並在每幀轉呼叫：
  - `world->Update(dt)`
  - `world->HandlePacket(pkt)`（OnPacket 內）
- 互動：
  - GameScene.OnEnter 建 shader、cameraObj、HUD、Scoreboard，再建立 `GameWorld`.
  - GameScene.Update：處理回到 Lobby（時間到或結束後 switch scene）。

#### GameWorld（gameplay/GameWorld.h）

- 職責：遊戲中的「總控」物件。集中管理：
  - 地圖與塗地：`Level`、`SplatMap(mapFloor,mapObstacle)`、`SplatPainter`、`SplatRenderer/Physics`
  - 玩家：`localPlayer`、`remotePlayers`（遠端玩家）
  - 投射物：`projectiles`、武器 spawn queue
  - 遊戲狀態：WorldState（PLAYING/FINISHED）、時間、分數
  - 網路資料流：在 Update 定時送 `C2S_PLAYER_STATE`；在 HandlePacket 收到封包時同步
- 與 Network 的互動（核心資料流）：
  - 每 0.05 秒送一次玩家狀態（位置/旋轉/泳姿/死亡等）：
    - Client：`SendToServer(PacketPlayerState, unreliable)`
    - Server：把自己的狀態包裝成 `S2C_WORLD_STATE` 然後 Broadcast（unreliable）
  - Client 射擊：送 `C2S_SHOOT (reliable)`；Server 收到後 broadcast `S2C_SHOOT_EVENT (reliable)`
  - Server 每 0.5 秒計算塗地比例，broadcast `S2C_GAME_STATE`（比分與剩餘時間）
- GameWorld 的封包處理策略（讀 HandlePacket 最清楚）：
  - A. 若自己是 Server：
    - 收到 `C2S_PLAYER_STATE`：改 type 成 `S2C_WORLD_STATE` 後廣播
    - 收到 `C2S_SHOOT`：改 type 成 `S2C_SHOOT_EVENT` 後廣播
    - 收到 `C2S_SPECIAL_ATTACK`：觸發效果並 broadcast 對應 `S2C_SPECIAL_ATTACK`
  - B. Client & Server 共用邏輯（收到 server 廣播包）：
    - `S2C_WORLD_STATE`：更新 remotePlayers 或同步狀態
    - `S2C_SHOOT_EVENT`：生成遠端子彈（忽略自己的）
    - `S2C_GAME_STATE`：更新 HUD/Scoreboard，並檢查是否 end game
    - `S2C_JOIN_ACCEPT`：設定自己的 playerID/team，更新武器顏色等
    - `S2C_KILL_EVENT`：顯示 kill log、若自己是 victim 觸發死亡

#### Player（gameplay/Player.h）

- 職責：單一玩家（local 或 remote）的「角色邏輯」：
  - 狀態機：ALIVE / SWIMMING / SHARKING / DEAD…（以 enum/變數為準）
  - 輸入：local player 讀 Input
  - 移動與跳躍：用 dt 更新 transform
  - 與塗地互動：查 `SplatMap::IsColorAt(...)` 或同類方法判斷站在敵方 ink 上，影響速度/狀態
  - 武器：持有 `Weapon* weapon`，由 Lobby/Join Accept 決定武器類型與顏色
- 與其他類別互動：
  - 需要 `Level` 取得高度或地形資訊（例如 GetHeightAt）
  - 需要 `SplatMap` 判斷 ink 分布，影響游泳/阻力/扣血等
  - 需要 HUD/Camera 引用來顯示或影響視角（camera follow）
  - 透過 Weapon 生成投射物 spawn requests（pendingSpawns）

#### Weapon（gameplay/Weapon.h）與子類

- 職責：把「按下攻擊」轉成一串投射物 spawn 資訊（SpawnInfo），交由 GameWorld 真正生成 Projectile。
  - Weapon::Update(...)（通常會處理射速、冷卻）
  - 子類 FireLogic（Shooter/Brush/Slosher）會 push 多個 SpawnInfo 到 `pendingSpawns`
- 互動：
  - GameWorld 在 Update 時 `CollectProjectiles(*localPlayer->weapon)` 把 pendingSpawns 轉成 Projectile
  - 同步面：GameWorld 會送 `C2S_SHOOT`（或其他）給 server，server 再廣播讓其他 client 也 SpawnRemoteProjectile

#### Projectile / Item / Enemy

- Projectile：子彈/炸彈/火箭等投射物，會與 SplatPhysics/Level 碰撞，命中後塗地或造成傷害。
- Item：場上道具，GameWorld 控制生成/重生計時。
- Enemy：AI（在 server 端也會被 broadcast 成一個 player state，ID 可能是 100）。

---

### 3.8 splat：塗地系統

#### SplatMap（splat/SplatMap.h）

- 職責：儲存 ink 的分布與計分資料。  
- 資料表示：
  - OpenGL 端：FBO + texture（用來在 shader 渲染地面時當 inkMap）
  - CPU 端：`gridData[100][100]`（離散網格，用於計分與「是否站在某隊 ink 上」這類快速判斷）
- 主要方法：
  - InitFBO：建立 texture 與 framebuffer
  - BindTexture(slot)：提供 SplatRenderer 在繪製地面時綁定
  - CalculatePercentages()：統計 gridData 內 team1/team2 佔比 → 分數

#### SplatPainter（splat/SplatPainter.h）

- 職責：把「某次射擊/爆炸/刷子」的 ink 投影到 SplatMap 的 texture 上。  
- 技術重點：
  - 使用一個 quad（quadVAO/quadVBO）
  - 用 splatShader 把 splat texture（筆刷形狀）畫到目標 FBO
- 互動：
  - GameWorld 在投射物命中或玩家攻擊時呼叫 painter 在 mapFloor/mapObstacle 上 paint

#### SplatRenderer（splat/SplatRenderer.h）

- 職責：渲染地面時，把 `map->textureID` 當成 shader uniform `inkMap`，讓地面顯示塗地效果。  
- 互動：
  - GameWorld.Render 或 Level.Draw 時，呼叫 SplatRenderer::RenderFloor(shader, floor, mapFloor)

#### SplatPhysics（splat/SplatPhysics.h）

- 職責：提供 ink 相關碰撞/取樣，或對應「站在敵方 ink 上減速/扣血」等。  
- 互動：
  - Player.UpdateLogic：查 mapFloor/mapObstacle 的 ink 影響移動/狀態
  - Projectile 命中：更新 ink 或觸發效果

---

## 4. 網路資料流（加入、Lobby、遊戲中同步）

以下用 ASCII sequence diagram 表示「誰送給誰、何時、送什麼」。

### 4.1 Join 流程（Client 加入）

```
Client                                         Server (Host)
  |                                             |
  |--- C2S_JOIN_REQUEST ----------------------->|
  |                                             | assign playerID / teamID
  |<-- S2C_JOIN_ACCEPT (yourPlayerID, teamID) --|
  |                                             |
  | (client 設定 myPlayerID/myTeamID)            |
```

### 4.2 Lobby 同步（顯示 8 格、紅/綠/灰）

Server 每隔一段時間（LobbyScene 內 timer）廣播 `S2C_LOBBY_UPDATE`：

```
Server: build PacketLobbyState { slots[8] }
  slots[i] = {playerID, teamID, weaponType, isReady}
Broadcast -> all clients

Client:
  OnPacket(S2C_LOBBY_UPDATE) -> GUIManager.UpdateLobbyState(pkt)
  GUI DrawLobbyCircles() 依 lobbySlots 著色
```

### 4.3 遊戲中同步（移動/射擊/比分）

#### 移動狀態（每 0.05 秒一次）

```
Client                                     Server
  |  PacketPlayerState (C2S_PLAYER_STATE)   |
  |---------------------------------------->|
  |                                         | outPkt = inPkt
  |                                         | outPkt.type = S2C_WORLD_STATE
  |                                         | Broadcast (unreliable)
  |<----------------------------------------|
  |      PacketPlayerState (S2C_WORLD_STATE)|
  |  更新 remotePlayers / 顯示              |
```

Server 自己的狀態也會以同樣方式 Broadcast；另外 server 也會把 AI 的狀態用 ID=100 形式送出去（GameWorld.Update 可看到）。

#### 射擊事件（可靠）

```
Client                             Server                            Other Clients
  | C2S_SHOOT (reliable)             |                                   |
  |--------------------------------->|                                   |
  |                                  | outPkt.type=S2C_SHOOT_EVENT       |
  |                                  | Broadcast (reliable)              |
  |<---------------------------------|-----------------------------------|
  | S2C_SHOOT_EVENT (reliable)       |   S2C_SHOOT_EVENT (reliable)      |
  | 生成遠端子彈（忽略自己）          |    生成子彈                        |
```

#### 比分/時間（每 0.5 秒，server-only 計算）

```
Server:
  scores = SplatMap.CalculatePercentages()
  PacketGameState {scoreTeam1, scoreTeam2, timeRemaining}
  Broadcast S2C_GAME_STATE

Clients:
  更新 Scoreboard/HUD
  若 timeRemaining==0 -> EndGame
```

---

## 5. Class Interaction

### 5.1 為何要把封包集中丟給 Scene/World？

因為你的遊戲有明確階段：
- Login：只需要處理連線/輸入
- Lobby：只需要處理 lobby 狀態更新與武器切換
- Game：需要大量即時同步

如果你把所有封包處理都寫在 NetworkManager，會讓 NetworkManager 變成「全知全能」，後期很難維護。現在的設計讓：
- NetworkManager 只做 transport（收/送/queue）
- Scene/World 才做 domain logic（如何解讀封包、如何改 UI/物件）

這是一個很典型的分層：Transport Layer vs Game Logic Layer。

### 5.2 為何 GameWorld 同時處理 Server 與 Client？

listen server 模式下，Host 其實同時扮演兩個角色：
- Server：收到 client 包後 broadcast 給全體
- 本機玩家（client view）：也要更新畫面、生成子彈、顯示分數

因此 GameWorld.HandlePacket 裡你會看到：
- 若 net.IsServer()：先處理 client->server 的包，並轉成 server->client 包廣播
- 不論是否 server：都要處理 S2C_* 的包，因為 server 本機也需要看到世界更新

---

## 6. Build & Run

### 6.1 建置
依專案的 CMake/vcpkg 設定建置即可（略）。

### 6.2 執行模式
- 單機測試：不連線，直接跑 GameWorld（程式內有 local test 分支）
- Host（listen server）：Login 按 Host Server → StartServer(port) → 進 Lobby
- Client：輸入 `domain:port` 或 `ip:port` → Connect → 進 Lobby

### 6.3 外網用 playit.gg
Host 端：
1) 遊戲 StartServer(7777) 監聽 UDP 7777  
2) playit 建 UDP tunnel：local address 127.0.0.1 / local port 7777  
3) 把 playit 給的 `domain:externalPort` 發給其他玩家

Client 端：
- 在 Server Address 輸入 `domain:externalPort` 直接連

---

## 7. Components

- main.cpp：主迴圈（Network → Scene → Render → UI）  
- SceneManager：場景切換 + 轉呼叫  
- LoginScene：Host/Connect → 切 Lobby  
- LobbyScene：Server 廣播 lobby slots；Client 更新 UI；StartGame 切 GameScene  
- GameScene：建立 shader/camera/HUD/Scoreboard + GameWorld  
- GameWorld：玩家/子彈/塗地/分數/網路同步總控  
- NetworkManager：GNS 連線、收包 queue、Send/Broadcast  
- GUIManager：ImGui 畫面與 lobbySlots 狀態  
- SplatMap/Painter/Renderer：塗地資料結構 + 画到地面 + 計分  
- Player/Weapon：玩家狀態機與武器生成投射物  
