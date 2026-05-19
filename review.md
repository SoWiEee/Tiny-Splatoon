# Code Review: `Tiny-Splatoon`

> Review based on the current repository state. Focus areas: correctness, memory usage, rendering/network efficiency, style, readability, maintainability, and extensibility.

---

## Summary

The project already has a clear gameplay split (`Scene` / `GameWorld` / `NetworkManager` / `Splat`), but there are several high-risk correctness issues that should be fixed before broader refactoring:

1. There is an out-of-bounds write in lobby state handling.
2. The end-of-match scene transition can dereference a null `GUIManager*`.
3. The server sends two different `S2C_JOIN_ACCEPT` packets during one connection flow.
4. Score percentages are computed with the wrong denominator.
5. Incoming packet payloads are cast without validating length.

After those are fixed, the next biggest gains come from:

- reducing `GameWorld` size and duplicated packet logic
- replacing manual ownership with RAII
- improving network and projectile update hot paths
- removing inconsistent constants and slot-count duplication

---

## Findings

### 1. Critical: lobby packet handling writes past fixed-size arrays

**Location**

- [gui/GUIManager.hpp](/C:/Users/MintIce/Programming/Tiny-Splatoon/gui/GUIManager.hpp:23)
- [gui/GUIManager.cpp](/C:/Users/MintIce/Programming/Tiny-Splatoon/gui/GUIManager.cpp:39)
- [network/NetworkProtocol.hpp](/C:/Users/MintIce/Programming/Tiny-Splatoon/network/NetworkProtocol.hpp:63)

**Problem**

`GUIManager::lobbySlots` is declared as `LobbySlotInfo lobbySlots[6]`, and `PacketLobbyState::slots` is also `slots[6]`, but `GUIManager::UpdateLobbyState()` copies `8` elements:

```cpp
for (int i = 0; i < 8; i++) {
    lobbySlots[i] = pkt.slots[i];
}
```

This is immediate undefined behavior and can corrupt adjacent state.

**Impact**

- memory corruption
- random UI/network bugs
- hard-to-reproduce crashes

**Recommended fix**

- define one shared constant such as `constexpr int kLobbySlots = 6;`
- use it in packet structs, GUI arrays, loops, and layout code

**Example**

```cpp
constexpr int kLobbySlots = 6;

struct PacketLobbyState {
    PacketHeader header;
    LobbySlotInfo slots[kLobbySlots];
};

void GUIManager::UpdateLobbyState(const PacketLobbyState& pkt) {
    for (int i = 0; i < kLobbySlots; ++i) {
        lobbySlots[i] = pkt.slots[i];
    }
}
```

### 2. Critical: match end can switch to a `LoginScene` with null GUI dependency

**Location**

- [scene/GameScene.cpp](/C:/Users/MintIce/Programming/Tiny-Splatoon/scene/GameScene.cpp:64)
- [scene/LoginScene.hpp](/C:/Users/MintIce/Programming/Tiny-Splatoon/scene/LoginScene.hpp:12)
- [scene/LoginScene.hpp](/C:/Users/MintIce/Programming/Tiny-Splatoon/scene/LoginScene.hpp:18)

**Problem**

`GameScene::Update()` switches to `std::make_unique<LoginScene>()`, but the default constructor leaves `gui` unset. `LoginScene::OnEnter()` immediately calls `gui->SetState(...)` without a null check.

**Impact**

- deterministic crash after a finished match

**Recommended fix**

- make `GUIManager&` a required constructor dependency
- remove the default constructor
- pass the same GUI instance through scene transitions

**Example**

```cpp
class LoginScene : public Scene {
public:
    explicit LoginScene(GUIManager& guiManager) : gui(guiManager) {}

    void OnEnter() override {
        glfwSetInputMode(glfwGetCurrentContext(), GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        gui.SetState(UIState::LOGIN);
    }

private:
    GUIManager& gui;
};
```

### 3. Critical: server sends two different player IDs during connect

**Location**

- [network/NetworkManager.cpp](/C:/Users/MintIce/Programming/Tiny-Splatoon/network/NetworkManager.cpp:278)
- [network/NetworkManager.cpp](/C:/Users/MintIce/Programming/Tiny-Splatoon/network/NetworkManager.cpp:293)
- [network/NetworkManager.cpp](/C:/Users/MintIce/Programming/Tiny-Splatoon/network/NetworkManager.cpp:300)
- [network/NetworkManager.cpp](/C:/Users/MintIce/Programming/Tiny-Splatoon/network/NetworkManager.cpp:304)

**Problem**

In `k_ESteamNetworkingConnectionState_Connecting`, the server accepts the connection and sends `S2C_JOIN_ACCEPT` with `yourPlayerID = (int)pInfo->m_hConn`.

Later, in `k_ESteamNetworkingConnectionState_Connected`, the server sends another `S2C_JOIN_ACCEPT` with a different logical ID from `m_NextClientID++`.

This means one client connection can receive two different identities.

**Impact**

- unstable player identity
- team assignment inconsistency
- lobby state and world state can disagree

**Recommended fix**

- assign player ID exactly once
- keep a `connection -> playerID` map
- only send `S2C_JOIN_ACCEPT` after the logical ID is finalized

**Example**

```cpp
// Pseudocode
case Connected:
    int playerID = AllocatePlayerID(pInfo->m_hConn);
    PacketJoinAccept pkt{ ... playerID, ResolveTeam(playerID) };
    Send(pInfo->m_hConn, &pkt, sizeof(pkt), true);
    break;
```

### 4. High: score percentage math is incorrect

**Location**

- [splat/SplatMap.hpp](/C:/Users/MintIce/Programming/Tiny-Splatoon/splat/SplatMap.hpp:71)
- [splat/SplatMap.hpp](/C:/Users/MintIce/Programming/Tiny-Splatoon/splat/SplatMap.hpp:74)

**Problem**

`CalculatePercentages()` counts painted cells in a `100 x 100` CPU grid but divides by `width * height` (`1024 * 1024`).

That denominator does not match the counted domain.

**Impact**

- displayed scores are much smaller than the actual painted ratio
- endgame result and HUD percentages are misleading

**Recommended fix**

- divide by `GRID_SIZE * GRID_SIZE`
- or rename the function if the intent is not true percentage

**Example**

```cpp
int totalCells = GRID_SIZE * GRID_SIZE;
return {
    static_cast<float>(count1) / totalCells,
    static_cast<float>(count2) / totalCells
};
```

### 5. High: packet payloads are cast without validating message size

**Location**

- [scene/LobbyScene.hpp](/C:/Users/MintIce/Programming/Tiny-Splatoon/scene/LobbyScene.hpp:98)
- [scene/LobbyScene.hpp](/C:/Users/MintIce/Programming/Tiny-Splatoon/scene/LobbyScene.hpp:107)
- [gameplay/GameWorld.hpp](/C:/Users/MintIce/Programming/Tiny-Splatoon/gameplay/GameWorld.hpp:565)
- [gameplay/GameWorld.hpp](/C:/Users/MintIce/Programming/Tiny-Splatoon/gameplay/GameWorld.hpp:575)
- [gameplay/GameWorld.hpp](/C:/Users/MintIce/Programming/Tiny-Splatoon/gameplay/GameWorld.hpp:614)

**Problem**

Most packet handlers do:

```cpp
auto* pkt = (PacketGameState*)received.data.data();
```

without checking `received.data.size() >= sizeof(PacketGameState)`.

**Impact**

- malformed or truncated packets can cause undefined behavior
- unsafe network boundary

**Recommended fix**

- centralize packet decoding in a checked helper
- reject packets that are too short

**Example**

```cpp
template <typename T>
const T* AsPacket(const ReceivedPacket& pkt) {
    if (pkt.data.size() < sizeof(T)) return nullptr;
    return reinterpret_cast<const T*>(pkt.data.data());
}
```

### 6. High: server broadcasts a fixed `timeRemaining = 180.0f`

**Location**

- [gameplay/GameWorld.hpp](/C:/Users/MintIce/Programming/Tiny-Splatoon/gameplay/GameWorld.hpp:289)
- [gameplay/GameWorld.hpp](/C:/Users/MintIce/Programming/Tiny-Splatoon/gameplay/GameWorld.hpp:406)
- [gameplay/GameWorld.hpp](/C:/Users/MintIce/Programming/Tiny-Splatoon/gameplay/GameWorld.hpp:416)

**Problem**

The world decrements `gameTimeRemaining`, but score sync packets always send:

```cpp
scorePkt.timeRemaining = 180.0f;
```

**Impact**

- packet contents do not represent actual world state
- future client-side authority or reconnect logic will be wrong
- debugging becomes harder because state packets lie

**Recommended fix**

- send `gameTimeRemaining`
- make one side authoritative for match clock

### 7. Medium: only one network message per connection is drained per frame

**Location**

- [network/NetworkManager.cpp](/C:/Users/MintIce/Programming/Tiny-Splatoon/network/NetworkManager.cpp:233)

**Problem**

`ReceiveMessagesOnConnection(conn, &pIncomingMsg, 1)` reads only one message per connection per frame.

Under burst traffic, backlog accumulates even if the local frame rate is healthy.

**Impact**

- artificial latency
- packet queue growth under load
- visible delay for shooting and state sync

**Recommended fix**

- drain in a loop
- or read a batch array per call

**Example**

```cpp
ISteamNetworkingMessage* msgs[32];
int count = 0;
do {
    count = m_pInterface->ReceiveMessagesOnConnection(conn, msgs, 32);
    for (int i = 0; i < count; ++i) {
        // decode and enqueue
        msgs[i]->Release();
    }
} while (count > 0);
```

### 8. Medium: projectile update path allocates a fresh target vector for every projectile

**Location**

- [gameplay/GameWorld.hpp](/C:/Users/MintIce/Programming/Tiny-Splatoon/gameplay/GameWorld.hpp:765)
- [gameplay/GameWorld.hpp](/C:/Users/MintIce/Programming/Tiny-Splatoon/gameplay/GameWorld.hpp:773)

**Problem**

`UpdateProjectiles()` rebuilds `std::vector<Entity*> targets` inside the projectile loop. This adds repeated allocations and repeated traversal of `remotePlayers`.

**Impact**

- avoidable CPU overhead in the hottest gameplay loop
- scales poorly with projectile count and player count

**Recommended fix**

- build the target list once per frame
- reuse a scratch buffer with `reserve(...)`

**Example**

```cpp
std::vector<Entity*> targets;
targets.reserve(2 + remotePlayers.size());
targets.push_back(localPlayer.get());
if (enemyAI) targets.push_back(enemyAI.get());
for (auto& [id, rp] : remotePlayers) targets.push_back(rp.get());

for (auto& proj : projectiles) {
    for (Entity* target : targets) {
        ...
    }
}
```

### 9. Medium: ownership is inconsistent and partially manual

**Location**

- [gameplay/Player.hpp](/C:/Users/MintIce/Programming/Tiny-Splatoon/gameplay/Player.hpp:76)
- [gameplay/Player.hpp](/C:/Users/MintIce/Programming/Tiny-Splatoon/gameplay/Player.hpp:112)
- [gameplay/Enemy.hpp](/C:/Users/MintIce/Programming/Tiny-Splatoon/gameplay/Enemy.hpp:24)
- [gameplay/Enemy.hpp](/C:/Users/MintIce/Programming/Tiny-Splatoon/gameplay/Enemy.hpp:31)
- [gameplay/Enemy.hpp](/C:/Users/MintIce/Programming/Tiny-Splatoon/gameplay/Enemy.hpp:40)

**Problem**

`Player` and `Enemy` own objects through raw pointers (`Weapon*`, `GameObject*`) and manual `delete`. `Enemy` deletes `weapon` but not `shadow` or `visualBody`, so if that path is enabled it leaks memory.

**Impact**

- leak risk
- harder exception safety and lifetime reasoning
- refactors become fragile

**Recommended fix**

- use `std::unique_ptr<Weapon>`
- keep owned visuals in `std::unique_ptr<GameObject>` or hand ownership to `GameWorld`

### 10. Medium: `GameWorld` is too large and contains duplicated packet logic

**Location**

- [gameplay/GameWorld.hpp](/C:/Users/MintIce/Programming/Tiny-Splatoon/gameplay/GameWorld.hpp:558)
- [gameplay/GameWorld.hpp](/C:/Users/MintIce/Programming/Tiny-Splatoon/gameplay/GameWorld.hpp:661)

**Problem**

`GameWorld.hpp` is carrying implementation for:

- world init
- simulation
- rendering
- networking
- kill processing
- item spawning
- projectile physics
- special attacks

It also contains both `HandlePacket(...)` and an unused duplicated `ProcessNetworkPackets()`.

**Impact**

- very high edit risk
- difficult review and testing
- more likely to introduce regressions in unrelated systems

**Recommended fix**

- move implementations to `GameWorld.cpp`
- split into subsystems such as `WorldNetSync`, `ProjectileSystem`, `CombatSystem`, `ItemSystem`
- delete duplicated dead code

### 11. Low: visual state API is inconsistent and partially dead

**Location**

- [gameplay/Player.hpp](/C:/Users/MintIce/Programming/Tiny-Splatoon/gameplay/Player.hpp:105)
- [gameplay/Player.hpp](/C:/Users/MintIce/Programming/Tiny-Splatoon/gameplay/Player.hpp:191)
- [gameplay/Player.hpp](/C:/Users/MintIce/Programming/Tiny-Splatoon/gameplay/Player.hpp:392)

**Problem**

`Player` stores `visualBody`, `visualHuman`, and `visualSquid`, but only `visualHuman` / `visualSquid` are wired by `SetupVisuals()`. `GetVisualBody()` returns `visualBody`, which stays `nullptr`.

Code that expects `GetVisualBody()` to represent the visible mesh silently does nothing.

**Impact**

- misleading API
- future bugs when trying to recolor or animate the local player

**Recommended fix**

- remove `visualBody` if obsolete
- or define a single authoritative visual interface

### 12. Low: constants and behavior are spread as magic numbers

**Location**

- [gameplay/GameWorld.hpp](/C:/Users/MintIce/Programming/Tiny-Splatoon/gameplay/GameWorld.hpp:58)
- [gameplay/Player.hpp](/C:/Users/MintIce/Programming/Tiny-Splatoon/gameplay/Player.hpp:24)
- [scene/Level.hpp](/C:/Users/MintIce/Programming/Tiny-Splatoon/scene/Level.hpp:25)

**Problem**

Match time, map size, cooldowns, projectile speeds, UI timings, and radii are embedded throughout the code.

**Impact**

- tuning requires editing many files
- difficult to build multiple weapon/map variants
- weak data-driven extensibility

**Recommended fix**

- centralize tunables in config structs or data files

---

## Example Improvement Directions

### A. Centralize compile-time constants

Use a shared constants header for values that must stay in sync:

```cpp
namespace GameConstants {
    inline constexpr int kLobbySlots = 6;
    inline constexpr float kMatchDurationSeconds = 180.0f;
    inline constexpr float kNetworkSyncInterval = 0.05f;
}
```

This alone would remove several current inconsistencies.

### B. Make network decoding safe by construction

Instead of casting in every scene/world handler:

```cpp
template <typename T>
std::optional<std::reference_wrapper<const T>> DecodePacket(const ReceivedPacket& pkt) {
    if (pkt.data.size() < sizeof(T)) return std::nullopt;
    return std::cref(*reinterpret_cast<const T*>(pkt.data.data()));
}
```

Then call:

```cpp
if (auto p = DecodePacket<PacketGameState>(received)) {
    finalScoreTeam1 = p->get().scoreTeam1;
}
```

### C. Replace manual ownership with `unique_ptr`

Current code:

```cpp
Weapon* weapon = nullptr;
~Player() { if (weapon) delete weapon; }
```

Safer version:

```cpp
std::unique_ptr<Weapon> weapon;

void EquipWeapon(std::unique_ptr<Weapon> newWeapon) {
    weapon = std::move(newWeapon);
}
```

Benefits:

- no manual delete
- no double-free risk during refactor
- ownership is explicit

### D. Split `GameWorld` by responsibility

A practical first cut:

- `GameWorld`:
  owns top-level state and orchestrates subsystems
- `ProjectileSystem`:
  spawn, simulate, resolve hit, paint ink
- `WorldReplication`:
  encode/decode game packets
- `MatchState`:
  score, timer, finish flow

This will improve:

- compile time
- testability
- onboarding readability

### E. Reduce hot-path allocations

Current projectile collision checks repeatedly rebuild containers. Use frame scratch buffers:

```cpp
class GameWorld {
    std::vector<Entity*> frameTargets;
};

void BuildFrameTargets() {
    frameTargets.clear();
    frameTargets.reserve(2 + remotePlayers.size());
    ...
}
```

This is a straightforward CPU-side optimization with low refactor cost.

---

## Suggested Fix Order

1. Fix slot-count mismatch and all related loops.
2. Fix `LoginScene` dependency and end-of-match transition.
3. Fix duplicate join-accept logic and introduce `connection -> playerID` mapping.
4. Add packet-size validation helpers.
5. Correct score percentage calculation and authoritative match timer sync.
6. Drain network message queues fully each frame.
7. Replace raw ownership in `Player` / `Enemy`.
8. Split `GameWorld` and remove duplicated packet handling code.

---

## Review Scope Note

This review is static-analysis only. I did not run gameplay, profiling, or multiplayer soak tests in this pass.

