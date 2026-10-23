# Native HDR and shadow contract

This document records the native rendering seam introduced on 2026-10-20. It does not certify all of RENDER-01–05 or FX-01–03.

## Ownership and frame order

`src/render/RenderPipeline` owns half-float scene and postprocessing targets. Call `Begin` with framebuffer dimensions, the actual projection, camera-world matrix and clipping planes, render the scene in linear HDR, then call `End` before drawing GUI. `End` resolves scene color and depth, applies optional GTAO and bloom, grade, screen FX, then Neutral tone mapping and the sRGB output transfer. It returns its fullscreen draw count for performance capture. A size or sample-count change rebuilds all dependent targets; zero dimensions must be skipped by the window owner.

`tools/export-post-shaders.mjs` exports the locked reference `renderer.js` grade shader, `screenfx.js` composite and Three.js postprocessing shaders. Only the desktop GLSL prefix and expanded shader includes change. The committed `assets/reference/post/manifest.json` records source and generated SHA-256 hashes. Native builds do not require the browser source tree; source replay tests do.

## Source parameters

- GTAO: radius 1.1, distance exponent 1.6, thickness 1, scale 1.5, 12 samples, distance falloff 1; the blend retains 20% of scene light even for full occlusion. Normals are reconstructed using the source shader's supported depth-normal branch. This branch is not the separate normal render used by the reference's default GTAOPass.
- Poisson denoising: luma/depth/normal phi 10/2/3, radius 6, two rings, 16 samples. The 5×5 magic-square GTAO noise is source-generated. The source simplex-noise denoiser texture is exported with a fixed fixture RNG seed rather than claiming the browser's unrelated random stream is identical.
- Bloom: five separable mips, source kernels 6/10/14/18/22, paired bilinear Gaussian taps, default strength/radius/threshold 0.28/0.45/2.4. Each mip dimension rounds the previous half-size as in the source. The source composite includes its factor of 3 and radius-interpolated mip factors.
- Grade: source vibrance, log contrast, split tone, exposure, vignette, hurt and flash expressions. `GradeSettings::Theme` carries day/sunset/golden values. Output exposure stays 1.
- Screen composite: the complete source HDR fragment shader, eight radial-blur taps, and every uniform are available. `ScreenEffectsController` ports damage batching, punch spring, blast attenuation/deduplication, swim/charge/special aura, low-health heartbeat, urgency, flood and reveal envelopes, pause and reduced-motion scaling. `LensInk` ports the 220-part pool, splat/drop recipes, stick-slip motion, trails and evaporation; `LensField` draws the source additive metaball shaders. Gameplay integration must feed the corresponding events and cloud/local-player state; shader/controller availability alone does not prove every gameplay caller emits them.

On the Windows acceptance target the default uses 4× MSAA. The mobile/Apple SMAA fallback is outside this Windows implementation.

## Directional shadows

`DirectionalShadow` ports `Environment::_fitShadow`: arena bounds expand by 6 m, vertical bounds span −1.9 to 14 m, light distance is 160 m, near is `max(0.5, -maxZ-30)`, far is `-minZ+2`. Bias is `-0.025/(far-near)`; normal bias is `max(0.012, largestWorldTexel*1.4)`; radius is `2.2*(size/4096)+0.8`.

Its depth texture uses bilinear hardware comparison. `SamplingGLSL` implements the reference renderer's noise-free 3×3 PCF grid with step `texelSize*max(radius*0.55,0.6)`. The shadow owner submits actual casters, and scene material receivers consume this texture; the class does not submit geometry or introduce gameplay effects.

`tools/export-character-shadow-shaders.mjs` exports 23 source character material variants with one directional shadow, the same stable PCF patch and linear HDR output. The original diagnostic shader assets remain byte-identical. The GPU check compiles every shadow variant and verifies that the original physical material receives a shadow, rather than only testing a standalone custom shadow sampler.

## Evidence

`reference_post_export` regenerates and compares every committed post asset byte for byte. `reference_render_pipeline` compiles all exported shaders and exercises actual OpenGL 4.5 HDR color/depth resolve, 4× MSAA, GTAO background preservation, Neutral/sRGB grey, exposure, HDR bloom threshold, screen flash, resize and invalid dimensions. It also checks shadow depth comparison in lit and occluded cases, source fit parameters and viewport restoration.

`reference_screen_effects` compares 24 actual source scenarios over 2,160 updates, including 144 complete lens-particle snapshots. Cases include nearby enemy impacts, friendly/roller exclusion, reset and GO transitions. The GPU test also exercises the actual lens-field texture through the HDR composite, capacity 220 and complete expiration after 20 s. Export replay locks `screenfx.js` and reproduces the fixture. These are component-level checks; the game must still wire its events and submit the render passes.

These targeted checks do not replace whole-scene reference image comparisons, all event-effect traces, or final 1080p 4v4/eight-person Boss performance measurements.

## Walking ink and surface ripples (2026-10-22)

The live rig foot-plant callback now drives the source dry/own-ink/enemy-ink recipes instead of a generic explosion burst. A polling fallback serves callers without rig callbacks. Cosmetic droplets enter `FacePaint` as `Speck` requests and never change the ownership grid or scoring version. Their collision ripples, and dive/emerge ripples, feed the production level shader's existing `uRip`/`uRipP` table. The table keeps the source 24-slot capacity, camera priority, short-range impact deduplication and expiry/reuse rules. Ordinary ink splats also excite this surface table.

Source glow kinds use their soft/star/bubble/halo equations in a separate additive pass, preserving the caller's separate RGB/alpha blend factors and equations. This prevents the glow billboard background from darkening the scene. The walking regression rejects explosion shells, circular collision fallback decals and scoring changes; the GPU glow check rejects dark rectangular backgrounds and was confirmed to fail when normal alpha blending was temporarily substituted. The production stage GPU check compares identical ink at an identical shader time with and without a ripple. These checks cover the reported walking artifacts, not pixel identity for every particle recipe.
