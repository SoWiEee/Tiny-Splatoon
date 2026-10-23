Weapon artwork is rasterized from `splat-ref/src/ui/ui-icons.js` and the icon
registrations in `splat-ref/src/game/kits/` using the reference browser's SVG
renderer. White replaces `currentColor`; transparent 128 × 128 PNGs retain the
source shapes, outlines and material colors. The native UI supplies team ink.

Re-export: `node tools/export-hud-icons.mjs`.
Read-only comparison: `node tools/export-hud-icons.mjs --check`.

The browser's HTML parser normalizes source SVG attributes before XML image
decoding (the source Brolly icon repeats `stroke-width`). Reference files are
never edited. PNGs are runtime assets; the browser exporter is not required to
build or run the game.
