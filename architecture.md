# Architecture Notes

## GameWorld Responsibility Split

Current state after the `GameWorld.hpp -> GameWorld.hpp + GameWorld.cpp` refactor:

- `GameWorld`
  - Owns top-level match state, world resources, entities, and orchestration.
  - Remains the integration point for scene update/render flow.

- Match lifecycle
  - `Update()`
    - thin dispatcher by `WorldState`
  - `UpdateActiveMatch()`
    - main gameplay tick orchestration
  - `UpdateFinishedMatch()`
    - post-match countdown and frozen-player handling
  - `EndGame()`
    - authoritative match completion and final score broadcast

- Local gameplay flow
  - `HandleLocalAbilityRequests()`
    - resolves queued local bomb / rocket / shark actions into spawned gameplay events
  - `UpdateItems()`
    - item respawn and pickup handling
  - `UpdateRemotePlayers()`
    - interpolation tick for replicated players
  - `UpdateVisualEntities()`
    - updates visual-only objects managed by the world

- Networking
  - `HandlePacket()`
    - top-level packet dispatcher
  - `HandleServerPacket()`
    - server-only handling of client-originated packets
  - `HandleSharedPacket()`
    - shared client/server handling of replicated packets
  - `UpdateNetworkSync()`
    - periodic sync scheduling
  - `SyncLocalPlayerState()`
    - local player replication payload assembly
  - `SyncAiState()`
    - AI replication payload assembly
  - `BroadcastScoreState()`
    - authoritative score/time sync packet emission

- Combat / projectile path
  - `CreateProjectile()`
    - projectile construction
  - `SendShootPacket()`
    - outbound shoot replication
  - `SpawnRemoteProjectile()`
    - replicated projectile instantiation
  - `UpdateProjectiles()`
    - projectile physics, collision, explosion, and paint resolution
  - `CheckCollision()`
    - entity hit test helper
  - `ProcessKillEvent()`
    - kill attribution and broadcast

- World presentation
  - `Render()`
    - world draw orchestration
  - `CreateRemotePlayer()`
    - replicated player visual/entity creation
  - `SpawnDeathSplat()`
    - death paint effect helper

## Remaining High-Value Split Targets

- Extract `Render()` into smaller private helpers:
  - terrain/obstacle rendering
  - entity rendering
  - shadow rendering
  - particle rendering

- Extract `UpdateProjectiles()` into a dedicated projectile/combat module or helper group:
  - projectile target gathering
  - direct-hit resolution
  - rocket explosion resolution
  - bomb explosion resolution
  - floor/obstacle paint resolution

- Extract match-state logic further:
  - score/timer authority
  - endgame transition
  - final result computation

## Non-Goals For This Refactor Stage

- No gameplay rule changes
- No protocol shape changes
- No data-driven config system yet
- No subsystem class explosion before helper boundaries are stable
