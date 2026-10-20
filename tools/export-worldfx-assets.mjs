import { createHash } from 'node:crypto';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { register } from 'node:module';
import { resolve } from 'node:path';

const root = resolve(import.meta.dirname, '..');
const directory = resolve(root, 'assets/reference/characters');
const check = process.argv.includes('--check');
if (process.argv.slice(2).some(arg => arg !== '--check')) throw new Error('Usage: node tools/export-worldfx-assets.mjs [--check]');
register(new URL('./reference-loader.mjs', import.meta.url));
const THREE = await import('three');
const { getSpecialProp } = await import('../splat-ref/src/game/special-props.js');
const { getWeaponDef } = await import('../splat-ref/src/game/character-weapons.js');

class Writer {
  chunks = [];
  raw(value) { this.chunks.push(Buffer.from(value)); }
  u32(value) { const b = Buffer.alloc(4); b.writeUInt32LE(value); this.raw(b); }
  string(value) { const b = Buffer.from(value, 'utf8'); this.u32(b.length); this.raw(b); }
  bytes() { return Buffer.concat(this.chunks); }
}

const assets = [];
const pending = [];
function mesh(geometry) {
  if (!geometry?.isBufferGeometry) throw new Error('World FX source part has no BufferGeometry');
  const writer = new Writer(), count = geometry.attributes.position.count;
  writer.raw('TSMESH01'); writer.u32(count);
  const indices = geometry.index ? Array.from(geometry.index.array) : Array.from({ length: count }, (_, i) => i);
  writer.u32(indices.length);
  const attributes = Object.entries(geometry.attributes).sort(([a], [b]) => a.localeCompare(b));
  writer.u32(attributes.length);
  for (const [name, attribute] of attributes) {
    if (attribute.count !== count || attribute.itemSize < 1 || attribute.itemSize > 4) throw new Error(`Invalid world FX source attribute ${name}`);
    writer.string(name); writer.u32(attribute.itemSize); writer.string(attribute.array?.constructor.name || 'Interleaved'); writer.u32(attribute.normalized ? 1 : 0);
    const values = Buffer.alloc(count * attribute.itemSize * 4);
    for (let i = 0; i < count; i++) for (let j = 0; j < attribute.itemSize; j++) {
      const value = attribute.getComponent(i, j);
      if (!Number.isFinite(value)) throw new Error(`Non-finite world FX source attribute ${name}`);
      values.writeFloatLE(value, (i * attribute.itemSize + j) * 4);
    }
    writer.raw(values);
  }
  const indexBytes = Buffer.alloc(indices.length * 4);
  for (let i = 0; i < indices.length; i++) {
    if (indices[i] < 0 || indices[i] >= count) throw new Error('World FX source index out of range');
    indexBytes.writeUInt32LE(indices[i], i * 4);
  }
  writer.raw(indexBytes);
  const bytes = writer.bytes(), hash = createHash('sha256').update(bytes).digest('hex');
  const file = `${hash}.mesh`;
  if (!assets.some(asset => asset.file === file)) {
    assets.push({ file, hash, vertices: count, indices: indices.length, attributes: attributes.map(([name, a]) => ({ name, size: a.itemSize })) });
    pending.push({ file, bytes });
  }
  return file;
}

const catalog = JSON.parse(await readFile(resolve(directory, 'catalog.json'), 'utf8'));
const partRows = [];
for (const kind of ['speaker', 'missile']) {
  const source = getSpecialProp(kind), catalogProp = catalog.specials[kind];
  if (!source || !catalogProp) throw new Error(`Missing source ${kind} prop`);
  for (const role of kind === 'speaker' ? ['body', 'ink', 'glow', 'cone', 'coneInk'] : ['body', 'ink', 'glow']) {
    const sourcePart = source[role], catalogPart = catalogProp[role];
    if (!sourcePart || !catalogPart?.mesh) throw new Error(`Missing ${kind}.${role} source mesh`);
    const generated = mesh(sourcePart);
    if (generated !== catalogPart.mesh) throw new Error(`Source ${kind}.${role} mesh differs from reference catalog`);
    const pivot = kind === 'speaker' && role.startsWith('cone') ? source.coneAt.toArray() : [0, 0, 0];
    partRows.push({ kind, role, file: generated, pivot });
  }
}
const stamp = getWeaponDef('sp_stamp');
for (const role of ['body', 'ink']) {
  const file = mesh(stamp[role]);
  partRows.push({ kind: 'stamp', role, file, pivot: [0, 0, 0] });
}
const primitiveRows = [
  ['funnel', new THREE.CylinderGeometry(1, 1, 1, 28, 12, true).translate(0, 0.5, 0)],
  ['speakerBeam', new THREE.CylinderGeometry(1, 1, 1, 24, 1, true).translate(0, 0.5, 0).rotateX(Math.PI / 2)],
  ['sphere4', new THREE.IcosahedronGeometry(1, 4)],
  // Ink Tempest cloud puffs come from weapons.js' cloudGeo.
  ['stormCloud', new THREE.IcosahedronGeometry(1, 3)],
  ['warningRing', new THREE.RingGeometry(0.86, 1, 48).rotateX(-Math.PI / 2)],
];
for (const [role, geometry] of primitiveRows) partRows.push({ kind: 'geometry', role, file: mesh(geometry), pivot: [0, 0, 0] });

await mkdir(directory, { recursive: true });
for (const { file, bytes } of pending) {
  const target = resolve(directory, file);
  if (check) {
    if (!(await readFile(target)).equals(bytes)) throw new Error(`Stale world FX source mesh: ${file}`);
  } else await writeFile(target, bytes);
}
if (check) {
  for (const { file } of assets) await readFile(resolve(directory, file));
}

const quote = value => JSON.stringify(value);
const sourceHash = async file => createHash('sha256').update(await readFile(resolve(root, file))).digest('hex');
const out = `// Generated by tools/export-worldfx-assets.mjs. Do not edit.\n#pragma once\n#include <array>\n#include <glm/glm.hpp>\nnamespace restoration {\nstruct WorldFxMeshAsset { const char* kind; const char* role; const char* file; glm::dvec3 pivot; };\n// Source SHA-256: special-props.js ${await sourceHash('splat-ref/src/game/special-props.js')}\n// Source SHA-256: character-weapons.js ${await sourceHash('splat-ref/src/game/character-weapons.js')}\n// Source SHA-256: weapons.js ${await sourceHash('splat-ref/src/game/weapons.js')}\ninline constexpr std::array<WorldFxMeshAsset, ${partRows.length}> WorldFxMeshAssets={{\n${partRows.map(row => `    {${quote(row.kind)},${quote(row.role)},${quote(row.file)},{${row.pivot.map(n => Number(n).toPrecision(17)).join(',')}}},`).join('\n')}\n}};\n}\n`;
const headerPath = resolve(root, 'src/game/WorldFxAssetData.hpp');
if (check) {
  if (await readFile(headerPath, 'utf8') !== out) throw new Error('Stale World FX asset data');
} else await writeFile(headerPath, out);
console.log(`${check ? 'Verified' : 'Exported'} ${partRows.length} source World FX mesh parts`);
