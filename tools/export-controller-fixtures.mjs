import { register } from 'node:module';
import { readFile, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';

register(new URL('./reference-loader.mjs', import.meta.url));
const { MAPS } = await import('../splat-ref/src/config.js');
const { MAP_LAYOUTS } = await import('../splat-ref/src/world/maps.js');
const { dressingFor } = await import('../splat-ref/src/world/dressing.js');
const { layoutFor, inMode } = await import('../splat-ref/src/world/variants.js');
const { Level } = await import('../splat-ref/src/world/level.js');
const { PropKit } = await import('../splat-ref/src/world/props.js');
const { Physics, makeContacts, GroundHit } = await import('../splat-ref/src/game/physics.js');
const THREE = await import('three');

const V = (x, y, z) => new THREE.Vector3(x, y, z);
const vec = v => [v.x, v.y, v.z];
const num = x => { if (!Number.isFinite(x)) throw Error(`Non-finite fixture: ${x}`); return String(Object.is(x, -0) ? 0 : x) + (Number.isInteger(x) ? '.0' : ''); };
const cppVec = v => `{${v.map(num).join(', ')}}`;
const bool = x => x ? 'true' : 'false';
const levels = [];
for (const map of MAPS) {
  const id = map.layout || map.id;
  for (const mode of ['turf', 'zones', 'boss']) {
    if (mode === 'boss' && map.noBoss) continue;
    const kit = new PropKit(null, { quality: 'high', headless: true });
    const colliders = [];
    try {
      for (const item of dressingFor(id)) if (inMode(item, mode)) colliders.push(...kit.add(item.type, item).colliders);
    } finally { kit.dispose(); }
    levels.push(new Level(layoutFor(MAP_LAYOUTS[id], mode), colliders));
  }
}

const synthetic = new Level({
  bounds: { minX: -12, maxX: 12, minZ: -12, maxZ: 12 },
  spawnPads: [[0, 0, 0], [0, 0, 0]], spawnBarrier: 0,
  single: [
    { kind: 'box', min: [-10, -1, -10], max: [10, 0, 10], hidden: true },
    { kind: 'box', min: [1, 0, -2], max: [3, 0.3, 2], hidden: true },
    { kind: 'box', min: [4, 0, -2], max: [4.4, 3, 2], hidden: true },
    { kind: 'box', min: [-3, 0, -2], max: [-1, 0.2, 2], hidden: true, grate: true },
    { kind: 'box', min: [-7, 2, -2], max: [-5, 2.4, 2], hidden: true, roof: true },
    { kind: 'ramp', low: [-2, 0, 4], high: [-2, 1, 7], width: 2, thickness: 0.2, hidden: true },
  ], half: [],
});
levels.push(synthetic);
const syntheticIndex = levels.length - 1;
const probes = [], bodies = [], fits = [], capsules = [], cameras = [], segments = [], closest = [], capsuleDistances = [];
function record(stage, level, pos, label) {
  const physics = new Physics(level);
  const radius = 0.4, height = 1.65, lift = 0.28;
  for (const skip of [false, true]) {
    const g = physics.groundProbe(pos.x, pos.y, pos.z, 0.6, 1.2, 0.42, new GroundHit(), skip);
    probes.push({ stage, label, pos: vec(pos), skip, hit: g.hit, center: g.center, grate: g.grate,
      y: g.hit ? g.y : 0, normal: vec(g.normal), block: g.block, face: g.face,
      u: g.hit && g.face >= 0 ? g.u : 0, v: g.hit && g.face >= 0 ? g.v : 0 });
    for (const horizontal of [false, true]) {
      const bpos = pos.clone(), bc = makeContacts();
      physics.collideBody(bpos, radius, lift, height, bc, horizontal, skip);
      bodies.push({ stage, label, pos: vec(pos), skip, horizontal, result: vec(bpos), c: bc });
    }
    fits.push({ stage, label, pos: vec(pos), skip, fits: physics.bodyFits(pos, radius, lift, height, skip) });
    const cpos = pos.clone(), cc = makeContacts();
    physics.collideCapsule(cpos, radius, height, cc, 3, skip);
    capsules.push({ stage, label, pos: vec(pos), skip, result: vec(cpos), c: cc });
  }
  const back = V(1, 0.12, 0).normalize();
  const cam = physics.cameraProbe(pos.clone().add(V(0, 1, 0)), back, 5, 0.62, {});
  cameras.push({ stage, label, pivot: vec(pos.clone().add(V(0, 1, 0))), back: vec(back), ...cam });
}
for (const [stage, level] of levels.entries()) {
  if (stage === syntheticIndex) {
    for (const [label, p] of [
      ['floor', V(0, 0.08, 0)], ['step', V(0.8, 0, 0)], ['wall', V(3.7, 0, 0)],
      ['grate', V(-2, 0.2, 0)], ['roof', V(-6, 2.41, 0)], ['ramp', V(-2, 0.7, 5.5)],
      ['ledge', V(10.15, 0.02, 0)], ['inside', V(2, 0.1, 0)],
    ]) record(stage, level, p, label);
    continue;
  }
  const chosen = [level.spawnPads[0].clone(), level.spawnPads[1].clone()];
  for (const predicate of [b => b.grate, b => b.roof, b => b.axes[1].y < 0.99,
                            b => b.hidden && !b.grate]) {
    const block = level.blocks.find(predicate);
    if (block) chosen.push(block.center.clone().add(V(0, block.half.y + 0.05, 0)));
  }
  chosen.forEach((p, i) => record(stage, level, p, `real${i}`));
}

const syntheticPhysics = new Physics(synthetic);
for (const [a, b, skip] of [
  [V(3, 1, 0), V(5, 1, 0), false], [V(-2, 1, 0), V(-2, -1, 0), false],
  [V(-2, 1, 0), V(-2, -1, 0), true], [V(0, 0, 0), V(0, 0, 0), false],
  [V(-6, 3, 0), V(-6, 1, 0), false],
]) {
  const hit = syntheticPhysics.segment(a,b,undefined,skip);
  segments.push({ a: vec(a), b: vec(b), skip, hit: hit.hit, dist: hit.hit ? hit.dist : 0, block: hit.block });
}
for (const [block, p] of [[2,V(3.1,1.2,0)], [3,V(-2,0.8,3)], [5,V(-2,2,5.5)]]) {
  closest.push({ block, p: vec(p), result: vec(syntheticPhysics.closestOnBlock(synthetic.blocks[block],p,new THREE.Vector3())) });
}
for (const [a,b,base] of [
  [V(-1,1,0),V(2,1,0),V(0,0,0)], [V(0,3,0),V(0,-1,0),V(0,0,0)],
]) {
  const point = Physics.pointCapsuleDist(a,base,0.4,1.65);
  const segment = Physics.segmentCapsuleDist(a,b,base,0.4,1.65,{});
  capsuleDistances.push({a:vec(a),b:vec(b),base:vec(base),point,segment});
}

const contact = c => `${bool(c.ground)}, ${bool(c.wall)}, ${bool(c.ceiling)}, ${cppVec(vec(c.groundNormal))}, ${cppVec(vec(c.wallNormal))}, ${c.groundBlock}, ${c.wallBlock}`;
const row = (f, output) => `  {${f.stage}, ${cppVec(f.pos)}, ${bool(f.skip)}, ${output}},`;
const blockCpp = b => `{${cppVec(vec(b.center))}, ${cppVec(vec(b.half))}, {{${b.axes.map(a => cppVec(vec(a))).join(', ')}}}, {${b.faces.join(', ')}}, ${[b.solid,b.grate,b.roof,b.perch,b.noNav,b.rail,b.hidden].map(bool).join(', ')}}`;
const lines = [
  '// Generated from actual splat-ref Physics by tools/export-controller-fixtures.mjs.',
  '#pragma once', '#include "../restoration/ReferenceData.hpp"', '#include <array>',
  'namespace restoration::controller_fixtures {',
  `inline const std::array<BlockData, ${synthetic.blocks.length}> syntheticBlocks = {{`,
  ...synthetic.blocks.map(b => `  ${blockCpp(b)},`), '}};',
  'struct Probe { int stage; glm::dvec3 pos; bool skip,hit,center,grate; double y; glm::dvec3 normal; int block,face; double u,v; };',
  `inline const std::array<Probe, ${probes.length}> probes = {{`,
  ...probes.map(f => row(f, `${bool(f.hit)}, ${bool(f.center)}, ${bool(f.grate)}, ${num(f.y)}, ${cppVec(f.normal)}, ${f.block}, ${f.face}, ${num(f.u)}, ${num(f.v)}`)), '}};',
  'struct Collision { int stage; glm::dvec3 pos; bool skip,horizontal; glm::dvec3 result; bool ground,wall,ceiling; glm::dvec3 groundNormal,wallNormal; int groundBlock,wallBlock; };',
  `inline const std::array<Collision, ${bodies.length}> bodies = {{`,
  ...bodies.map(f => row(f, `${bool(f.horizontal)}, ${cppVec(f.result)}, ${contact(f.c)}`)), '}};',
  `inline const std::array<Collision, ${capsules.length}> capsules = {{`,
  ...capsules.map(f => row(f, `false, ${cppVec(f.result)}, ${contact(f.c)}`)), '}};',
  'struct Fit { int stage; glm::dvec3 pos; bool skip,fits; };',
  `inline const std::array<Fit, ${fits.length}> fits = {{`,
  ...fits.map(f => row(f, bool(f.fits))), '}};',
  'struct Camera { int stage; glm::dvec3 pivot,back; double hard,soft; bool floor; };',
  `inline const std::array<Camera, ${cameras.length}> cameras = {{`,
  ...cameras.map(f => `  {${f.stage}, ${cppVec(f.pivot)}, ${cppVec(f.back)}, ${num(f.hard)}, ${num(f.soft)}, ${bool(f.floor)}},`), '}};',
  'struct Segment { glm::dvec3 a,b; bool skip,hit; double dist; int block; };',
  `inline const std::array<Segment, ${segments.length}> segments = {{`,
  ...segments.map(f => `  {${cppVec(f.a)}, ${cppVec(f.b)}, ${bool(f.skip)}, ${bool(f.hit)}, ${num(f.dist)}, ${f.block}},`), '}};',
  'struct Closest { int block; glm::dvec3 point,result; };',
  `inline const std::array<Closest, ${closest.length}> closest = {{`,
  ...closest.map(f => `  {${f.block}, ${cppVec(f.p)}, ${cppVec(f.result)}},`), '}};',
  'struct CapsuleDistance { glm::dvec3 a,b,base; double point,segment,t; };',
  `inline const std::array<CapsuleDistance, ${capsuleDistances.length}> capsuleDistances = {{`,
  ...capsuleDistances.map(f => `  {${cppVec(f.a)}, ${cppVec(f.b)}, ${cppVec(f.base)}, ${num(f.point)}, ${num(f.segment.dist)}, ${num(f.segment.t)}},`), '}};',
  '} // namespace restoration::controller_fixtures', '',
];
const path = resolve(import.meta.dirname, '../tests/controller_fixtures.hpp');
const generated = lines.join('\n');
if (process.argv.includes('--check')) {
  if (await readFile(path, 'utf8') !== generated) throw Error('Controller fixtures are stale');
} else await writeFile(path, generated);
console.log(`${probes.length} ground, ${bodies.length} body, ${capsules.length} capsule, ${fits.length} fit, ${cameras.length} camera fixtures`);
