// Source Actor oracle for the local Spritzer subset. Run: node tools/export-player-fixtures.mjs
import { register } from 'node:module';
import { readFileSync, writeFileSync } from 'node:fs';
register(new URL('./reference-loader.mjs', import.meta.url));
const THREE = await import('three');
const { Actor } = await import('../splat-ref/src/game/actor.js');
const { Physics } = await import('../splat-ref/src/game/physics.js');
const { G } = await import('../splat-ref/src/core/ctx.js');

const axis = [new THREE.Vector3(1, 0, 0), new THREE.Vector3(0, 1, 0), new THREE.Vector3(0, 0, 1)];
const block = (id, center, half, faces) => ({
  id, center: new THREE.Vector3(...center), half: new THREE.Vector3(...half), axes: axis,
  faces, solid: true, grate: false, roof: false, rail: false,
  aabbMin: new THREE.Vector3(...center).sub(new THREE.Vector3(...half)),
  aabbMax: new THREE.Vector3(...center).add(new THREE.Vector3(...half)),
});
const level = {
  blocks: [block(0, [0, -0.5, 0], [20, 0.5, 20], [-1, -1, 0, -1, -1, -1]),
           block(1, [0, 1, 8], [4, 1, 0.2], [-1, -1, -1, -1, -1, 1])],
  faces: [
    { origin: new THREE.Vector3(-20, 0, -20), u: axis[0], v: axis[2] },
    { origin: new THREE.Vector3(-4, 0, 7.8), u: axis[0], v: axis[1] },
  ],
  spawnPads: [new THREE.Vector3(), new THREE.Vector3()], spawnBarrier: 0, hasRails: false,
  queryBlocks(x0, z0, x1, z1, out = []) {
    out.length = 0;
    for (const b of this.blocks)
      if (b.aabbMax.x >= x0 && b.aabbMin.x <= x1 && b.aabbMax.z >= z0 && b.aabbMin.z <= z1) out.push(b.id);
    return out;
  },
  groundHeight(x, z, ymax = 50) { return x >= -20 && x <= 20 && z >= -20 && z <= 20 && ymax >= 0 ? 0 : -Infinity; },
};
class Character {
  constructor() { this.root = new THREE.Group(); }
  setVisible() {} setHurt() {} trigger() {} update() {} setSub() {}
  getMuzzle(out) { out.set(0, 1.05, 0.3); }
  aimReady() { return 1; }
}
G.level = level;
G.physics = new Physics(level);
G.paint = { sample: () => 0 };
G.projectiles = { fireShooter() {} };
G.teamColors = [new THREE.Color('orange'), new THREE.Color('blue')];
G.actors = [];
const actor = new Actor({ team: 0, name: 'fixture', CharacterClass: Character });
G.actors.push(actor);
const scenarios = [
  ['run', 120, i => ({ move: [0, 0, 1] })],
  ['brake', 120, i => ({ move: i < 60 ? [0, 0, 1] : [0, 0, 0] })],
  ['reverse', 120, i => ({ move: i < 60 ? [0, 0, 1] : [0, 0, -1] })],
  ['turn', 120, i => ({ move: i < 60 ? [0, 0, 1] : [1, 0, 0] })],
  ['jump', 120, i => ({ jump: i === 0 })],
  ['squidDry', 120, i => ({ squid: true, move: [0, 0, 1] })],
  ['swim', 35, i => ({ squid: true, move: [0, 0, 1], surface: 1 })],
  ['enemy', 120, i => ({ move: [0, 0, 1], surface: 2 })],
  ['air', 100, i => ({ jump: i === 0, move: i < 30 ? [0, 0, 1] : [1, 0, 0] })],
  ['fireMove', 120, i => ({ fire: true, move: [0, 0, 1] })],
  ['wall', 120, i => ({ squid: true, move: [0, 0, 1], wallInk: true })],
  ['fireRelease', 120, i => ({ fire: i < 30, move: [1, 0, 0] })],
  ['airFire', 120, i => ({ fire: true, jump: i === 0, move: [0, 0, 1] })],
  ['step', 120, i => ({ move: [0, 0, 1] })],
  ['roof', 120, i => ({})],
  ['rail', 120, i => ({ move: [0, 0, 1] })],
  ['grate', 120, i => ({ squid: true })],
  ['barrier', 120, i => ({ move: [0, 0, 1] })],
];
const rows = [];
for (const dt of [1 / 60, 1 / 120])
for (const [name, frames, intent] of scenarios) {
  level.blocks.length = 2;
  level.blocks[0].roof = name === 'roof'; level.blocks[0].grate = name === 'grate';
  level.hasRails = name === 'rail';
  if (name === 'step') level.blocks.push(block(2, [0, 0.1, 2], [2, 0.1, 1], [-1,-1,-1,-1,-1,-1]));
  if (name === 'rail') { const rail = block(2, [0, 0.5, 0], [0.035, 0.06, 4], [-1,-1,-1,-1,-1,-1]); rail.rail = true; level.blocks.push(rail); }
  level.spawnPads[1].set(0, 0, 4); level.spawnBarrier = name === 'barrier' ? 2 : 0;
  actor.spawnAt(new THREE.Vector3(name === 'rail' ? 0.15 : 0, name === 'rail' ? 0.56 : 0, name === 'wall' ? 7 : 0), 0);
  actor.invuln = 0;
  for (let i = 0; i < frames; ++i) {
    const now = intent(i);
    G.paint.sample = face => face === 0 ? (now.surface || 0) : (now.wallInk ? 1 : 0);
    actor.intent.move.set(...(now.move || [0, 0, 0]));
    actor.intent.jump = !!now.jump; actor.intent.squid = !!now.squid; actor.intent.fire = !!now.fire;
    G.time += dt;
    actor.update(dt);
    if (i === 0 || i === 5 || i === 11 || i === 23 || i === 35 || i === 59 || i === 60 || i === 71 || i === 95 || i === frames - 1)
      rows.push({ name, dt, frame: i + 1, x: actor.pos.x, y: actor.pos.y, z: actor.pos.z,
        vx: actor.vel.x, vy: actor.vel.y, vz: actor.vel.z, yaw: actor.yaw,
        hp: actor.hp, ink: actor.ink, ground: +actor.grounded, squid: +(actor.form === 'squid'),
        submerged: +actor.submerged, climbing: +actor.climbing });
  }
}
const n = value => Number(value).toPrecision(17);
const lines = [
  '#pragma once',
  '#include <array>',
  'namespace restoration::test {',
  'struct PlayerFixture { const char* scenario; int frame; double dt,x,y,z,vx,vy,vz,yaw,hp,ink; int ground,squid,submerged,climbing; };',
  `inline const std::array<PlayerFixture, ${rows.length}>& playerFixtures() {`,
  `  static const std::array<PlayerFixture, ${rows.length}> fixtures{{`,
  ...rows.map(r => `    {"${r.name}", ${r.frame}, ${[r.dt,r.x,r.y,r.z,r.vx,r.vy,r.vz,r.yaw,r.hp,r.ink].map(n).join(', ')}, ${r.ground}, ${r.squid}, ${r.submerged}, ${r.climbing}},`),
  '  }};',
  '  return fixtures;',
  '}',
  '} // namespace restoration::test',
  '',
];
const target = new URL('../tests/player_fixtures.hpp', import.meta.url), output = lines.join('\n');
if (process.argv.includes('--check')) {
  if (readFileSync(target, 'utf8') !== output) throw new Error('Player fixtures differ from reference Actor');
} else writeFileSync(target, output);
console.log(`${process.argv.includes('--check') ? 'Verified' : 'Wrote'} ${rows.length} actual Actor samples`);
