# Native stage visual source contract

The native stage renderer uses exported stage geometry, source material shaders, captured source texture data, and a continuous ink atlas. This restores the material and paint detail that was missing when stages rendered as plain-colored face blocks and square ownership cells. It records the implemented source path and the checks that support it; it does not claim whole-scene pixel parity.

## Source and exported assets

`tools/export-stage-visuals.mjs` evaluates the locked `splat-ref` stage builders and writes `assets/reference/stages/`. It exports 23 stage/mode mesh packs across the eight stage layouts and the supported Turf, Zones, and Boss variants. Each pack contains the actual `Level.buildGeometry` output, including its exposed-edge quarter-round bevel strips and octant corners, plus source PropKit high-quality meshes merged or instanced at their authored placements. The level and PropKit geometry are baked into world space. The source cloth material and atlas are retained, while cloth geometry is exported at the canonical pose.

The level material shader is expanded from the source `levelMaterial.js` hooks and bundled Three.js shader chunks. PropKit material variants retain their source material hooks and draw state, including map UVs, transparency, alpha test, alpha-to-coverage, blending, depth writes, culling and polygon offset where used. The native path binds the captured source albedo, normal, ORM, mural and PropKit textures, the source TL lookup, the character DFG lookup and the scene shadow map. It initializes the DFG texture as RG16F; leaving that lookup unbound makes the source physical BRDF produce invalid output.

`tools/export-stage-textures.mjs` captures 14 actual source texture packs: the albedo, normal and ORM GPU texture arrays (46 material layers each at 512×512), the PropKit atlas, chain and blob Canvas textures, and eight stage mural atlases. `assets/reference/stage-textures/metadata.json` records the dimensions, source hashes and per-pack hashes. Pack files contain zlib-compressed `TSTEX001` data with a 24-byte header followed by bottom-up RGBA8 pixels. The native loader checks the header and dimensions before creating sRGB or linear OpenGL textures. The source texture capture uses the actual browser WebGL render target for the material arrays and captured Canvas pixels for the Canvas assets; generated or blank replacement maps are not used in this path.

## Continuous source ink

`FacePaint` continues to own the gameplay coverage grid and scoring. It also queues visual splats and clears for `StageRenderer`; those events feed the source `paint.js` vertex and fragment shaders into a 4096×4096 RGBA paint atlas. The shader draws spreading splats, source splat shapes and wall drips, then reduces the atlas wetness channel over time. Coverage and team color remain present as the ink dries. The renderer uploads and advances this visual atlas every frame, including frames without a CPU grid version change. Diagnostic displays 1, 2 and 3 keep their existing ownership and geometry behavior.

The GPU fixture verifies both representations: a gameplay splat updates the CPU scoring path and creates fractional, covered, wet source ink in the GPU atlas; repeated frame updates retain coverage while lowering wetness; and the wet and dry renders differ in visible pixels. `ReadVisualInk` is a test/diagnostic readback of the GPU atlas, not an alternate gameplay state.

## Evidence and commands

The generated mesh manifest describes source geometry, shader digests, texture inputs, PropKit counts and source prop placement counts. For example, `tidewater-turf` includes 7,966 vertices with non-axis-aligned edge bevel normals, 280 placed source props, 758,448 prop vertices and 1,978,782 prop indices. The GPU test renders source level and PropKit meshes, checks source PBR and captured maps, reads back wet and dry atlas values, checks visible ink shading changes, and checks for OpenGL errors. The strengthened fixture selects a floor patch with unobstructed sight lines across the splat and requires more than 100 visibly changed pixels. It passed with sourcePbr=pass capturedMaps=pass bevels=sourceGeometry inkWet=0.984314 inkDry=0.2 visibleInkPixels=603 on an NVIDIA GeForce GTX 1650, with no GL errors. The actual application also completed a 1280×720, 240-step Tidewater Turf capture through its normal HDR/shadow/post pipeline; the capture showed stage textures, props, characters and continuous wet ink together.

Run the exporter integrity checks and focused tests from the repository root:

```powershell
node tools/export-stage-visuals.mjs --check
node tools/export-stage-textures.mjs --check
node --test tests/stage_visual_export.test.mjs tests/stage_texture_data.test.mjs
ctest --test-dir build -R reference_stage_visual_gpu --output-on-failure
```

The stage mesh exporter check verifies deterministic generated assets. The texture exporter check recaptures and compares the browser source assets; it uses the reference browser setup. Texture regeneration uses `node tools/export-stage-textures.mjs` with the reference browser session configured to disable accelerated 2D Canvas, which avoids small Canvas raster variations while keeping the source WebGL texture arrays GPU-backed. The Node tests validate all 23 stage variants, bevel geometry, source shader features, source hashes, texture dimensions and content hashes, nonblank texture data, mural tables, and the PropKit fence alpha mask. `reference_stage_visual_gpu` writes `build/captures/stage-visual/stage-overview.png`, `stage-wet-ink.png`, and `stage-dry-ink.png`.

## Known limits

This is source-derived stage geometry, materials, textures, dressing and paint presentation, not a complete environment port. The source environment sky and sea, PMREM/reflection environment, and far scenery are not ported. Cloth remains at its exported canonical pose, and source prop placement animation is not evaluated continuously. The isolated direct-PBR GPU fixture uses explicit test lighting and verifies renderer/material behavior; it is not a whole-scene parity comparison. Final integrated application captures under the normal HDR, shadow and post-processing pipeline are a separate acceptance check.
