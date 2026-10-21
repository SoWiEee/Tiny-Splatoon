import { register } from 'node:module';
import { readFile, mkdir, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { resolve } from 'node:path';
register(new URL('./reference-loader.mjs', import.meta.url));
const THREE = await import('three');
const { MAPS } = await import('../splat-ref/src/config.js');
const { MAP_LAYOUTS } = await import('../splat-ref/src/world/maps.js');
const { layoutFor, inMode } = await import('../splat-ref/src/world/variants.js');
const { dressingFor } = await import('../splat-ref/src/world/dressing.js');
const { PropKit } = await import('../splat-ref/src/world/props.js');
const { Level } = await import('../splat-ref/src/world/level.js');
const { PaintSystem } = await import('../splat-ref/src/world/paint.js');
const { ZoneControl } = await import('../splat-ref/src/game/zones.js');
const { ZoneMarks } = await import('../splat-ref/src/fx/zoneMarks.js');
const { G } = await import('../splat-ref/src/core/ctx.js');
const { buildCharacterShader } = await import('./export-character-shaders.mjs');
const output = resolve(import.meta.dirname, '../assets/reference/zones');
const check = process.argv.includes('--check');
if (process.argv.slice(2).some(v => v !== '--check')) throw new Error('Usage: node tools/export-zone-visuals.mjs [--check]');
await mkdir(output, { recursive: true });
async function artifact(name, data) {
  const file = resolve(output, name), bytes = Buffer.isBuffer(data) ? data : Buffer.from(data);
  if (check) {
    const old = await readFile(file);
    if (Buffer.isBuffer(data) ? !old.equals(bytes) : old.toString('utf8').replace(/\r\n/g, '\n') !== data)
      throw new Error(`Stale source zone visual: ${name}`);
  } else await writeFile(file, bytes);
}
// Expose shader constants in memory; the locked reference files remain untouched.
const paintUrl = new URL('../splat-ref/src/world/paint.js', import.meta.url);
const paintSource = (await readFile(paintUrl, 'utf8')).replace(/((?:from\s*|import\s*)['"])(\.{1,2}\/[^'"]+)(['"])/g, (_, a, b, c) => a + new URL(b, paintUrl).href + c);
const floodSource = await import('data:text/javascript;base64,' + Buffer.from(paintSource + '\nexport { FLOOD_VS, FLOOD_FS };').toString('base64'));
const floodShader = buildCharacterShader(new THREE.ShaderMaterial({ vertexShader: floodSource.FLOOD_VS, fragmentShader: floodSource.FLOOD_FS }), { toneMapping: false, linearOutput: true });
await artifact('flood.vert', floodShader.vertex); await artifact('flood.frag', floodShader.fragment);
const stages = [];
for (const map of MAPS) {
  const id = map.id, layout = MAP_LAYOUTS[map.layout || id];
  if (!layout) continue;
  const kit = new PropKit(null, { quality: 'high', headless: true }), colliders = [];
  try {
    for (const item of dressingFor(map.layout || id)) if (inMode(item, 'zones')) colliders.push(...kit.add(item.type, item).colliders);
  } finally { kit.dispose(); }
  G.level = new Level(layoutFor(layout, 'zones'), colliders);
  const paint = Object.create(PaintSystem.prototype);
  Object.assign(paint, { level: G.level, size: 4096, cell: 0.25, pad: 8 });
  paint._layout(30); paint._initGrid(); G.paint = paint; G.time = 0;
  const match = { time: 300, state: 'intro', local: { team: 0 } };
  match.zones = new ZoneControl(match);
  const marks = new ZoneMarks(new THREE.Scene());
  marks.build(match);
  const mesh = marks.mesh, geometry = mesh.geometry, n = geometry.attributes.position.count, indices = geometry.index.array;
  const bytes = Buffer.alloc(16 + n * 28 + indices.length * 4);
  bytes.write('TSZMARK1'); bytes.writeUInt32LE(n, 8); bytes.writeUInt32LE(indices.length, 12);
  let at = 16;
  for (let i = 0; i < n; ++i) for (const [name, size] of [['position', 3], ['aInfo', 4]])
    for (let c = 0; c < size; ++c, at += 4) bytes.writeFloatLE(geometry.attributes[name].getComponent(i, c), at);
  for (const index of indices) { bytes.writeUInt32LE(index, at); at += 4; }
  await artifact(`${id}.bin`, bytes);
  if (!stages.length) {
    const shader = buildCharacterShader(mesh.material, { toneMapping: false, linearOutput: true });
    await artifact('marks.vert', shader.vertex); await artifact('marks.frag', shader.fragment);
  }
  stages.push({ id, vertices: n, indices: indices.length, zones: match.zones.zones.map(z => ({ cells: z.cells.length, center: z.center, reach: z.reach })) });
  marks.dispose();
}
const sources = [];
for (const path of ['src/fx/zoneMarks.js', 'src/game/zones.js', 'src/world/zones-data.js', 'src/world/paint.js'])
  sources.push({ path, sha256: createHash('sha256').update(await readFile(new URL('../splat-ref/' + path, import.meta.url))).digest('hex') });
await artifact('manifest.json', JSON.stringify({ sources, stages }, null, 2) + '\n');
console.log(`Source ZoneMarks: ${stages.length} stages`);
