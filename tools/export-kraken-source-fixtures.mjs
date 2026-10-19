// Actual-source behavioral oracle for Kraken. It drives Actor.update and SpecialSystem,
// capturing state transitions, movement, damage, paint, and lifecycle events.
import { createHash } from 'node:crypto';
import { readFileSync, writeFileSync } from 'node:fs';
import { register } from 'node:module';
register(new URL('../tools/reference-loader.mjs', import.meta.url));

const THREE = await import('three');
const { G, on } = await import('../splat-ref/src/core/ctx.js');
const { Actor } = await import('../splat-ref/src/game/actor.js');
const { SpecialSystem } = await import('../splat-ref/src/game/specials.js');
const { PLAYER, SPECIALS } = await import('../splat-ref/src/config.js');
// Resolve the same optional procedural prop module that SpecialSystem loads asynchronously.
await import('../splat-ref/src/game/special-props.js');

const sourcePaths = [
  '../splat-ref/src/config.js',
  '../splat-ref/src/game/actor.js',
  '../splat-ref/src/game/specials.js',
  '../splat-ref/src/game/special-props.js',
  '../splat-ref/src/game/physics.js',
  '../splat-ref/src/game/weapons.js',
];
const sourceTexts = sourcePaths.map((path) => readFileSync(new URL(path, import.meta.url), 'utf8'));
const hashes = sourceTexts.map((source) => createHash('sha256').update(source).digest('hex'));
const seededRandom = (seed) => () => {
  seed |= 0; seed = seed + 0x6d2b79f5 | 0;
  let t = Math.imul(seed ^ seed >>> 15, 1 | seed);
  t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t;
  return ((t ^ t >>> 14) >>> 0) / 4294967296;
};
const nativeRandom = Math.random;
const round = (x) => Number(Number(x || 0).toPrecision(15));
const vec = (v) => [round(v.x), round(v.y), round(v.z)];
const scalar = (x) => round(x);
const dt = 1 / 60;
const events = [];
const eventNames = ['special:use', 'special:start', 'special:end', 'special:slam', 'actor:form', 'actor:enemyInk', 'actor:land', 'damage', 'splatted'];
const unlisten = eventNames.map((name) => on(name, (event) => events.push({ name, event })));
const cleanEvent = (entry) => {
  if (!entry) return null;
  const e = entry.event;
  return {
    name: entry.name,
    actor: e.actor?.name ?? null,
    victim: e.victim?.name ?? null,
    attacker: e.attacker?.name ?? null,
    id: e.id ?? null,
    reason: e.reason ?? null,
    form: e.form ?? null,
    on: e.on ?? null,
    pos: e.pos ? vec(e.pos) : null,
    radius: e.radius == null ? null : scalar(e.radius),
    amount: e.amount == null ? null : scalar(e.amount),
    source: e.source ?? e.cause ?? null,
  };
};

class HeadlessCharacter {
  constructor({ weapon }) {
    this.root = { position: new THREE.Vector3(), rotation: { y: 0 } };
    this.weapon = { pivot: { visible: true }, left: { pivot: { visible: true } } };
    this.left = { pivot: { visible: true } };
    this.weaponId = weapon;
    this.weaponHidden = false;
    this.visible = true;
    this.triggers = [];
    this.visibility = [];
  }
  setSub(id) { this.subId = id; }
  setWeapon(id) { this.weaponId = id; }
  trigger(name, data) { this.triggers.push({ name, data: data ?? null }); }
  setVisible(value) { this.visible = value; this.visibility.push(value); }
  setHurt() {}
  update() {}
}

const scene = {
  objects: new Set(),
  add(object) { this.objects.add(object); },
  remove(object) { this.objects.delete(object); },
};
let paintCalls = [];
let hitCalls = [];
let areaCalls = [];
let blockedLosX = null;
function setEnvironment(actors = [], { groundSample = 2, blockLosX = null } = {}) {
  G.teamColors = [new THREE.Color('#ff8a14'), new THREE.Color('#2f5bff')];
  G.teamHex = ['#ff8a14', '#2f5bff'];
  G.time = 0; G.mode = 'match';
  G.audio = null; G.fx = null; G.input = null; G.camera = null; G.rig = null; G.match = null; G.boss = null;
  G.nav = null;
  G.level = {
    bounds: { minX: -100, maxX: 100, minZ: -100, maxZ: 100 },
    spawnPads: [new THREE.Vector3(100, 0, 100), new THREE.Vector3(-100, 0, -100)],
    spawnBarrier: 0, hasRails: false, blocks: [],
    groundHeight() { return 0; },
    pointInside() { return false; },
    queryBlocks() { return []; },
  };
  G.physics = {
    groundProbe(_x, _y, _z, _up, _down, _radius, hit) {
      hit.hit = true; hit.y = 0; hit.normal.set(0, 1, 0);
      hit.face = 1; hit.u = 0; hit.v = 0; hit.block = -1; hit.center = false; hit.grate = false;
      return hit;
    },
    collideBody() { return { wall: false, ceiling: false, wallNormal: new THREE.Vector3() }; },
    raycast(from) { return { hit: false, point: new THREE.Vector3(from.x, 0, from.z), normal: new THREE.Vector3(0, 1, 0), face: -1, u: 0, v: 0 }; },
    los(_from, to) { return blockLosX == null || Math.abs(to.x - blockLosX) > 0.15; },
  };
  G.paint = {
    sample() { return groundSample; },
    splat(pos, radius, team, options = {}) {
      paintCalls.push({ pos: vec(pos), radius: scalar(radius), team, seed: scalar(options.seed) });
      return Math.PI * radius * radius;
    },
  };
  G.subs = { blockActor() {}, damageArea(pos, radius, damage, team) { areaCalls.push({ pos: vec(pos), radius: scalar(radius), damage: scalar(damage), team }); } };
  G.projectiles = {
    applyHit(owner, victim, amount, weaponId) {
      hitCalls.push({ owner: owner.name, victim: victim.name, amount: scalar(amount), weaponId });
      victim.damage(amount, owner, weaponId);
    },
  };
  G.actors = actors;
  paintCalls = []; hitCalls = []; areaCalls = []; blockedLosX = blockLosX;
}

function makeActor(name, team = 0, { x = 0, y = 0, z = 0, grounded = true } = {}) {
  const actor = new Actor({ team, name, weapon: 'shooter', special: 'kraken', isLocal: false, isBot: false, CharacterClass: HeadlessCharacter });
  actor.pos.set(x, y, z);
  actor.grounded = grounded;
  actor.groundTeam = 0;
  actor.ground.hit = grounded;
  actor.ground.y = 0;
  actor.ground.normal.set(0, 1, 0);
  actor.ground.face = 1; actor.ground.u = 0; actor.ground.v = 0; actor.ground.block = -1;
  actor.lastFire = 0;
  actor.lastDamage = 0;
  actor.hp = PLAYER.hp;
  return actor;
}

function activate(actor, system) {
  G.specials = system;
  actor.special = actor.specialCost();
  actor.intent.special = true;
  actor.update(dt);
  actor.intent.special = false;
  return actor.specialActive;
}

function newSystem(actors = [], environment = {}) {
  setEnvironment(actors, environment);
  scene.objects.clear();
  const system = new SpecialSystem(scene);
  G.specials = system;
  return system;
}

// Activation and held-squid input pass through the real Actor activation and normal update path.
Math.random = seededRandom(0x4b52414b);
events.length = 0;
const activationSystem = newSystem();
const activationActor = makeActor('kraken-activation');
let runnerResets = 0;
const runnerReset = activationActor.weaponRunner.reset;
activationActor.weaponRunner.reset = function (...args) { runnerResets++; return runnerReset.apply(this, args); };
const activation = activate(activationActor, activationSystem);
activationActor.intent.squid = true;
activationActor.update(dt);
activationActor.intent.squid = false;
const activationFixture = {
  activeId: activationActor.specialActive?.id ?? null,
  body: !!activation?.body,
  noSquid: !!activation?.noSquid,
  inkProof: !!activation?.inkProof,
  speed: scalar(activation?.speed),
  accel: scalar(activation?.accel),
  faceMove: !!activation?.faceMove,
  formWhileSquidHeld: activationActor.form,
  characterVisible: activationActor.character.visible,
  runnerResetCountAfterStart: runnerResets,
  ownedObjects: activationSystem.owned.size,
  startEvents: events.map(cleanEvent),
};
activationSystem.clear();

// Real Actor movement through enemy ink: capture acceleration, steady speed, facing, ink damage and source flags.
Math.random = seededRandom(0x4b52414b);
events.length = 0;
const moveSystem = newSystem([], { groundSample: 2 });
const mover = makeActor('kraken-mover');
activate(mover, moveSystem);
mover.intent.move.set(1, 0, 0);
const moveSamples = [];
for (let frame = 1; frame <= 60; frame++) {
  G.time = frame * dt;
  mover.update(dt);
  if ([1, 5, 15, 30, 60].includes(frame)) moveSamples.push({ frame, pos: vec(mover.pos), vel: vec(mover.vel), yaw: scalar(mover.yaw), onEnemy: mover.onEnemy, groundTeam: mover.groundTeam, hp: scalar(mover.hp), damageFromInk: scalar(mover.damageFromInk) });
}
const movementFixture = {
  samples: moveSamples,
  enemyInkSample: 2,
  endEvents: events.map(cleanEvent),
};
moveSystem.clear();

function simulateAttack({ airborne = false, withTargets = false } = {}) {
  Math.random = seededRandom(0x4b52414b);
  events.length = 0;
  let owner = makeActor(airborne ? 'kraken-airdrop' : 'kraken-ground-attack', 0, { y: airborne ? 3 : 0, grounded: !airborne });
  const system = newSystem([owner], { groundSample: 2 });
  activate(owner, system);
  if (airborne) owner.vel.y = 0;
  else owner.grounded = true;
  owner.intent.fire = true;
  G.time += dt;
  owner.update(dt);
  const afterTrigger = { pos: vec(owner.pos), vel: vec(owner.vel), grounded: owner.grounded, attack: !!owner.specialActive?.attack, cooldown: scalar(owner.specialActive?.cd), active: owner.specialActive?.id ?? null };
  owner.intent.fire = false;

  if (withTargets) {
    // Deterministic replay gets victims at the expected impact center; the first run below supplies that center.
    const center = withTargets.center;
    const close = makeActor('kraken-victim-close', 1, { x: center[0], y: 0, z: center[2] });
    const edge = makeActor('kraken-victim-edge', 1, { x: center[0] + 2.0, y: 0, z: center[2] });
    const blocked = makeActor('kraken-victim-blocked', 1, { x: center[0] + 0.45, y: 0, z: center[2] });
    const outside = makeActor('kraken-victim-outside', 1, { x: center[0] + 4.0, y: 0, z: center[2] });
    const ally = makeActor('kraken-ally', 0, { x: center[0], y: 0, z: center[2] });
    const dead = makeActor('kraken-dead-victim', 1, { x: center[0], y: 0, z: center[2] }); dead.alive = false;
    G.actors.push(close, edge, blocked, outside, ally, dead);
    blockedLosX = blocked.pos.x;
    G.physics.los = (_from, to) => Math.abs(to.x - blockedLosX) > 0.15;
  }

  let frames = 1;
  let slamEvent = null;
  while (owner.specialActive && frames < 180 && !slamEvent) {
    frames++;
    G.time = frames * dt;
    owner.update(dt);
    slamEvent = events.find((entry) => entry.name === 'special:slam') || null;
  }
  const victims = G.actors.filter((actor) => actor !== owner).map((actor) => ({ name: actor.name, alive: actor.alive, hp: scalar(actor.hp) }));
  const result = {
    trigger: afterTrigger,
    framesToImpact: frames,
    landing: slamEvent ? cleanEvent(slamEvent) : null,
    finalPos: vec(owner.pos),
    remainingAttack: !!owner.specialActive?.attack,
    hitCalls: [...hitCalls],
    victims,
    damageAreas: [...areaCalls],
    paintCalls: [...paintCalls],
    events: events.map(cleanEvent),
  };
  system.clear();
  return result;
}

// First capture actual impact position, then run the same trajectory with target/LOS cases around it.
const plainGroundAttack = simulateAttack();
const groundCenter = plainGroundAttack.landing?.pos ?? plainGroundAttack.finalPos;
const groundAttack = simulateAttack({ withTargets: { center: groundCenter } });
const airDrop = simulateAttack({ airborne: true });

// A normal jump is still owned by Actor; the Kraken tick only raises the first airborne frame to hopVel.
Math.random = seededRandom(0x4b52414b);
events.length = 0;
const hopSystem = newSystem();
const hopper = makeActor('kraken-hop', 0, { y: 1, grounded: false });
hopper.vel.y = 4;
activate(hopper, hopSystem);
hopper.intent.jump = true;
G.time = dt; hopper.update(dt);
hopper.intent.jump = false;
const hopFixture = {
  pos: vec(hopper.pos), vel: vec(hopper.vel), grounded: hopper.grounded,
  specialAir: !!hopper.specialActive?.air, attack: !!hopper.specialActive?.attack,
  jumpEvents: events.map(cleanEvent),
};
hopSystem.clear();

// Trail paint and ground movement use the real Actor controller plus Kraken tick.
Math.random = seededRandom(0x4b52414b);
events.length = 0;
const trailSystem = newSystem([], { groundSample: 2 });
const trailOwner = makeActor('kraken-trail');
activate(trailOwner, trailSystem);
trailOwner.intent.move.set(0, 0, 1);
for (let frame = 1; frame <= 90; frame++) { G.time = frame * dt; trailOwner.update(dt); }
const trailFixture = {
  elapsed: scalar(90 * dt), pos: vec(trailOwner.pos), vel: vec(trailOwner.vel),
  onEnemy: trailOwner.onEnemy, hp: scalar(trailOwner.hp), damageFromInk: scalar(trailOwner.damageFromInk),
  paintCalls: [...paintCalls],
};
trailSystem.clear();

// Kraken negates damage and converts it into a bounded shove; Actor.damage handles invulnerability first.
Math.random = seededRandom(0x4b52414b);
events.length = 0;
const damageSystem = newSystem();
const protectedActor = makeActor('kraken-damage-target');
const attacker = makeActor('kraken-attacker', 1, { x: -1, grounded: false });
activate(protectedActor, damageSystem);
const beforeDamage = { hp: scalar(protectedActor.hp), pos: vec(protectedActor.pos), vel: vec(protectedActor.vel), grounded: protectedActor.grounded };
protectedActor.damage(100, attacker, 'fixture-hit');
const damageFilter = {
  before: beforeDamage,
  afterDamage: { hp: scalar(protectedActor.hp), vel: vec(protectedActor.vel), grounded: protectedActor.grounded, hurtFlash: scalar(protectedActor.hurtFlash) },
  expectedKnock: scalar(Math.min(4, 100 * SPECIALS.kraken.knockPerDamage)),
  invulnerability: null,
};
const invulnerable = makeActor('kraken-invulnerable-target');
activate(invulnerable, damageSystem);
invulnerable.invuln = 0.5;
invulnerable.damage(100, attacker, 'fixture-hit');
damageFilter.invulnerability = { hp: scalar(invulnerable.hp), vel: vec(invulnerable.vel), grounded: invulnerable.grounded, hurtFlash: scalar(invulnerable.hurtFlash) };
damageSystem.clear();

// Exact expiry is driven by one source Actor update per 60 Hz frame, including end animation/object removal.
Math.random = seededRandom(0x4b52414b);
events.length = 0;
const expirySystem = newSystem();
const expiring = makeActor('kraken-expiry');
activate(expiring, expirySystem);
const activeBeforeExpiry = expiring.specialActive;
let expiryFrames = 0;
while (expiring.specialActive && expiryFrames < 500) {
  expiryFrames++;
  G.time = expiryFrames * dt;
  expiring.update(dt);
}
const expiryFixture = {
  frames: expiryFrames, elapsed: scalar(expiryFrames * dt), duration: scalar(SPECIALS.kraken.duration),
  activeAfter: !!expiring.specialActive, lastSourceElapsed: scalar(activeBeforeExpiry?.t),
  visible: expiring.character.visible, ownedObjectsAfter: expirySystem.owned.size,
  events: events.map(cleanEvent),
};
expirySystem.clear();

// Owner death follows onSplat -> Kraken.end; Actor.splat then leaves the Character hidden.
Math.random = seededRandom(0x4b52414b);
events.length = 0;
const deathSystem = newSystem();
const dying = makeActor('kraken-owner-death');
activate(dying, deathSystem);
const objectsBeforeDeath = deathSystem.owned.size;
dying.splat(null, 'fixture-death');
const ownerDeath = {
  objectsBeforeDeath, objectsAfter: deathSystem.owned.size,
  activeAfter: !!dying.specialActive, visibleAfter: dying.character.visible,
  visibilityCalls: [...dying.character.visibility], events: events.map(cleanEvent),
};
deathSystem.clear();

const fixture = {
  sourceBehavior: 'actual Actor / SpecialSystem execution with headless physics, Character, effects, and paint sinks',
  config: Object.fromEntries(['duration', 'speed', 'hopVel', 'attackVel', 'attackFwd', 'radius', 'damage', 'knockPerDamage', 'cooldown', 'paintRadius'].map((key) => [key, scalar(SPECIALS.kraken[key])])),
  activation: activationFixture,
  movement: movementFixture,
  hop: hopFixture,
  groundAttack,
  airDrop,
  trail: trailFixture,
  damageFilter,
  expiry: expiryFixture,
  ownerDeath,
};

const outputJson = JSON.stringify({ sources: sourcePaths.map((path, i) => ({ path, sha256: hashes[i] })), kraken: fixture }, null, 2) + '\n';
const fields = ['duration', 'speed', 'hopVel', 'attackVel', 'attackFwd', 'radius', 'damage', 'knockPerDamage', 'cooldown', 'paintRadius'];
const header = [
  '// Generated by tools/export-kraken-source-fixtures.mjs. Do not edit by hand.',
  '#pragma once', '#include <array>', '#include <string_view>', 'namespace restoration::test {',
  'struct KrakenSourceFixture { double duration, speed, hopVelocity, attackVelocity, attackForward, radius, damage, knockPerDamage, cooldown, paintRadius; int impactFrames, dropImpactFrames, expiryFrames, trailPaintCalls, acceptedDamageCalls; double acceptedDamageTotal; };',
  `inline constexpr KrakenSourceFixture krakenSourceFixture{${[
    ...fields.map((key) => fixture.config[key]),
    groundAttack.framesToImpact, airDrop.framesToImpact, expiryFixture.frames,
    trailFixture.paintCalls.length, groundAttack.hitCalls.length,
    groundAttack.hitCalls.reduce((sum, call) => sum + call.amount, 0),
  ].map((value, index) => index >= 10 && index <= 14 ? String(value) : Number(value).toPrecision(17)).join(',')}};`,
  `inline constexpr std::array<std::string_view, ${sourcePaths.length}> krakenSourceHashes{{${hashes.map((hash) => `"${hash}"`).join(',')}}};`,
  'struct KrakenMotion { int frame; std::array<double, 3> pos, vel; double yaw; };',
  `inline constexpr std::array<KrakenMotion, ${moveSamples.length}> krakenMotion{{${moveSamples.map(s => `{${s.frame},{${s.pos.join(',')}},{${s.vel.join(',')}},${s.yaw}}`).join(',')}}};`,
  'struct KrakenAttack { std::array<double, 3> triggerPos, triggerVel, finalPos; int frames; };',
  ...[['krakenGroundAttack', groundAttack], ['krakenAirDrop', airDrop]].map(([name, s]) => `inline constexpr KrakenAttack ${name}{{${s.trigger.pos.join(',')}},{${s.trigger.vel.join(',')}},{${s.finalPos.join(',')}},${s.framesToImpact}};`),
  `inline constexpr std::array<double, 3> krakenHopPos{${hopFixture.pos.join(',')}};`,
  `inline constexpr std::array<double, 3> krakenHopVel{${hopFixture.vel.join(',')}};`,
  '}', '',
].join('\n');
const headerPath = new URL('../tests/kraken_source_fixtures.hpp', import.meta.url);
const jsonPath = new URL('../tests/kraken_source_fixtures.json', import.meta.url);
if (process.argv.includes('--check')) {
  if (readFileSync(headerPath, 'utf8') !== header) throw new Error('Kraken C++ source fixture differs');
  if (readFileSync(jsonPath, 'utf8') !== outputJson) throw new Error('Kraken JSON source fixture differs');
} else {
  writeFileSync(headerPath, header);
  writeFileSync(jsonPath, outputJson);
}
unlisten.forEach((off) => off());
Math.random = nativeRandom;
console.log(`Locked actual Kraken Actor/SpecialSystem behavior; ${sourcePaths.length} SHA-256 source stamps`);
