// Source behavioral oracle for native WorldSpecials. This intentionally calls the locked JS actor/special methods;
// it is not a tuning-only extraction. `--check` detects source or generated-fixture drift.
import { createHash } from 'node:crypto';
import { readFileSync, writeFileSync } from 'node:fs';
import { register } from 'node:module';
register(new URL('../tools/reference-loader.mjs', import.meta.url));

const THREE = await import('three');
const { G, on } = await import('../splat-ref/src/core/ctx.js');
const { Actor } = await import('../splat-ref/src/game/actor.js');
const { SpecialSystem } = await import('../splat-ref/src/game/specials.js');
const { Projectiles, WeaponRunner } = await import('../splat-ref/src/game/weapons.js');
const { SubSystem } = await import('../splat-ref/src/game/subs.js');
const { SPECIALS, SUBS, WEAPONS } = await import('../splat-ref/src/config.js');

const sourcePaths = [
  '../splat-ref/src/config.js',
  '../splat-ref/src/game/actor.js',
  '../splat-ref/src/game/specials.js',
  '../splat-ref/src/game/weapons.js',
  '../splat-ref/src/game/subs.js',
];
const sourceTexts = sourcePaths.map((path) => readFileSync(new URL(path, import.meta.url), 'utf8'));
const hashes = sourceTexts.map((source) => createHash('sha256').update(source).digest('hex'));
const seededRandom = (seed) => () => {
  seed |= 0; seed = seed + 0x6d2b79f5 | 0;
  let t = Math.imul(seed ^ seed >>> 15, 1 | seed);
  t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t;
  return ((t ^ t >>> 14) >>> 0) / 4294967296;
};
const random = Math.random;
Math.random = seededRandom(0x51ec1a1);
G.teamColors = [new THREE.Color('#ff8a14'), new THREE.Color('#2f5bff')];
G.teamHex = ['#ff8a14', '#2f5bff'];
G.time = 0;
G.audio = null; G.fx = null; G.input = null; G.camera = { position: new THREE.Vector3() };
const events = [];
const unlisten = ['special:use', 'special:slam', 'storm:start', 'special:shield', 'special:start', 'weapon:fire', 'sub:use', 'bomb:throw']
  .map((name) => on(name, (event) => events.push({ name, event })));

const vec = (v) => [v.x, v.y, v.z];
const n = (v) => Number(v).toPrecision(17);
const actorShell = (id, team = 0) => {
  const actor = {
    specialId: id, special: 240, specialCost: () => 240, stats: { specials: 0 }, ink: 12,
    form: 'squid', climbing: false, team, teamHex: G.teamHex[team], color: G.teamColors[team], slot: team,
    pos: new THREE.Vector3(0, 1, 0), vel: new THREE.Vector3(0, 0, 0), grounded: true, alive: true,
    aimYaw: 0.3, aimPitch: 0.1, aimDir: new THREE.Vector3(Math.sin(0.3), 0, Math.cos(0.3)),
    aimPoint: new THREE.Vector3(0, 0, 12), smoothY: 0, yaw: 0.3, isLocal: true, isBot: false,
    intent: { move: new THREE.Vector3(), jump: false, squid: false, fire: false, sub: false, special: false },
    status: { shield: 0, reveal: 0, revealTeam: -1, track: 0, poison: 0 },
    character: {
      trigger() {}, setWeapon(id) { this.weapon = id; }, setSub(id) { this.sub = id; },
      getMuzzle(out) { return out.copy(this.actor.pos).add(new THREE.Vector3(0, 1.05, 0)).addScaledVector(this.actor.aimDir, 0.3); },
      aimReady() { return 1; },
    },
    weaponId: 'shooter', weapon: { kind: 'shooter' },
    weaponRunner: { endDodge() {}, reset() {} },
    _setClimb() {}, turfNoSpecial: 0, addTurfNoSpecial(area) { this.turfNoSpecial += area; }, _nearCamera() { return false; }, hurtFlash: 0,
    _slamImpact(...args) { return Actor.prototype._slamImpact.call(this, ...args); },
    setSub(id) { this.sub = id; }, damage(amount, attacker, source) { this.damageRecord = { amount, source, attacker }; },
    _resolve(_stick, prevY) {
      if (this.vel.y <= 0 && prevY > 0 && this.pos.y <= 0) { this.pos.y = 0; this.grounded = true; }
      else this.grounded = false;
    },
  };
  actor.character.actor = actor;
  return actor;
};

// Slam and Storm use Actor's actual source activation/body implementation. The shell only supplies its engine hooks.
const slam = actorShell('slam');
const hits = [];
G.actors = [slam, Object.assign(actorShell('slam', 1), { pos: new THREE.Vector3(1, 0, 0) })];
G.paint = { splat() { return 0.1; } };
G.physics = { los() { return true; }, raycast() { return { hit: true, point: new THREE.Vector3(0, 0, 0), normal: new THREE.Vector3(0, 1, 0) }; } };
const projectileHarness = Object.create(Projectiles.prototype);
Object.assign(projectileHarness, {
  scene: { add() {}, remove() {} }, bombGeo: new THREE.SphereGeometry(0.2, 4, 3), cloudGeo: new THREE.SphereGeometry(1, 4, 3),
  bombCapGeo: new THREE.ConeGeometry(0.12, 0.24, 5),
  bombMatCache: new Map(), bombs: [], clouds: [],
  _bombMat() { return new THREE.MeshStandardMaterial({ color: 0xffffff }); },
});
G.projectiles = projectileHarness;
projectileHarness.applyHit = (_owner, victim, damage, source) => hits.push({ damage, source, victimTeam: victim.team });
Actor.prototype._startSpecial.call(slam);
const slamArmorDuringSpecial = !!slam.specialActive?.armor;
let slamTicks = 0;
for (let frame = 1; frame <= 180 && slam.specialActive; ++frame) {
  slamTicks = frame;
  G.time = frame / 60;
  Actor.prototype._updateSpecial.call(slam, 1 / 60);
}
const slamFixture = {
  id: 'slam', activeAfterImpact: !!slam.specialActive,
  impact: (() => { const e = events.findLast((x) => x.name === 'special:slam')?.event; return e && { pos: vec(e.pos), radius: e.radius }; })(),
  hit: hits[0] || null, ticksUntilImpact: slamTicks,
  armorDuringSpecial: slamArmorDuringSpecial,
};

const storm = actorShell('storm');
storm.pos.set(2, 3, -4); storm.vel.set(1, 0, -0.5);
Actor.prototype._startSpecial.call(storm);
const stormBomb = projectileHarness.bombs.at(-1);
Actor.prototype._updateSpecial.call(storm, 0.36);
const stormEventStart = events.length;
projectileHarness._spawnCloud(stormBomb);
const cloud = projectileHarness.clouds.at(-1);
const stormCloudAtSpawn = vec(cloud.group.position);
const stormVictim = Object.assign(actorShell('storm', 1), { pos: new THREE.Vector3(2, 0, -4), remote: false });
G.actors = [storm, stormVictim];
projectileHarness._updateClouds(1 / 60);
const stormFixture = {
  id: 'storm', activeAfterThrow: !!storm.specialActive,
  bomb: { pos: vec(stormBomb.pos), vel: vec(stormBomb.vel), dir: vec(stormBomb.dir) },
  cloud: { pos: stormCloudAtSpawn, afterOneTick: vec(cloud.group.position), duration: cloud.dur, radius: SPECIALS.storm.radius, driftSpeed: SPECIALS.storm.driftSpeed, dps: SPECIALS.storm.dps },
  rainDamage: stormVictim.damageRecord?.amount ?? 0, specialPaintArea: storm.turfNoSpecial,
  event: (() => { const e = events.slice(stormEventStart).find((x) => x.name === 'storm:start')?.event; return e && { pos: vec(e.pos), team: e.team, radius: e.radius }; })(),
};

// Public SpecialSystem entry points exercise the actual Bubbler and Zooka source behavior.
const scene = { add() {}, remove() {} };
const system = new SpecialSystem(scene);
const bubbler = actorShell('bubbler');
G.actors = [bubbler];
system.start(bubbler, 'bubbler');
const bubblerAttacker = Object.assign(actorShell('bubbler', 1), { pos: new THREE.Vector3(0, 0, 1) });
const bubblerFiltered = system.filterDamage(bubbler, 36, bubblerAttacker, 'shooter');
const bubblerFixture = {
  id: 'bubbler', active: bubbler.specialActive.id, elapsed: bubbler.specialActive.t,
  duration: bubbler.specialActive.dur, shield: bubbler.status.shield,
  ownerShield: !!bubbler._shieldOwner,
  filteredDamage: bubblerFiltered, knock: vec(bubbler.vel),
  eventTime: events.findLast((e) => e.name === 'special:shield')?.event.time,
};

const zooka = actorShell('zooka');
const zookaVictim = Object.assign(actorShell('zooka', 1), { pos: new THREE.Vector3(0, 0, 1.5) });
zooka.aimYaw = 0; zooka.aimDir.set(0, 0, 1); zooka.aimPoint.set(0, 0, 12);
G.actors = [zooka, zookaVictim];
G.physics = { los() { return true; }, segment() { return { hit: false }; }, raycast() { return { hit: true, point: new THREE.Vector3(0, 0, 0), normal: new THREE.Vector3(0, 1, 0) }; } };
G.projectiles = {
  _muzzle(a, out) { return Projectiles.prototype._muzzle.call(projectileHarness, a, out); },
  _aimFrom(a, from, out) { return Projectiles.prototype._aimFrom.call(projectileHarness, a, from, out); },
  applyHit(_owner, victim, damage, source) { victim.damageRecord = { amount: damage, source }; },
};
system.start(zooka, 'zooka');
const zookaState = zooka.specialActive;
const consumed = system.weapon(zooka, zookaState, 0.25, { fire: true, firePressed: true, sub: false, subReleased: false });
const twister = system.world.at(-1);
twister.update(0.05);
const zookaFixture = {
  id: 'zooka', duration: zookaState.dur, interval: zookaState.def.interval,
  cooldownAfterFirstShot: zookaState.cd, consumed,
  twister: { kind: twister.kind, pos: vec(twister.pos), vel: vec(twister.vel), life: twister.life,
    damage: SPECIALS.zooka.damage, height: SPECIALS.zooka.height, radius: SPECIALS.zooka.radius,
    paintEvery: SPECIALS.zooka.paintEvery, paintRadius: SPECIALS.zooka.paintRadius },
  hitDamage: zookaVictim.damageRecord?.amount ?? 0,
  event: (() => { const e = events.findLast((x) => x.name === 'weapon:fire' && x.event.weapon === 'zooka')?.event; return e && { weapon: e.weapon, muzzle: vec(e.muzzle), dir: vec(e.dir) }; })(),
};

// Barrage throws run the real WeaponRunner release/gap logic and the real SubSystem velocity path.
// The SubSystem's mesh allocation is replaced at its final _throw seam so snapshots stay headless.
G.projectiles = projectileHarness;
const barrageFixtures = ['barrage', 'barrage_sticky', 'barrage_burst', 'barrage_seeker', 'barrage_mist'].map((id) => {
  const def = SPECIALS[id], bomb = SUBS[def.bomb];
  const actor = actorShell(id);
  actor.pos.set(2, 3, -4); actor.vel.set(1, 0, -0.5);
  actor.weapon = WEAPONS.shooter;
  actor.specialId = id;
  actor.special = 240;
  G.specials = system;
  Actor.prototype._startSpecial.call(actor);
  const runner = new WeaponRunner(actor);
  const thrown = [];
  const subSystem = Object.create(SubSystem.prototype);
  subSystem._throw = (_actor, sub, pos, vel) => {
    const item = { kind: sub.kind, pos: vec(pos), vel: vec(vel), fuse: sub.fuse ?? -1,
      inkCost: sub.inkCost, radius: sub.radius ?? 0 };
    thrown.push(item);
    return { gid: thrown.length };
  };
  G.subs = subSystem;
  const beforeBombs = projectileHarness.bombs.length;
  const throwAt = (time) => {
    G.time = time;
    runner.update(1 / 60, { fire: false, sub: true, subReleased: false });
    runner.update(1 / 60, { fire: false, sub: false, subReleased: true });
  };
  throwAt(0);
  const first = bomb.kind === 'bomb' ? projectileHarness.bombs.at(-1) : thrown.at(-1);
  const firstAccepted = bomb.kind === 'bomb' ? projectileHarness.bombs.length === beforeBombs + 1 : thrown.length === 1;
  throwAt(def.gap * 0.5);
  const blockedBeforeGap = bomb.kind === 'bomb' ? projectileHarness.bombs.length === beforeBombs + 1 : thrown.length === 1;
  throwAt(def.gap);
  const acceptedAtGap = bomb.kind === 'bomb' ? projectileHarness.bombs.length === beforeBombs + 2 : thrown.length === 2;
  const throwData = bomb.kind === 'bomb'
    ? { kind: bomb.kind, pos: vec(first.pos), vel: vec(first.vel), fuse: first.fuse,
        inkCost: bomb.inkCost, radius: bomb.radius }
    : first;
  throwData.throwSpeed = bomb.throwSpeed;
  return { id, duration: def.duration, gap: def.gap, bombKind: def.bomb, throw: throwData,
    firstAccepted, blockedBeforeGap, acceptedAtGap,
    acceptedThrows: bomb.kind === 'bomb' ? projectileHarness.bombs.length - beforeBombs : thrown.length,
    inkAfter: actor.ink };
});

const rows = [slamFixture, stormFixture, bubblerFixture, zookaFixture, ...barrageFixtures];
const output = [
  '// Generated by tools/export-specials-source-fixtures.mjs. Do not edit by hand.',
  '#pragma once', '#include <array>', '#include <string_view>', 'namespace restoration::test {',
  'struct SpecialSourceFixture { std::string_view id; double duration, interval, x, y, z, vx, vy, vz, aux0, aux1, aux2; int frames, flag0, flag1, flag2; std::string_view payload = {}; };',
  `inline constexpr std::array<SpecialSourceFixture, ${rows.length}> specialSourceFixtures{{`,
  ...rows.map((r) => {
    const barrage = r.id === 'barrage' || r.id.startsWith('barrage_');
    const dur = r.id === 'slam' ? SPECIALS.slam.rise + SPECIALS.slam.hang : (r.duration ?? r.cloud?.duration ?? SPECIALS[r.id].duration);
    const v = r.throw || r.bomb || r.twister || {};
    const pos = v.pos || r.cloud?.pos || [0, 0, 0], vel = v.vel || [0, 0, 0];
    const aux = r.id === 'slam' ? [SPECIALS.slam.radius, r.hit?.damage ?? 0, SPECIALS.slam.killRadius]
      : r.id === 'storm' ? [r.cloud.radius, r.rainDamage, r.specialPaintArea]
      : r.id === 'bubbler' ? [r.shield, r.filteredDamage, Math.hypot(r.knock[0], r.knock[2])]
      : barrage ? [r.gap, v.throwSpeed, v.radius]
      : [r.twister.life, r.twister.damage, r.twister.paintRadius];
    const frames = r.ticksUntilImpact ?? (barrage ? r.acceptedThrows : 0);
    const flags = r.id === 'slam' ? [r.activeAfterImpact ? 1 : 0, !!r.hit ? 1 : 0]
      : r.id === 'storm' ? [r.activeAfterThrow ? 1 : 0, 1]
      : r.id === 'bubbler' ? [r.ownerShield ? 1 : 0, 1]
      : barrage ? [r.firstAccepted ? 1 : 0, r.blockedBeforeGap ? 1 : 0]
      : [r.consumed ? 1 : 0, 1];
    const flag2 = r.id === 'slam' ? (r.armorDuringSpecial ? 1 : 0)
      : barrage && r.acceptedAtGap && r.inkAfter === 100 ? 1 : 0;
    const nums = [dur, barrage ? r.gap : r.interval ?? SPECIALS[r.id].interval ?? 0, ...pos, ...vel, ...aux].map(n).join(',');
    return `  {"${r.id}",${nums},${frames},${flags.join(',')},${flag2},"${barrage ? r.bombKind : ''}"},`;
  }),
  '}};',
  ...hashes.map((hash, i) => `inline constexpr char specialSourceHash${i}[] = "${hash}";`),
  '}', '',
].join('\n');
const outputJson = JSON.stringify({ sources: sourcePaths.map((path, i) => ({ path, sha256: hashes[i] })), rows }, null, 2) + '\n';
const headerPath = new URL('../tests/fixtures/world_specials_source_fixtures.hpp', import.meta.url);
const jsonPath = new URL('../tests/fixtures/world_specials_source_fixtures.json', import.meta.url);
if (process.argv.includes('--check')) {
  if (readFileSync(headerPath, 'utf8') !== output) throw new Error('Special source C++ fixture differs');
  if (readFileSync(jsonPath, 'utf8') !== outputJson) throw new Error('Special source JSON fixture differs');
} else {
  writeFileSync(headerPath, output);
  writeFileSync(jsonPath, outputJson);
}
unlisten.forEach((off) => off());
Math.random = random;
console.log(`Locked ${rows.length} source special activation/action snapshots; ${sourcePaths.length} SHA-256 source stamps`);
