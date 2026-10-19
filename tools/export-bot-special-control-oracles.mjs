import { register } from 'node:module';
import { readFile, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';

const root = resolve(import.meta.dirname, '..');
register(new URL('./reference-loader.mjs', import.meta.url));
const THREE = await import('three');
const { OFFLINE_MAPS, WEAPONS, SUBS, SPECIALS } = await import('../splat-ref/src/config.js');
const { MAP_LAYOUTS } = await import('../splat-ref/src/world/maps.js');
const { layoutFor, inMode } = await import('../splat-ref/src/world/variants.js');
const { dressingFor } = await import('../splat-ref/src/world/dressing.js');
const { PropKit } = await import('../splat-ref/src/world/props.js');
const { Level } = await import('../splat-ref/src/world/level.js');
const { Physics } = await import('../splat-ref/src/game/physics.js');
const { NavGraph } = await import('../splat-ref/src/game/nav.js');
const { BotBrain } = await import('../splat-ref/src/game/bots.js');
const { MAIN_KITS, SUB_KITS } = await import('../splat-ref/src/game/kits/registry.js');
const { G, rng } = await import('../splat-ref/src/core/ctx.js');

const map = OFFLINE_MAPS.find((item) => item.id === 'halyard');
const layout = MAP_LAYOUTS[map.layout || map.id];
const kit = new PropKit(null, { quality: 'high', headless: true });
const colliders = [];
try {
  for (const item of dressingFor(map.layout || map.id)) if (inMode(item, 'turf')) colliders.push(...kit.add(item.type, item).colliders);
} finally { kit.dispose(); }
const level = new Level(layoutFor(layout, 'turf'), colliders);
for (const face of level.faces) if (face.paintable) face.atlas = true;
const physics = new Physics(level);
const nav = new NavGraph(level, physics);

function duelPositions() {
  for (const id of nav.validIds) {
    const a = nav.nodes[id];
    if (a.zone >= 0 || a.wet) continue;
    for (const otherId of nav.validIds) {
      const b = nav.nodes[otherId], d = Math.hypot(b.x - a.x, b.z - a.z);
      if (b.zone >= 0 || b.wet || d < 7 || d > 8.5 || Math.abs(a.y - b.y) > 0.35) continue;
      if (physics.los(new THREE.Vector3(a.x, a.y + 1.3, a.z), new THREE.Vector3(b.x, b.y + 1, b.z)))
        return [[a.x, a.y, a.z], [b.x, b.y, b.z]];
    }
  }
  throw new Error('No unobstructed native-comparable duel positions on halyard');
}
const [botPos, duelFoePos] = duelPositions();
function longDuelFoe() {
  const a = new THREE.Vector3(...botPos);
  for (const id of nav.validIds) {
    const b = nav.nodes[id], d = Math.hypot(b.x - a.x, b.z - a.z);
    if (b.zone >= 0 || b.wet || d < 9 || d > 14 || Math.abs(a.y - b.y) > 0.35) continue;
    if (physics.los(new THREE.Vector3(a.x, a.y + 1.3, a.z), new THREE.Vector3(b.x, b.y + 1, b.z)))
      return [b.x, b.y, b.z];
  }
  throw new Error('No unobstructed long-range duel position on halyard');
}
const crabFoePos = longDuelFoe();
const closeFoePos = [botPos[0], botPos[1], botPos[2] + 3.2];
const behindFoePos = [botPos[0], botPos[1], botPos[2] - 2.2];

function makeActor(team, pos, specialId, options = {}) {
  const weaponId = 'shooter', subId = 'bomb';
  const actor = {
    team, pos: new THREE.Vector3(...pos), vel: new THREE.Vector3(), yaw: 0, aimYaw: 0, aimPitch: 0,
    aimPoint: new THREE.Vector3(), alive: true, grounded: options.grounded ?? true, climbing: false,
    superJumpState: null, squid: false, groundTeam: 0, ink: 100, hp: 100, special: 0,
    specialActive: null, specialId, weaponId, weapon: WEAPONS[weaponId], subId, sub: SUBS[subId],
    intent: { move: new THREE.Vector3(), aimPoint: new THREE.Vector3(), jump: false, fire: false, sub: false, special: false, squid: false, cheer: false },
    weaponRunner: { charging: false, charge: 0, burstT: 0, streaming: false, rolling: false, turret: false, dodge: false, dualRolls: 0 },
    anim: { form: 'kid' }, form: 'kid', smoothY: 0, lastDamage: 99, fireFacing: 0,
    specialReady() { return this.special >= this.specialCost() && !this.specialActive; },
    specialCost() { return this.weapon.specialCost; },
  };
  if (options.active) {
    const def = SPECIALS[specialId];
    actor.specialActive = {
      id: specialId, kind: def.kind || specialId, def, t: 0, dur: def.duration || 0,
      charge: options.charge ?? 0, hp: options.specialHp ?? def.hp ?? 0,
      bodyYaw: 0, thrown: false,
    };
  }
  return actor;
}

function run(spec) {
  // Controller-only oracle: BotBrain advances against a fixed actor pose; active specials are synthetic source states, not Actor.tick trajectories.
  const seed = 3;
  Math.random = rng(seed);
  const pos = spec.botPos || botPos, foePos = spec.foePos || duelFoePos;
  const actor = makeActor(0, pos, spec.special, {
    active: spec.active, charge: spec.charge, specialHp: spec.specialHp, grounded: spec.grounded,
  });
  const enemy = makeActor(1, foePos, 'zooka');
  if (spec.ready) actor.special = actor.specialCost();
  const paint = { regionStats(_x, _y, _z, _radius, _team, out = {}) { return Object.assign(out, { own: 0, enemy: 0, empty: 1, n: 1 }); } };
  G.level = level; G.physics = physics; G.nav = nav; G.paint = paint;
  G.match = { state: 'playing', playing() { return true; }, zones: null, duration: 180 };
  G.actors = [actor, enemy]; G.subs = null; G.time = 0;
  SUB_KITS.waddle ||= {};
  SUB_KITS.waddle.threats = () => {};
  MAIN_KITS.brolly ||= {};
  MAIN_KITS.brolly.shields = () => {};
  const brain = new BotBrain(actor, 'normal');
  const dt = 1 / 60;
  let specialFrames = 0, fireFrames = 0, subPresses = 0, jumpFrames = 0, squidFrames = 0;
  let previousSub = false, framesRun = 0;
  for (let frame = 0; frame < spec.frames; ++frame) {
    G.time += dt;
    brain.update(dt);
    ++framesRun;
    if (actor.intent.special) ++specialFrames;
    if (actor.intent.fire) ++fireFrames;
    if (actor.intent.sub && !previousSub) ++subPresses;
    if (actor.intent.jump) ++jumpFrames;
    if (actor.intent.squid) ++squidFrames;
    previousSub = actor.intent.sub;
    if (spec.firstActivation && actor.intent.special) break;
  }
  return {
    name: spec.name, special: spec.special, seed, frames: framesRun, botPos: pos, foePos,
    grounded: actor.grounded, active: !!actor.specialActive, ready: spec.ready || false,
    charge: spec.charge ?? 0,
    mode: brain.mode, targetSeen: brain.target === enemy,
    specialFrames, fireFrames, subPresses, jumpFrames, squidFrames,
    move: [actor.intent.move.x, actor.intent.move.y, actor.intent.move.z],
    fire: actor.intent.fire, sub: actor.intent.sub, specialInput: actor.intent.special,
    jump: actor.intent.jump, squid: actor.intent.squid,
  };
}

const cases = [
  { name: 'activation-ready-kraken', special: 'kraken', ready: true, firstActivation: true, frames: 90 },
  { name: 'grounded-kraken-attack', special: 'kraken', active: true, foePos: closeFoePos, frames: 90 },
  { name: 'zipcaster-grapple', special: 'zipcaster', active: true, frames: 90 },
  { name: 'booyah-full-charge', special: 'booyah', active: true, charge: 1, frames: 90 },
  { name: 'crab-tank', special: 'crab', active: true, foePos: crabFoePos, frames: 90 },
  { name: 'jetpack-fire', special: 'jetpack', active: true, grounded: false, frames: 90 },
  { name: 'blower-cycle', special: 'blower', active: true, frames: 90 },
  { name: 'barrage-bombs', special: 'barrage', active: true, frames: 90 },
  { name: 'barrage-sticky-bombs', special: 'barrage_sticky', active: true, frames: 90 },
  { name: 'zooka-fire', special: 'zooka', active: true, frames: 90 },
  { name: 'stamp-turn', special: 'stamp', active: true, foePos: behindFoePos, frames: 90 },
].map(run);

const n = (v) => Number(v.toFixed(9));
const vec = (v) => `{${v.map(n).join(', ')}}`;
const bool = (v) => v ? 'true' : 'false';
const lines = [
  '#pragma once',
  '// Generated from source BotBrain running-special control scenarios; do not hand edit.',
  '#include <array>',
  '#include <string_view>',
  '#include <glm/vec3.hpp>',
  'namespace restoration::bot_special_fixtures {',
  'struct Oracle { std::string_view name, special; unsigned seed; int frames; glm::dvec3 bot, foe; bool grounded, active, ready; double charge; int specialFrames, fireFrames, subPresses, jumpFrames, squidFrames; glm::dvec3 move; bool fire, sub, specialInput, jump, squid; };',
  `inline const std::array<Oracle, ${cases.length}> oracles{{`,
];
for (const c of cases) {
  console.log(`${c.name}: frames=${c.frames}, special=${c.specialFrames}, fire=${c.fireFrames}, sub=${c.subPresses}, jump=${c.jumpFrames}, squid=${c.squidFrames}`);
  lines.push(`  Oracle{"${c.name}", "${c.special}", ${c.seed}u, ${c.frames}, ${vec(c.botPos)}, ${vec(c.foePos)}, ${bool(c.grounded)}, ${bool(c.active)}, ${bool(c.ready)}, ${n(c.charge)}, ${c.specialFrames}, ${c.fireFrames}, ${c.subPresses}, ${c.jumpFrames}, ${c.squidFrames}, ${vec(c.move)}, ${bool(c.fire)}, ${bool(c.sub)}, ${bool(c.specialInput)}, ${bool(c.jump)}, ${bool(c.squid)}},`);
}
lines.push('}};', '} // namespace restoration::bot_special_fixtures', '');
const file = resolve(root, 'tests/bot_special_control_fixtures.hpp');
const output = lines.join('\n');
if (process.argv.includes('--check')) {
  if (await readFile(file, 'utf8') !== output) throw new Error('Bot special-control fixtures differ from source BotBrain');
} else await writeFile(file, output);
