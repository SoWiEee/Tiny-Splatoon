import { register } from 'node:module';
import { readFile, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';

register(new URL('./reference-loader.mjs', import.meta.url));
const root = resolve(import.meta.dirname, '..');
const { OFFLINE_MAPS } = await import('../splat-ref/src/config.js');
const { MAP_LAYOUTS } = await import('../splat-ref/src/world/maps.js');
const { layoutFor, inMode } = await import('../splat-ref/src/world/variants.js');
const { dressingFor } = await import('../splat-ref/src/world/dressing.js');
const { PropKit } = await import('../splat-ref/src/world/props.js');
const { Level } = await import('../splat-ref/src/world/level.js');
const { BossNav } = await import('../splat-ref/src/boss/bossNav.js');

const n = (v) => Number(v.toFixed(6));
const rows = [];
for (const map of OFFLINE_MAPS) {
  const layout = MAP_LAYOUTS[map.layout || map.id];
  const kit = new PropKit(null, { quality: 'high', headless: true });
  const colliders = [];
  try {
    for (const item of dressingFor(map.layout || map.id)) {
      if (!inMode(item, 'turf')) continue;
      colliders.push(...kit.add(item.type, item).colliders);
    }
  } finally { kit.dispose(); }
  const level = new Level(layoutFor(layout, 'turf'), colliders);
  const nav = new BossNav(level);
  const samples = [0.1, 0.3, 0.5, 0.7, 0.9].map((f) => nav.planIds[Math.floor((nav.planIds.length - 1) * f)]);
  const probes = samples.map((cell) => {
    const [x, z] = nav.xz(cell);
    return { cell, x: n(x), z: n(z), wall: n(nav.wallClear(x, z)), floor: n(nav.floorClear(x, z)), y: n(nav.floorAt(x, z)),
      pose: nav.poseOk(x, z, 0), turn: nav.turnOk(x, z, 1.2), cast: n(nav.cast(x, z, 0, 8).dist) };
  });
  const first = probes[0], last = probes[probes.length - 1];
  const path = nav.path(first.x, first.z, last.x, last.z) || [];
  rows.push({ id: map.id, floorY: n(nav.floorY), plan: nav.planIds.length, probes, path: path.map(([x, z]) => [n(x), n(z)]) });
  console.log(`${map.id}: ${nav.planIds.length} plan cells`);
}
const output = `${JSON.stringify(rows, null, 2)}\n`;
const file = resolve(root, 'tests/boss_nav_fixtures.json');
if (process.argv.includes('--check')) {
  if (await readFile(file, 'utf8') !== output) throw new Error('Boss nav fixtures differ from locked source');
} else await writeFile(file, output);
