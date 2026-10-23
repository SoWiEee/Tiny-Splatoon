import { register } from 'node:module';
import { readFile, readdir, mkdir, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { resolve, dirname, relative } from 'node:path';

const root = resolve(import.meta.dirname, '..');
register(new URL('./reference-loader.mjs', import.meta.url));

const { MAPS } = await import('../splat-ref/src/config.js');
const config = await import('../splat-ref/src/config.js');
const { MAP_LAYOUTS } = await import('../splat-ref/src/world/maps.js');
const { DRESSING, dressingFor } = await import('../splat-ref/src/world/dressing.js');
const { STAGES } = await import('../splat-ref/src/world/stages/index.js');
const { layoutFor, inMode } = await import('../splat-ref/src/world/variants.js');
const { Level } = await import('../splat-ref/src/world/level.js');
const { PropKit } = await import('../splat-ref/src/world/props.js');
const { Physics } = await import('../splat-ref/src/game/physics.js');
const { PaintSystem } = await import('../splat-ref/src/world/paint.js');
const THREE = await import('three');

async function verifyBaseline() {
  const manifest = await readFile(resolve(root, 'docs/reference-baseline.sha256'), 'utf8');
  const listed = new Set();
  for (const line of manifest.split(/\r?\n/)) {
    if (!line || line.startsWith('#')) continue;
    const match = /^([a-f0-9]{64})  (splat-ref\/.*)$/.exec(line);
    if (!match) throw new Error(`Invalid baseline entry: ${line}`);
    listed.add(match[2]);
    const bytes = await readFile(resolve(root, match[2]));
    if (createHash('sha256').update(bytes).digest('hex') !== match[1]) throw new Error(`Reference changed: ${match[2]}`);
  }
  const excluded = new Set(['node_modules', 'vendor', 'build', 'dist', '.git', '.cache', 'shots']);
  async function checkFiles(directory) {
    for (const entry of await readdir(directory, { withFileTypes: true })) {
      if (entry.isDirectory() && !excluded.has(entry.name)) await checkFiles(resolve(directory, entry.name));
      else if (entry.isFile()) {
        const path = relative(root, resolve(directory, entry.name)).replaceAll('\\', '/');
        if (!listed.has(path)) throw new Error(`Reference file missing from baseline: ${path}`);
      }
    }
  }
  await checkFiles(resolve(root, 'splat-ref'));
}
await verifyBaseline();

const vec = (v) => [v.x, v.y, v.z];
const serializableConfig = Object.fromEntries(Object.entries(config).filter(([key]) => key !== 'default'));
const layouts = MAP_LAYOUTS;
const primitiveKinds = new Set(['box', 'obox', 'ramp']);
for (const [id, layout] of Object.entries(layouts)) {
  for (const part of ['single', 'half']) {
    for (const [index, piece] of layout[part].entries()) {
      if (!primitiveKinds.has(piece.kind)) throw new Error(`Unknown primitive kind in ${id}.${part}[${index}]: ${piece.kind}`);
    }
  }
}
const stages = [];
const ground = [];
const rays = [];
const paintLevels = new Map();
function recordRay(stage, physics, origin, dir, maxDist, skipGrates = false) {
  const hit = physics.raycast(origin, dir, maxDist, undefined, skipGrates);
  rays.push({ stage, origin: vec(origin), dir: vec(dir), maxDist, skipGrates,
    hit: hit.hit, dist: hit.dist, block: hit.block, face: hit.face,
    point: hit.hit ? vec(hit.point) : [0, 0, 0], normal: hit.hit ? vec(hit.normal) : [0, 0, 0],
    u: hit.hit && hit.face >= 0 ? hit.u : 0, v: hit.hit && hit.face >= 0 ? hit.v : 0 });
  return hit;
}
for (const map of MAPS) {
  const id = map.layout || map.id;
  const layout = layouts[id];
  if (!layout) throw new Error(`Missing layout: ${id}`);
  for (const mode of ['turf', 'zones', 'boss']) {
    if (mode === 'boss' && map.noBoss) continue;
    const kit = new PropKit(null, { quality: 'high', headless: true });
    const colliders = [];
    try {
      for (const item of dressingFor(id)) {
        if (!inMode(item, mode)) continue;
        const count = kit.count;
        const result = kit.add(item.type, item);
        if (kit.count !== count + 1 || !Array.isArray(result?.colliders)) throw new Error(`Prop collider export failed: ${id}/${mode}/${item.type}`);
        colliders.push(...result.colliders);
      }
    } finally {
      kit.dispose();
    }
    const level = new Level(layoutFor(layout, mode), colliders);
    const layoutBlockCount = level.blocks.length - colliders.length;
    if (mode === 'turf' && (map.id === 'tidewater' || map.id === 'halyard')) paintLevels.set(map.id, level);
    const physics = new Physics(level);
    const stage = {
      id: map.id, name: map.name, mode, theme: map.theme, water: layout.water || '',
      bounds: layout.bounds, spawnPads: layout.spawnPads, spawnBarrier: layout.spawnBarrier,
      onlineOnly: !!map.onlineOnly, noBots: !!map.noBots, noBoss: !!map.noBoss,
      layoutBlockCount, propColliderCount: colliders.length, propColliders: colliders,
      blocks: level.blocks.map((b) => ({
        center: vec(b.center), half: vec(b.half), axes: b.axes.map(vec), faces: b.faces,
        solid: b.solid, grate: b.grate, roof: b.roof, perch: b.perch, noNav: b.noNav,
        rail: b.rail, hidden: b.hidden,
      })),
      faces: level.faces.map((f) => ({
        origin: vec(f.origin), u: vec(f.u), v: vec(f.v), normal: vec(f.n), color: [f.color.r, f.color.g, f.color.b],
        width: f.su, height: f.sv, block: f.block, pattern: f.pattern, mural: f.mural,
        paintable: f.paintable, turf: f.turf, wall: f.wall, ceiling: f.ceiling, groundedBottom: f.groundedBottom,
      })),
    };
    const stageIndex = stages.length;
    stages.push(stage);
    const propBlocks = level.blocks.slice(layoutBlockCount);
    let heightProbe = false;
    let sideProbe = false;
    for (const b of propBlocks) {
      if (!heightProbe && !b.grate) {
        const top = b.center.y + b.half.y;
        const x = b.center.x, z = b.center.z;
        const yMax = top + 0.25;
        const hit = physics.raycast(new THREE.Vector3(x, yMax, z), new THREE.Vector3(0, -1, 0), 0.5);
        const y = level.groundHeight(x, z, yMax);
        if (hit.hit && hit.block === b.id && Math.abs(y - top) < 1e-6) {
          ground.push({ stage: stageIndex, x, z, yMax, skipGrates: false, hit: true, y });
          recordRay(stageIndex, physics, new THREE.Vector3(x, yMax, z), new THREE.Vector3(0, -1, 0), 0.5);
          heightProbe = true;
        }
      }
      if (!sideProbe) for (const axis of [b.axes[0], b.axes[2]]) {
        const half = axis === b.axes[0] ? b.half.x : b.half.z;
        for (const sign of [-1, 1]) {
          const origin = b.center.clone().addScaledVector(axis, sign * (half + 0.25));
          const dir = axis.clone().multiplyScalar(-sign);
          const hit = physics.raycast(origin, dir, 0.5);
          if (hit.hit && hit.block === b.id && hit.face === -1) {
            recordRay(stageIndex, physics, origin, dir, 0.5);
            sideProbe = true;
            break;
          }
        }
        if (sideProbe) break;
      }
      if (heightProbe && sideProbe) break;
    }
    if (!heightProbe || !sideProbe) throw new Error(`Missing actual prop collider probes: ${map.id}/${mode}`);
    const { minX, maxX, minZ, maxZ } = layout.bounds;
    for (const [a, b] of [
      [[minX - 2, 2, minZ - 2], [maxX + 2, 2, maxZ + 2]],
      [[maxX + 2, 4, minZ - 2], [minX - 2, 4, maxZ + 2]],
    ]) {
      const origin = new THREE.Vector3(...a);
      const delta = new THREE.Vector3(...b).sub(origin);
      recordRay(stageIndex, physics, origin, delta.clone().normalize(), delta.length());
    }
    const points = [layout.spawnPads[0], layout.spawnPads[1]];
    for (const fx of [0.17, 0.39, 0.61, 0.83]) for (const fz of [0.17, 0.39, 0.61, 0.83])
      points.push([minX + (maxX - minX) * fx, 0, minZ + (maxZ - minZ) * fz]);
    for (const [x, , z] of points) {
      for (const skipGrates of [false, true]) {
        const y = level.groundHeight(x, z, 50, skipGrates);
        ground.push({ stage: stageIndex, x, z, yMax: 50, skipGrates, hit: Number.isFinite(y), y: Number.isFinite(y) ? y : 0 });
        recordRay(stageIndex, physics, new THREE.Vector3(x, 50, z), new THREE.Vector3(0, -1, 0), 100, skipGrates);
      }
    }
  }
}

const number = (n) => {
  if (!Number.isFinite(n)) throw new Error(`Non-finite generated number: ${n}`);
  return `${Object.is(n, -0) ? 0 : n}${Number.isInteger(n) ? '.0' : ''}`;
};
const cppVec = (v) => `{${v.map(number).join(', ')}}`;
const bool = (v) => v ? 'true' : 'false';
const blockCpp = (b) => `{${cppVec(b.center)}, ${cppVec(b.half)}, {{${b.axes.map(cppVec).join(', ')}}}, {${b.faces.join(', ')}}, ${[b.solid,b.grate,b.roof,b.perch,b.noNav,b.rail,b.hidden].map(bool).join(', ')}}`;
const faceCpp = (f) => `{${[f.origin,f.u,f.v,f.normal,f.color].map(cppVec).join(', ')}, ${number(f.width)}, ${number(f.height)}, ${f.block}, ${f.pattern}, ${f.mural}, ${[f.paintable,f.turf,f.wall,f.ceiling,f.groundedBottom].map(bool).join(', ')}}`;
const cpp = ['// Generated by tools/export-reference.mjs from the locked splat-ref baseline.', '#include "ReferenceData.hpp"', '#include <iterator>', '', 'namespace restoration {', 'namespace {'];
for (const [index, s] of stages.entries()) {
  cpp.push(`static const BlockData stage${index}Blocks[] = {`, `  ${s.blocks.map(blockCpp).join(',\n  ')}`, '};');
  cpp.push(`static const FaceData stage${index}Faces[] = {`, `  ${s.faces.map(faceCpp).join(',\n  ')}`, '};');
}
cpp.push('} // namespace', '', 'const std::vector<StageData>& ReferenceStages() {', '  static const std::vector<StageData> stages = {');
for (const [index, s] of stages.entries()) {
  cpp.push(`    {${[s.id,s.name,s.mode,s.theme,s.water].map(JSON.stringify).join(', ')}, {${[s.bounds.minX,s.bounds.maxX,s.bounds.minZ,s.bounds.maxZ].map(number).join(', ')}}, {{${s.spawnPads.map(cppVec).join(', ')} }}, ${number(s.spawnBarrier)}, ${[s.onlineOnly,s.noBots,s.noBoss].map(bool).join(', ')}, ${s.layoutBlockCount}, ${s.propColliderCount},`);
  cpp.push(`      std::vector<BlockData>(std::begin(stage${index}Blocks), std::end(stage${index}Blocks)),`);
  cpp.push(`      std::vector<FaceData>(std::begin(stage${index}Faces), std::end(stage${index}Faces))},`);
}
cpp.push('  };', '  return stages;', '}', '} // namespace restoration', '');

const groundCpp = (f) => `{${f.stage}, ${number(f.x)}, ${number(f.z)}, ${number(f.yMax)}, ${bool(f.skipGrates)}, ${bool(f.hit)}, ${number(f.y)}}`;
const rayCpp = (f) => `{${f.stage}, ${cppVec(f.origin)}, ${cppVec(f.dir)}, ${number(f.maxDist)}, ${bool(f.skipGrates)}, ${bool(f.hit)}, ${number(f.dist)}, ${f.block}, ${f.face}, ${cppVec(f.point)}, ${cppVec(f.normal)}, ${number(f.u)}, ${number(f.v)}}`;
const fixture = [
  '// Generated from actual splat-ref Level and Physics queries by tools/export-reference.mjs.',
  '#pragma once', '#include <array>', '#include <glm/vec3.hpp>', 'namespace restoration::fixtures {',
  'struct GroundFixture { int stage; double x,z,yMax; bool skipGrates,hit; double y; };',
  'struct RayFixture { int stage; glm::dvec3 origin,dir; double maxDist; bool skipGrates,hit; double dist; int block,face; glm::dvec3 point,normal; double u,v; };',
  `inline const std::array<GroundFixture, ${ground.length}> ground = {{`,
  ...ground.map((f) => `  ${groundCpp(f)},`), '}};',
  `inline const std::array<RayFixture, ${rays.length}> rays = {{`,
  ...rays.map((f) => `  ${rayCpp(f)},`), '}};',
  '} // namespace restoration::fixtures', '',
].join('\n');

const paintFixtures = [];
for (const [id, atlasSize, maxDensity] of [['tidewater', 2048, 30], ['halyard', 4096, 30]]) {
  const level = paintLevels.get(id);
  const paint = Object.create(PaintSystem.prototype);
  paint.level = level;
  paint.size = atlasSize;
  paint.cell = 0.25;
  paint.pad = 8;
  paint._layout(maxDensity);
  paint._initGrid();
  paint.growing = [];
  paint.version = 0;
  paint._emitGrowth = () => {};
  paint._rippledNear = () => true;
  const f = paint.paintFaces.find((face) => face.turf && face.nu > 8 && face.nv > 8 && !paint.dead[face.grid + Math.floor(face.nv / 2) * face.nu + Math.floor(face.nu / 2)]);
  if (!f) throw new Error(`No live turf splat target: ${id}`);
  const u = f.su / 2, v = f.sv / 2;
  const center = f.origin.clone().addScaledVector(f.u, u).addScaledVector(f.v, v).addScaledVector(f.n, 0.02);
  const splats = [
    { center: vec(center), radius: 0.8, team: 0, seed: 0.25, stretch: null, stretchAmt: 0, kind: 'shot', cosmetic: false },
    { center: vec(center), radius: 0.65, team: 1, seed: 0.65, stretch: null, stretchAmt: 0, kind: 'shot', cosmetic: false },
    { center: vec(center.clone().addScaledVector(f.u, 0.8)), radius: 0.9, team: 0, seed: 0.4, stretch: vec(f.u), stretchAmt: 0, kind: 'roll', cosmetic: false },
    { center: vec(center.clone().addScaledVector(f.v, 0.8)), radius: 0.55, team: 1, seed: 0.8, stretch: vec(f.v), stretchAmt: 1.3, kind: 'line', cosmetic: false },
  ];
  for (const s of splats) {
    const before = paint.grid.slice();
    const claimed = paint.splat(new THREE.Vector3(...s.center), s.radius, s.team,
      { seed: s.seed, stretch: s.stretch ? new THREE.Vector3(...s.stretch) : undefined, stretchAmt: s.stretchAmt, kind: s.kind, cosmetic: s.cosmetic });
    s.claimed = claimed; s.counts = [...paint.counts]; s.version = paint.version;
    s.changed = [...paint.grid.entries()].filter(([index, value]) => before[index] !== value).map(([index, value]) => [index, value]);
    s.gridHash = createHash('sha256').update(paint.grid).digest('hex');
  }
  paintFixtures.push({ id, atlasSize, maxDensity, ppm: paint.ppm, usedHeight: paint.usedHeight,
    turfTotal: paint.turfTotal, turfArea: paint.turfArea, gridSize: paint.grid.length,
    faces: level.faces.map((face) => ({ atlas: face.atlas || null, grid: face.grid, nu: face.nu, nv: face.nv, cu: face.cu || 0, cv: face.cv || 0 })),
    dead: [...paint.dead.entries()].filter(([, value]) => value).map(([index]) => index), splats });
}
const paintFaceCpp = (f) => `{${f.atlas ? [f.atlas.x,f.atlas.y,f.atlas.w,f.atlas.h].join(', ') : '-1, -1, 0, 0'}, ${f.grid}, ${f.nu}, ${f.nv}, ${number(f.cu)}, ${number(f.cv)}}`;
const paintStepCpp = (s) => `{${cppVec(s.center)}, ${number(s.radius)}, ${s.team}, ${number(s.seed)}, ${cppVec(s.stretch || [0,0,0])}, ${bool(!!s.stretch)}, ${number(s.stretchAmt)}, ${['shot','line','blast','bomb','trail','drop','roll','speck'].indexOf(s.kind)}, ${bool(s.cosmetic)}, ${number(s.claimed)}, {${s.counts.join(', ')}}, ${s.version}, "${s.gridHash}", {${s.changed.map(([index,value]) => `{${index},${value}}`).join(', ')}}}`;
const paintHeader = ['// Generated from actual splat-ref PaintSystem CPU layout and splat methods.', '#pragma once', '#include <vector>', '#include <utility>', '#include <glm/vec3.hpp>', 'namespace restoration::test {',
  'struct PaintFaceFixture { int x,y,w,h,grid,nu,nv; double cu,cv; };',
  'struct PaintStepFixture { glm::dvec3 center; double radius; int team; double seed; glm::dvec3 stretch; bool hasStretch; double stretchAmt; int kind; bool cosmetic; double claimed; int counts[2]; int version; const char* gridSha256; std::vector<std::pair<int,int>> changed; };',
  'struct PaintFixture { const char* stageId; int atlasSize; double maxDensity,ppm; int usedHeight,turfTotal; double turfArea; int gridSize; std::vector<PaintFaceFixture> faces; std::vector<int> dead; std::vector<PaintStepFixture> steps; };',
  'inline const std::vector<PaintFixture>& paintFixtures() {', '  static const std::vector<PaintFixture> data = {'];
for (const p of paintFixtures) paintHeader.push(`    {"${p.id}", ${p.atlasSize}, ${number(p.maxDensity)}, ${number(p.ppm)}, ${p.usedHeight}, ${p.turfTotal}, ${number(p.turfArea)}, ${p.gridSize},`,
  `     {${p.faces.map(paintFaceCpp).join(',\n      ')}},`,
  `     {${p.dead.join(', ')}},`,
  `     {${p.splats.map(paintStepCpp).join(',\n      ')}}},`);
paintHeader.push('  };', '  return data;', '}', '} // namespace restoration::test', '');
const raw = { schema: 2, source: 'splat-ref', coverage: 'layout and PropKit collider geometry; prop render meshes are not included',
  config: serializableConfig, layouts, dressing: DRESSING, stagePlacements: Object.fromEntries(Object.entries(STAGES).map(([id, stage]) => [id, stage.PLACEMENTS || []])),
  stages };
const artifacts = new Map([
  ['assets/reference/content.json', JSON.stringify(raw, (_key, value) => typeof value === 'function' ? { $function: String(value) } : value, 2) + '\n'],
  ['src/world/ReferenceData.cpp', cpp.join('\n')],
  ['tests/fixtures/reference_fixtures.hpp', fixture],
  ['tests/fixtures/reference_paint_fixtures.hpp', paintHeader.join('\n')],
]);
for (const [path, content] of artifacts) {
  const destination = resolve(root, path);
  let action = 'checked';
  if (process.argv.includes('--check')) {
    const existing = await readFile(destination, 'utf8');
    if (existing !== content) throw new Error(`Generated artifact differs: ${path}`);
  } else {
    await mkdir(dirname(destination), { recursive: true });
    action = 'unchanged';
    if (await readFile(destination, 'utf8').catch((error) => { if (error.code === 'ENOENT') return null; throw error; }) !== content) {
      await writeFile(destination, content);
      action = 'wrote';
    }
  }
  console.log(`${action} ${relative(root, destination)} (${Buffer.byteLength(content)} bytes)`);
}
console.log(`${stages.length} legal stage/mode combinations, ${ground.length} ground and ${rays.length} ray fixtures`);
