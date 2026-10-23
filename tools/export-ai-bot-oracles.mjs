import { register } from 'node:module';
import { readFile, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';

const root = resolve(import.meta.dirname, '..');
register(new URL('./reference-loader.mjs', import.meta.url));
const THREE = await import('three');
const { OFFLINE_MAPS } = await import('../splat-ref/src/config.js');
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
const { DIFFICULTY, WEAPONS, SUBS, SPECIALS } = await import('../splat-ref/src/config.js');

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
  const ids = nav.validIds;
  for (const id of ids) {
    const a = nav.nodes[id];
    if (a.zone >= 0 || a.wet) continue;
    for (const otherId of ids) {
      const b = nav.nodes[otherId], d = Math.hypot(b.x - a.x, b.z - a.z);
      if (b.zone >= 0 || b.wet || d < 7 || d > 8.5 || Math.abs(a.y - b.y) > 0.35) continue;
      if (physics.los(new THREE.Vector3(a.x, a.y + 1.3, a.z), new THREE.Vector3(b.x, b.y + 1, b.z)))
        return [[a.x, a.y, a.z], [b.x, b.y, b.z]];
    }
  }
  throw new Error('No unobstructed native-comparable duel positions on halyard');
}
const [botPos, foePos] = duelPositions();

function blockedDuelPositions() {
  for (const id of nav.validIds) {
    const a = nav.nodes[id];
    if (a.zone >= 0 || a.wet) continue;
    for (const otherId of nav.validIds) {
      const b = nav.nodes[otherId], d = Math.hypot(b.x - a.x, b.z - a.z);
      if (b.zone >= 0 || b.wet || d < 4 || d > 14 || Math.abs(a.y - b.y) > 0.35) continue;
      if (!physics.los(new THREE.Vector3(a.x, a.y + 1.3, a.z), new THREE.Vector3(b.x, b.y + 1, b.z)))
        return [[a.x, a.y, a.z], [b.x, b.y, b.z]];
    }
  }
  throw new Error('No blocked-line-of-sight positions on halyard');
}
const [blockedBotPos, blockedFoePos] = blockedDuelPositions();

function seededStart(seed) {
  const random = rng(seed);
  random(); random(); random();
  return random() * 0.2;
}
function seedForStart() {
  for (let seed = 1; seed < 100000; seed++) if (seededStart(seed) <= 1 / 60) return seed;
  throw new Error('Could not find a deterministic first-frame perception seed');
}

function makeActor(team, pos, options = {}) {
  const actor = {
    team, pos: new THREE.Vector3(...pos), vel: new THREE.Vector3(), yaw: 0, aimYaw: 0, aimPitch: 0,
    aimPoint: new THREE.Vector3(),
    alive: true, grounded: true, climbing: false, superJumpState: null, squid: false, groundTeam: 0,
    ink: options.ink ?? 100, hp: options.hp ?? 100, special: 0, specialActive: null, specialId: 'zooka',
    weaponId: options.weaponId || 'shooter', weapon: WEAPONS[options.weaponId || 'shooter'], subId: options.subId || 'bomb', sub: SUBS[options.subId || 'bomb'],
    intent: { move: new THREE.Vector3(), aimPoint: new THREE.Vector3(), jump: false, fire: false, sub: false, special: false, squid: false, cheer: false },
    weaponRunner: { charging: false, charge: 0, burstT: 0, streaming: false, rolling: false, turret: false, dodge: false, dualRolls: 0 },
    anim: { form: 'kid' }, form: 'kid', smoothY: 0, lastDamage: 99, fireFacing: 0,
    specialReady() { return false; }, specialCost() { return SPECIALS[this.specialId]?.cost || 1000; },
  };
  return actor;
}

function run(spec) {
  const seed = spec.seed ?? seedForStart();
  Math.random = rng(seed);
  const actor = makeActor(0, spec.botPos || botPos, { ink: spec.ink, hp: spec.hp });
  const enemy = makeActor(1, spec.foePos || foePos, {
    subId: spec.threat === 'waddle' ? 'waddle' : 'bomb', weaponId: spec.threat === 'shield-fight' ? 'brolly' : 'shooter',
  });
  const paint = { regionStats(_x, _y, _z, _radius, _team, out = {}) { return Object.assign(out, { own: 0, enemy: 0, empty: 1, n: 1 }); } };
  G.level = level; G.physics = physics; G.nav = nav; G.paint = paint;
  G.match = { state: 'playing', playing() { return true; }, zones: null, duration: 180 };
  G.actors = [actor, ...(spec.foe === false ? [] : [enemy])]; G.subs = null; G.time = 0;
  // The source AI consumes these read-only query descriptors; keep the scenario fixed while testing the consumer.
  G.__oracleThreats = spec.threat === 'waddle' ? [{
    kind: 'waddle', team: 1, owner: enemy, pos: new THREE.Vector3(enemy.pos.x, enemy.pos.y + 1.35, enemy.pos.z),
    vel: new THREE.Vector3(0, 0, SUBS.waddle.throwSpeed),
    aimY: 0.198, radius: SUBS.waddle.radius, trigger: SUBS.waddle.triggerDist, speed: SUBS.waddle.speed,
    senseRadius: SUBS.waddle.senseRadius, ground: true, live: true, state: 'fly', shootable: false, locked: false,
    target: null, left: SUBS.waddle.life,
  }] : [];
  SUB_KITS.waddle ||= {};
  SUB_KITS.waddle.threats = (out) => out.push(...G.__oracleThreats);
  const shieldFight = spec.threat === 'shield-fight';
  const canopyRoute = spec.threat === 'canopy-route';
  const bot = new THREE.Vector3(...(spec.botPos || botPos)), foe = new THREE.Vector3(...(spec.foePos || foePos));
  const normal = shieldFight ? bot.clone().sub(foe).normalize() : new THREE.Vector3(0, 0, 1);
  const center = shieldFight
    ? foe.clone().add(new THREE.Vector3(0, 0.9, 0)).addScaledVector(normal, 0.62)
    : new THREE.Vector3(bot.x, bot.y + 0.76, bot.z - 1.5 - 0.17);
  const shieldPos = canopyRoute ? new THREE.Vector3(bot.x, bot.y, bot.z - 1.5) : foe.clone();
  G.__oracleShields = shieldFight || canopyRoute ? [{
    id: 1, kind: 'brolly', team: 1, owner: enemy, C: center, N: normal, R: shieldFight ? 0.8 : 0.8,
    hp: WEAPONS.brolly.canopyHp, hpMax: WEAPONS.brolly.canopyHp, left: 8,
    held: shieldFight, live: true, blocksActors: canopyRoute,
    pos: shieldPos, halfW: 0.8, planeOff: -0.17, speed: canopyRoute ? WEAPONS.brolly.launchSpeed : 0,
  }] : [];
  MAIN_KITS.brolly ||= {};
  MAIN_KITS.brolly.shields = (out) => out.push(...G.__oracleShields);
  const brain = new BotBrain(actor, spec.difficulty);
  const sourceSteer = brain._steer.bind(brain);
  brain._steer = (step) => {
    const result = sourceSteer(step);
    brain._lastSteer = [result.x, result.y, result.z];
    return result;
  };
  const dt = 1 / 60, frames = spec.frames;
  let fireFrames = 0, subPresses = 0, prevSub = false;
  for (let i = 0; i < frames; ++i) {
    G.time += dt;
    brain.update(dt);
    if (actor.intent.fire) fireFrames++;
    if (actor.intent.sub && !prevSub) subPresses++;
    prevSub = actor.intent.sub;
  }
  return {
    name: spec.name, difficulty: spec.difficulty, seed, frames, botPos: spec.botPos || botPos, foePos: spec.foePos || foePos,
    ink: spec.ink ?? 100, hp: spec.hp ?? 100, hasFoe: spec.foe !== false, threat: spec.threat || '',
    mode: brain.mode,
    targetSeen: brain.target === enemy, fireFrames, subPresses,
    goal: brain.goal, pathIndex: brain.pi, path: brain.path?.slice(0, 8) || [], steer: brain._lastSteer,
    strafe: [brain.strafe, brain.strafeAmp, brain.strafeS],
    move: [actor.intent.move.x, actor.intent.move.y, actor.intent.move.z],
    aim: [actor.aimYaw, actor.aimPitch],
    action: { fire: actor.intent.fire, sub: actor.intent.sub, special: actor.intent.special,
      squid: actor.intent.squid, jump: actor.intent.jump, cheer: actor.intent.cheer },
  };
}

const seed = seedForStart();
let retreatSeed = 1;
for (; retreatSeed < 10000; ++retreatSeed) {
  const probe = run({ name: 'retreat-seed-probe', difficulty: 'normal', frames: 2, seed: retreatSeed, hp: 15 });
  if (probe.mode === 'retreat') break;
}
if (retreatSeed === 10000) throw new Error('Could not find a deterministic low-HP retreat seed');
const cases = [
  ...['easy', 'normal', 'hard'].map((difficulty) => ({ name: `duel-${difficulty}`, difficulty, frames: 90, seed })),
  { name: 'paint', difficulty: 'normal', frames: 30, seed, foe: false },
  { name: 'blocked-los', difficulty: 'normal', frames: 30, seed, botPos: blockedBotPos, foePos: blockedFoePos },
  { name: 'low-ink-refill', difficulty: 'normal', frames: 2, seed, foe: false, ink: 5 },
  { name: 'low-hp-retreat', difficulty: 'normal', frames: 2, seed: retreatSeed, hp: 15 },
  { name: 'waddle-device', difficulty: 'normal', frames: 90, seed, foe: false, threat: 'waddle',
    foePos: [botPos[0], botPos[1], botPos[2] - 8] },
  { name: 'shield-fight', difficulty: 'normal', frames: 90, seed, threat: 'shield-fight' },
  { name: 'canopy-route', difficulty: 'normal', frames: 30, seed, foe: false, threat: 'canopy-route' },
].map(run);
const n = (v) => Number(v.toFixed(9));
const vec = (v) => `{${v.map(n).join(', ')}}`;
const bool = (v) => v ? 'true' : 'false';
const lines = [
  '#pragma once',
  '// Generated from source BotBrain with patched mulberry32 RNG and fixed source-shaped device/shield query snapshots; do not hand edit.',
  '#include <array>',
  '#include <string_view>',
  '#include <glm/vec3.hpp>',
  'namespace restoration::ai_bot_fixtures {',
  'struct Oracle { std::string_view name, difficulty; unsigned seed; int frames; glm::dvec3 bot, foe; double ink, hp; bool hasFoe; std::string_view threat, mode; bool targetSeen; int fireFrames, subPresses; glm::dvec3 move; glm::dvec2 aim; bool fire, sub, special, squid, jump, cheer; };',
  `inline const std::array<Oracle, ${cases.length}> oracles{{`,
];
for (const c of cases) {
  console.log(`${c.name}: mode=${c.mode}, target=${c.targetSeen}, fire=${c.fireFrames}, subs=${c.subPresses}, move=${c.move.join(',')}, aim=${c.aim.map(n).join('/')}, cheer=${c.action.cheer}`);
  lines.push(`  Oracle{"${c.name}", "${c.difficulty}", ${c.seed}u, ${c.frames}, ${vec(c.botPos)}, ${vec(c.foePos)}, ${n(c.ink)}, ${n(c.hp)}, ${bool(c.hasFoe)}, "${c.threat}", "${c.mode}", ${bool(c.targetSeen)}, ${c.fireFrames}, ${c.subPresses}, ${vec(c.move)}, {${c.aim.map(n).join(', ')}}, ${bool(c.action.fire)}, ${bool(c.action.sub)}, ${bool(c.action.special)}, ${bool(c.action.squid)}, ${bool(c.action.jump)}, ${bool(c.action.cheer)}},`);
}
lines.push('}};', '} // namespace restoration::ai_bot_fixtures', '');
const file = resolve(root, 'tests/fixtures/ai_bot_oracle_fixtures.hpp');
const output = lines.join('\n');
if (process.argv.includes('--check')) {
  if (await readFile(file, 'utf8') !== output) throw new Error('AI bot oracle fixtures differ from source BotBrain');
} else await writeFile(file, output);
