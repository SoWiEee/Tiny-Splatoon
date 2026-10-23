# Device visual source contract

The device visual catalog is generated from the immutable `splat-ref` game sources by `tools/export-device-visuals.mjs`. It contains 29 model kinds for both team palettes (58 kind/team records). Each record preserves the source Three.js object hierarchy, local transforms, visibility, shadow flags, geometry attributes, and material assignments. Geometry is stored in the shared `TSMESH01` format; source materials are exported as GLSL plus their default uniforms and loaded through the renderer's `CharacterData` mesh cache and `CharacterShader` path. The generated `assets/reference/devices/manifest.json` records hashes for the source modules and exported files.

The catalog is grouped as follows:

| Group | Kinds |
| --- | --- |
| Sub/device models (15) | `beacon`, `bomb`, `boomerang`, `burst`, `curtain`, `mine`, `mist`, `scan`, `seeker`, `shaker`, `sprinkler`, `sticky`, `torpedo`, `tracer`, `waddle` |
| Special and held prop models (7) | `brolly`, `crab`, `jetpack`, `kraken`, `missile`, `speaker`, `stamp` |
| Persistent world shapes and indicators (7) | `curtainSheet`, `mistCloud`, `scanCloud`, `stormBase`, `stormTop`, `torpedoRing`, `waddleRing` |

The asset export uses the source sub system and special-prop builders, source character weapon definitions, and source kit model builders. Per-team model records retain the source team tint and material setup. `tracer-ribbon.vert` and `tracer-ribbon.frag` are compiled from the shader strings in `splat-ref/src/game/kits/tracer.js` through the shared character material shader exporter.

## Runtime motion and lifetime

`DeviceRenderer::Update` consumes a read-only snapshot of live `CombatWorld` devices and the current actor poses. It does not invoke gameplay collision, damage, paint, or device-deployment methods. The renderer updates only visual motion state keyed by device or actor identity, clears that state when the world clock moves backwards, and removes state after its source object disappears. Device rendering is distance limited; tracer tails also have explicit point and tail-count limits.

The runtime maps source model pieces onto the source gameplay phases instead of replacing their meshes with generic shapes:

| Visual | Runtime inputs and motion |
| --- | --- |
| Ordinary sub/device props | Current kind, team, position, heading, normal, state, age, timer, fuse, and equipment parameters determine the root transform and lifecycle visibility. Source node hierarchies, mesh transforms, and material shaders remain in use. |
| Torpedo | `fly`, `unfold`, and deployed phases drive roll, fin opening, propeller rotation, and glow. The target ring follows the live target and source ring mesh. |
| Waddle | Position deltas provide movement speed and walking phase, including when the gameplay velocity field is zero; the walking pose and source ring use the current sensing/lock state. |
| Boomerang | Flight, hover, return, and orbit phases drive spin rate and smoothed tilt. The source blur node is enabled during runtime drawing. |
| Tracer | The source bolt orientation follows its velocity; its trail uses timestamped path points from `CombatDevice::trailPoints` and the source ribbon shaders. |
| Curtain | The source sheet grows during deployment and its runtime alpha follows the current HP fraction, using the equipment HP value when a device record has no maximum. |
| Scan and mist clouds | Source-built cloud geometry follows the live position and runtime growth/fade envelope; the mist's source cloud nodes rotate and pulse. |
| Speaker | The source cone node pulses during its active phases. |
| Kraken, crab, and jetpack actors | The live `SpecialPose`, actor movement, weapon pose, and tank transform drive the source special model and its animated nodes. Crab leg stride uses actor movement speed. |

The tracer ribbon follows the source rules in `splat-ref/src/game/kits/tracer.js`. It records up to 160 path points; a point at the same position refreshes its timestamp. During rendering, each point's side vector is the normalized cross product of the local path tangent and the camera-to-point vector, with world up as a degenerate fallback. Its half-width is `0.085 * (1 + 0.6 * (1 - age))`, where `age` is clamped from `(now - pointTime) / trailLife`. The source fragment shader squares a smooth edge falloff, adds a narrow core, fades alpha by `pow(1 - age, 1.5)`, and blends the team color toward white near the core.

As in the source, old points are trimmed while retaining a segment start; after the bolt ends, its tail remains drawable until the last point is older than `trailLife`. The native renderer caps each tail at 160 points and its cache at 128 tails, evicting the least recently seen tail above that bound. Tails farther than 65 units from the camera are not submitted. These renderer bounds prevent unbounded visual-state growth and do not change combat lifetime or trail damage rules.

## Fidelity and known differences

The catalog restores source geometry, hierarchy, team materials, and source-exported material shaders for the listed models. Persistent special/world shapes that are rendered by `WorldEffects` have their own scope in [the World FX source contract](worldfx-source-contract.md). This document does not claim whole-frame or whole-pixel parity with the browser game. Camera, renderer state, exposure, environment maps, shadow filtering, and the exact browser runtime timing/random stream can still produce different pixels.

Several motion paths intentionally remain approximations because the native combat snapshot does not expose all source animation inputs. In particular, ordinary devices in the `fly` state use a generic age-driven X/Z spin (`age * 8`, `age * 5`) instead of source-specific per-instance spin values; the source bomb's randomized spin rate and fuse-dependent spin behavior therefore are not reproduced. Some deployed prop motion and special actor secondary motion are likewise reconstructed from available state and elapsed time rather than from every source object's internal animation clock. Cloud templates use deterministic exporter-side randomness, not the original live spawn RNG stream, so their individual puff placement is stable but not matched to each source runtime cloud instance. The source meshes and material parameters are retained, but these runtime differences remain visible fidelity limits.

The focused GPU test in `tests/device_visual_gpu_test.cpp` is intended to draw all 58 catalog records and check for non-background pixels and GL errors. It also exercises sub-device cleanup and tracer ribbon creation, visibility, post-bolt lifetime, and expiry. Passing that test demonstrates source asset loading and the tested near-camera paths; it is not a pixel-parity certification or a substitute for the full all-gear lifecycle and visual comparison matrix.

Regenerate or check the content-addressed assets with:

```powershell
node tools/export-device-visuals.mjs --check
```

Material keys include the expanded vertex shader, fragment shader and serialized uniform defaults. Both teams can share shader code while retaining different diffuse/emissive parameters; duplicate asset names with different bytes are rejected. The GPU fixture magnifies small sub props and compares orange/blue pigment for the bomb and pixel differences for six team-colored model families. This corrects the initial shader-only-key collision that made both teams blue. The final focused check rendered all 58 records (411,979 visible pixels) and 473 tracer ribbon pixels, then verified tail expiry.
