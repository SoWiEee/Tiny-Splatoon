import { readFileSync, writeFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import { dirname, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const ref = resolve(root, 'splat-ref');
const source = readFileSync(resolve(ref, 'src/game/showcase.js'), 'utf8');
const url = (path) => pathToFileURL(resolve(ref, path)).href;
const moduleSource = source
  .replace("from 'three'", `from '${url('node_modules/three/build/three.module.js')}'`)
  .replace("from 'three/addons/utils/BufferGeometryUtils.js'", `from '${url('node_modules/three/examples/jsm/utils/BufferGeometryUtils.js')}'`)
  .replace("from '../core/ctx.js'", `from '${url('src/core/ctx.js')}'`)
  .replace("from '../config.js'", `from '${url('src/config.js')}'`)
  + `\nexport function sourceDrums() { return SLOTS.flatMap((slot, i) => {
    const body = drumGeometry(slot.R, DRUM, 112);
    const drips = makeDrips(31 + i * 7, 8, { front: 1.35, w0: 0.034, w1: 0.064, L0: 0.05, L1: 0.26, longChance: 0.35 });
    const coat = inkCoatGeometry(slot.R, DRUM.bevel, drips, { T0: DRUM.ink, du: 0.024, duF: 0.0042, NB: 22, seed: 9 + i, depth: 0.36 });
    return [body, coat];
  }); }\n`;
const { sourceDrums } = await import(`data:text/javascript;base64,${Buffer.from(moduleSource).toString('base64')}`);
const chunks = [Buffer.from('IWDRUM1\0'), createHash('sha256').update(source).digest()];
const header = Buffer.alloc(4); header.writeUInt32LE(8); chunks.push(header);
for (const geometry of sourceDrums()) {
  const position = geometry.getAttribute('position'), normal = geometry.getAttribute('normal');
  const color = geometry.getAttribute('color'), glow = geometry.getAttribute('aGlow');
  const index = geometry.getIndex();
  const counts = Buffer.alloc(8); counts.writeUInt32LE(position.count); counts.writeUInt32LE(index.count, 4); chunks.push(counts);
  const vertices = Buffer.alloc(position.count * 32);
  for (let i = 0; i < position.count; i++) {
    const values = [position.getX(i), position.getY(i), position.getZ(i), normal.getX(i), normal.getY(i), normal.getZ(i), color ? color.getX(i) : 1, glow ? glow.getX(i) : 0];
    for (let j = 0; j < values.length; j++) vertices.writeFloatLE(values[j], i * 32 + j * 4);
  }
  const indices = Buffer.alloc(index.count * 4);
  for (let i = 0; i < index.count; i++) indices.writeUInt32LE(index.getX(i), i * 4);
  chunks.push(vertices, indices);
  geometry.dispose();
}
const target = resolve(root, 'assets/reference/characters/showcase-drums.bin');
const exported = Buffer.concat(chunks);
if (process.argv.includes('--check')) {
  const existing = readFileSync(target);
  if (!existing.subarray(0, 8).equals(chunks[0])) throw new Error(`${target}: invalid drum geometry header`);
  if (!existing.subarray(8, 40).equals(chunks[1])) throw new Error(`${target}: source hash differs from showcase.js`);
  if (!existing.equals(exported)) throw new Error(`${target}: drum geometry differs from showcase.js export`);
  console.log(`${target}: source geometry verified`);
} else {
  writeFileSync(target, exported);
  console.log(`${target}: source geometry exported`);
}
