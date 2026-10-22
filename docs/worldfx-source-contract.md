# World FX source contract

The World FX port separates short lived `fx.js` pools from persistent world specials. `WorldEffects::Update` reads world state and drains `CombatWorld` visual events once. Particle collisions can request cosmetic paint specks and surface ripples; they never change scoring ownership, damage actors, or mutate gameplay collision state. `lastEvents()` exposes that frame's drained events to the screen-effects and audio adapters.

## Pooled recipes

`WorldFxRecipes.cpp` ports the reference pooled recipes `burst`, `explosion`, `slamLaunch`, `slamWave`, `stormStart`, `rainSheet`, `formPop`, `dive`, `emerge`, `wake`, and dry/own-ink/enemy-ink `footstep` cases. The 13-case native evaluator compares full pool records, render attributes after the first 1/60-second update, and expiration after 181 updates. The fixture exporter patches `Math.random` before loading `fx.js`, resets its xorshift32 state to `0x6d2b79f5` for each recipe, and preserves source call order.

For each fixture key, the oracle requires matching pool counts, scalar values, and Float32 values. The only tolerance is ordered Float32 bit distance at most one ULP for the explicitly listed trigonometry-derived positions and velocities in `tests/worldfx_oracle.test.mjs`; all other arrays are bit-exact. The source hash recorded in the fixtures is `9e8a646a6154824b64f9b35eeab1e4347f3bdb282ff7c337645dca14f8b88bc8`.

`WorldFxTransitions.hpp` keeps the reference's kid/squid form-pop event separate from ink submergence. The form pop is suppressed for fresh spawns, super-jump, special poses, and own-ink entry; swimming into ink emits `dive`, while leaving swim emits `emerge`. Entering climb is not treated as a dive, and leaving climb is not mislabeled as an emerge. The actor swim-spray cadence follows `Actor.update()`: horizontal speed above 2 m/s, accumulated updates over 0.05 seconds, and the source's 45 m camera range. `StageRenderer::UpdateSwimWake` separately supplies the four source ink-surface wake slots and mound shader inputs for swim and climb forms.

The source fixture calls `fx.js` without the app's `onRipple` paint callback, so its transient ripple records use the source FX ring fallback. Production now connects the matching paint-ripple callback to the existing level shader, with the source 24-slot priority/expiry table. Walking droplets use cosmetic specks on collision. Live rig foot plants drive the walking recipe, with a polling fallback for callers without a rig. The production surface GPU check exercises the transient ripple separately from the continuous submerged wake; details are in [the render contract](render-pipeline-source-contract.md#walking-ink-and-surface-ripples-2026-10-22).

Run the pool check with:

```powershell
node tests/worldfx_oracle.test.mjs build/worldfx_eval.exe
node tools/export-worldfx-source-fixtures.mjs --check
```

## Persistent special geometry and shaders

`tools/export-worldfx-assets.mjs` serializes reference `BufferGeometry` data into the existing `TSMESH01` content-addressed mesh format consumed through the shared `CharacterData` cache. Its generated table is `src/game/WorldFxAssetData.hpp`. The exporter includes the source `special-props.js`, `character-weapons.js`, and `weapons.js` hashes and checks speaker/missile prop meshes against `assets/reference/characters/catalog.json`.

The exported source geometry is:

- speaker body, ink, glow, woofer cone and cone ink from `getSpecialProp('speaker')`;
- missile body, ink and glow from `getSpecialProp('missile')`;
- stamp body and ink from `getWeaponDef('sp_stamp')`;
- funnel `CylinderGeometry(1, 1, 1, 28, 12, true)` translated to a bottom origin;
- speaker beam `CylinderGeometry(1, 1, 1, 24, 1, true)` translated and rotated to face +Z;
- bubble/orb `IcosahedronGeometry(1, 4)`;
- storm cloud `IcosahedronGeometry(1, 3)`;
- warning ring `RingGeometry(0.86, 1, 48)` rotated onto the ground plane.

`WorldEffects.cpp` contains direct GLSL ports of `specials.js` `SWIRL_VS/FS`, `BEAM_FS`, and `BUBBLE_VS/FS`. Tornado and zooka world views supply the original taper, wobble, scroll speed, size, vertical envelope, rotation speed, and fade values. Howl Box visuals use the source speaker/missile/stamp meshes and the reference beam, bubble and warning-ring geometry.

The reference cloud contains fifteen puffs and uses the source positions, size ranges, team color lerps, bob motion, and growth/fade envelope. `WorldObjectView` does not carry the original `Math.random` stream used at cloud spawn, so the visual-only port derives a stable seed from the immutable cloud landing position and team. This keeps the cloud stable across frames without advancing gameplay RNG; individual puff random values therefore are not source-stream exact.

Prop vertex attributes and geometry are retained, but their final `MeshStandardMaterial`, `getInkMaterial`, and emissive rendering are represented by a compact lit material shader rather than the original full character material pipeline. Bubble/tornado/beam source shader equations are ported directly. These prop material and cloud-seed differences are known visual differences, not claimed source-exact matches.

Run the mesh export check with:

```powershell
node tools/export-worldfx-assets.mjs --check
```

The latest export contains 15 mesh parts. Source hashes recorded by that generated table are `special-props.js` `c8fd279ad5869e1738b886abb57046562b8a10dc70c3c15f2ad5ff680a6fbdbd`, `character-weapons.js` `2b7e4c1054203df2b9adf4586bdc1dcd11a961db7b825194524c232e31efb870`, and `weapons.js` `2ec02b036376fdab0fda3556374d92db945b0f0409b311fe2e80b64418b79298`.

The focused source-fixture and mesh-export checks pass. The full Release app builds. `reference_worldfx_gpu` drives real player inputs to create a tornado, a held bubble and a Howl Box speaker/beam. It checks their persistent source shapes, submits the exported meshes and shaders to a hidden OpenGL 4.5 context, requires changed framebuffer pixels and no GL error. This exercises the near-camera draw path that the distant overview benchmark can cull; it does not certify every special's final material appearance.
