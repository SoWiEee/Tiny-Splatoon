// Actual-source behavioral oracle for the seven remaining world specials.
// It drives Actor, SpecialSystem, and their owned objects with headless render and physics sinks.
import { createHash } from 'node:crypto';
import { readFileSync, writeFileSync } from 'node:fs';
import { register } from 'node:module';
register(new URL('../tools/reference-loader.mjs', import.meta.url));

const THREE = await import('three');
const { G, on } = await import('../splat-ref/src/core/ctx.js');
const { Actor } = await import('../splat-ref/src/game/actor.js');
const { SpecialSystem } = await import('../splat-ref/src/game/specials.js');
const { Projectiles } = await import('../splat-ref/src/game/weapons.js');
const { getSpecialProp } = await import('../splat-ref/src/game/special-props.js');
const { SPECIALS } = await import('../splat-ref/src/config.js');
const sourcePaths = ['../splat-ref/src/config.js', '../splat-ref/src/game/actor.js', '../splat-ref/src/game/specials.js', '../splat-ref/src/game/physics.js', '../splat-ref/src/game/weapons.js', '../splat-ref/src/game/special-props.js'];
const sourceTexts = sourcePaths.map((path) => readFileSync(new URL(path, import.meta.url), 'utf8'));
const hashes = sourceTexts.map((source) => createHash('sha256').update(source).digest('hex'));

// Three UUID creation is render-only. Keep it off the seeded gameplay stream.
let uuid = 0;
THREE.MathUtils.generateUUID = () => `00000000-0000-4000-8000-${String(++uuid).padStart(12, '0')}`;
const nativeRandom = Math.random;
function seededRandom(seed) {
  return () => {
    seed |= 0; seed = seed + 0x6d2b79f5 | 0;
    let t = Math.imul(seed ^ seed >>> 15, 1 | seed);
    t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t;
    return ((t ^ t >>> 14) >>> 0) / 4294967296;
  };
}
const scalar = (x) => Number(Number(x || 0).toPrecision(15));
const vec = (v) => [scalar(v.x), scalar(v.y), scalar(v.z)];
const dt = 1 / 60;
const scene = { add() {}, remove() {} };
const crabProp = getSpecialProp('crab');
if (!crabProp) throw new Error('Could not build the source Crab Rig prop');

// ReturnMarker only needs a 2D canvas for its map badge; no drawing enters gameplay state.
const canvasContext = new Proxy({}, { get: (target, key) => target[key] ?? (() => {}) });
globalThis.document ??= { createElement: () => ({ width: 0, height: 0, getContext: () => canvasContext }) };
globalThis.Image ??= class { set src(_value) {} };

const events = [];
const eventNames = ['special:use', 'special:start', 'special:end', 'special:launch', 'special:strike', 'bomb:explode', 'actor:cheer', 'hit', 'superjump'];
const unlisten = eventNames.map((name) => on(name, (event) => events.push({ name, event })));
const roundEvent = (entry) => {
  const e = entry.event;
  return { name: entry.name, id: e.id ?? null, reason: e.reason ?? null, team: e.team ?? e.actor?.team ?? null,
    victimTeam: e.victim?.team ?? null, pos: e.pos ? vec(e.pos) : null, to: e.to ? vec(e.to) : null,
    radius: e.radius == null ? null : scalar(e.radius), damage: e.damage == null ? null : scalar(e.damage),
    helped: e.helped ?? null, phase: e.phase ?? null, home: e.home ?? null };
};

class HeadlessCharacter {
  constructor({ weapon }) {
    this.root = { position: new THREE.Vector3(), rotation: { y: 0 } };
    this.weapon = { pivot: { visible: true } }; this.left = { pivot: { visible: true } };
    this.weaponId = weapon; this.triggers = []; this.weaponHidden = false; this.visible = true;
  }
  setSub(id) { this.subId = id; }
  setWeapon(id) { this.weaponId = id; }
  trigger(name) { this.triggers.push(name); }
  setVisible(value) { this.visible = value; }
  setHurt() {}
  update() {}
  getMuzzle(out) { return out.copy(this.actor.pos).add(new THREE.Vector3(0, 1.05, 0)); }
  getHeadPosition(out) { return out.copy(this.actor.pos).add(new THREE.Vector3(0, 1.5, 0)); }
  aimReady() { return 1; }
}

let rayMode = 'ground', rayTarget = null, blockedLos = null, paintCount = 0, areaCalls = [], hitCalls = [], customShots = [];
function makeEnvironment(actors = []) {
  G.teamColors = [new THREE.Color('#ff8a14'), new THREE.Color('#2f5bff')];
  G.teamHex = ['#ff8a14', '#2f5bff']; G.time = 0; G.mode = 'match';
  G.audio = null; G.fx = null; G.input = null; G.camera = null; G.rig = null; G.match = { playing: () => true };
  G.nav = null; G.boss = null; G.netm = null;
  G.level = { bounds: { minX: -30, maxX: 30, minZ: -30, maxZ: 30 }, spawnPads: [new THREE.Vector3(-20, 0, -20), new THREE.Vector3(20, 0, -20)], spawnBarrier: 0,
    hasRails: false, blocks: [], groundHeight: () => 0, pointInside: () => false, queryBlocks: () => [] };
  G.physics = {
    groundProbe(x, _y, z, _up, _down, _radius, hit) {
      if (!hit) return { hit: true, y: 0, normal: new THREE.Vector3(0, 1, 0), face: -1, u: 0, v: 0 };
      hit.hit = true; hit.y = 0; hit.normal.set(0, 1, 0); hit.face = -1; hit.u = hit.v = 0; hit.block = -1; hit.center = false; hit.grate = false;
      return hit;
    },
    collideBody() { return { wall: false, ceiling: false, wallNormal: new THREE.Vector3() }; },
    raycast(from, dir, _length, hit) {
      if (rayMode === 'zip' && dir.y > -0.9 && rayTarget) return { hit: true, point: rayTarget.point.clone(), normal: rayTarget.normal.clone(), dist: from.distanceTo(rayTarget.point), face: 0, u: 0, v: 0 };
      if (dir.y < -0.9) return { hit: true, point: new THREE.Vector3(from.x, 0, from.z), normal: new THREE.Vector3(0, 1, 0), dist: Math.max(0, from.y), face: -1, u: 0, v: 0 };
      if (hit) { hit.hit = false; return hit; }
      return { hit: false, point: new THREE.Vector3(), normal: new THREE.Vector3(0, 1, 0), dist: _length };
    },
    segment(from, to) {
      if (from.y > 0 && to.y <= 0) {
        const k = from.y / (from.y - to.y);
        return { hit: true, point: from.clone().lerp(to, k).setY(0), normal: new THREE.Vector3(0, 1, 0) };
      }
      return { hit: false, point: to.clone(), normal: new THREE.Vector3(0, 1, 0) };
    },
    los(_from, to) { return !blockedLos || to.distanceTo(blockedLos) > 0.15; },
  };
  G.paint = { sample: () => 0, splat() { paintCount++; return 0; } };
  G.subs = { blockActor() {}, damageArea(pos, radius, damage, team) {
    areaCalls.push({ pos: vec(pos), radius: scalar(radius), damage: scalar(damage), team });
    G.specials?.areaHit(pos, radius, damage, team);
  } };
  G.actors = actors;
  const projectiles = Object.create(Projectiles.prototype);
  projectiles.scene = scene; projectiles.list = []; projectiles.pool = []; projectiles.bombs = [];
  projectiles.fireCustom = (owner, from, dir, options) => { customShots.push({ owner: owner.name, from: vec(from), dir: vec(dir), ...options }); };
  projectiles.applyHit = function (owner, victim, amount, weaponId) {
    if (!victim.alive || victim.team === owner.team) return;
    hitCalls.push({ owner: owner.name, victim: victim.name, amount: scalar(amount), weaponId });
    Projectiles.prototype.applyHit.call(this, owner, victim, amount, weaponId);
  };
  G.projectiles = projectiles;
}
function actor(id, team = 0, name = id, pos = [0, 0, 0]) {
  const a = new Actor({ team, name, weapon: 'shooter', special: id, isLocal: true, isBot: false, CharacterClass: HeadlessCharacter });
  a.character.actor = a; a.pos.fromArray(pos); a.grounded = true; a.groundTeam = 0; a.lastFire = 0; a.hp = 5000;
  a.ground.hit = true; a.ground.y = 0; a.ground.normal.set(0, 1, 0); a.ground.face = -1;
  a.aimPoint.set(pos[0], pos[1] + 1.05, pos[2] + 18); return a;
}
function system(actors) { makeEnvironment(actors); const s = new SpecialSystem(scene); G.specials = s; return s; }
function start(a, s) { a.special = a.specialCost(); a.intent.special = true; G.time += dt; a.update(dt); a.intent.special = false; return a.specialActive; }
function step(a, s, input = {}) { Object.assign(a.intent, { fire: false, jump: false, squid: false, sub: false, move: new THREE.Vector3() }, input); G.time += dt; a.update(dt); s.update(dt); }
function snapshotObject(w) {
  return { kind: w.kind, phase: w.phase ?? null, pos: w.pos ? vec(w.pos) : null, vel: w.vel ? vec(w.vel) : null,
    to: w.to ? vec(w.to) : null, from: w.from ? vec(w.from) : null, radius: w.radius ?? w.r ?? null,
    charge: w.charge ?? null, held: w.held ?? null, life: w.life ?? null, age: w.age ?? w.t ?? null,
    timer: w.timer ?? w.dur ?? null, damagePerSecond: w.damagePerSecond ?? null };
}
function actorTrace(a) { return { active: a.specialActive?.id ?? null, body: !!a.specialActive?.body, pos: vec(a.pos), vel: vec(a.vel), ink: scalar(a.ink), special: scalar(a.special), hp: scalar(a.hp) }; }
function resetTrace() { events.length = 0; paintCount = 0; areaCalls = []; hitCalls = []; customShots = []; blockedLos = null; rayMode = 'ground'; rayTarget = null; }
function setSeed(seed) { Math.random = seededRandom(seed); }

// Strike: source map cursor, strict confirm gate, missile flight, then one full-damage vortex tick.
resetTrace(); setSeed(0x53545249);
const strikeOwner = actor('strike', 0, 'strike-owner', [-8, 0, -8]);
const strikeEnemy = actor('strike', 1, 'strike-enemy', [0, 0, 0]);
const strikeAlly = actor('strike', 0, 'strike-ally', [0, 0, 0]);
const strikeOutside = actor('strike', 1, 'strike-outside', [7, 0, 0]);
strikeOutside.alive = true;
const strikeSystem = system([strikeOwner, strikeEnemy, strikeAlly, strikeOutside]);
start(strikeOwner, strikeSystem);
const minimap = { w: 100, h: 100, toCanvas(x, z, out) { out.x = x + 50; out.y = z + 50; return out; }, _worldX: (x) => x - 50, _worldZ: (y) => y - 50 };
strikeSystem.aimMove(strikeOwner, 8, -8, minimap);
const strikeTarget = vec(strikeOwner.specialActive.target);
for (let i = 0; i < 15; i++) step(strikeOwner, strikeSystem);
const strikeBoundary = { elapsed: scalar(strikeOwner.specialActive.t), confirmRejected: (strikeSystem.aimConfirm(strikeOwner), !strikeOwner.specialActive.confirm) };
step(strikeOwner, strikeSystem);
const strikeAfterBoundary = scalar(strikeOwner.specialActive.t);
strikeSystem.aimConfirm(strikeOwner);
let strikeWait = 0;
while (strikeOwner.specialActive && strikeWait++ < 3) step(strikeOwner, strikeSystem);
let strikeMissileFrames = 0;
while (strikeSystem.world.some((w) => w.kind === 'missile') && strikeMissileFrames < 200) { step(strikeOwner, strikeSystem); strikeMissileFrames++; }
const strikeTornado = strikeSystem.world.find((w) => w.kind === 'tornado');
const strikeBeforeTick = strikeTornado ? snapshotObject(strikeTornado) : null;
if (strikeTornado) step(strikeOwner, strikeSystem);
const strikeDamage = { enemy: scalar(5000 - strikeEnemy.hp), ally: scalar(5000 - strikeAlly.hp), outside: scalar(5000 - strikeOutside.hp), calls: [...hitCalls] };
let strikeCleanupFrames = 0;
while (strikeSystem.world.length && strikeCleanupFrames < 400) { step(strikeOwner, strikeSystem); strikeCleanupFrames++; }
const strike = { aimTime: SPECIALS.strike.aimTime, flight: SPECIALS.strike.flight, radius: SPECIALS.strike.radius, duration: SPECIALS.strike.duration, dps: SPECIALS.strike.dps, pull: SPECIALS.strike.pull,
  target: strikeTarget, boundary: strikeBoundary, elapsedAfterBoundary: strikeAfterBoundary,
  confirmWaitFrames: strikeWait, missileFrames: strikeMissileFrames, tornado: strikeBeforeTick, firstTick: strikeDamage, cleanupFrames: strikeCleanupFrames,
  events: events.map(roundEvent), objectsAfter: strikeSystem.world.length };
strikeSystem.clear();

// Blower: inflate and release a real held Bubble, then charge it with friendly fire until it blasts.
resetTrace(); setSeed(0x424c4f57);
const blowerOwner = actor('blower', 0, 'blower-owner');
const blowerSystem = system([blowerOwner]); start(blowerOwner, blowerSystem);
for (let i = 0; i < 60; i++) step(blowerOwner, blowerSystem, { fire: true });
step(blowerOwner, blowerSystem, { fire: false });
const bubble = blowerSystem.world.find((w) => w.kind === 'bubble');
const bubbleBeforeHit = bubble ? snapshotObject(bubble) : null;
const blowerEnemy = actor('blower', 1, 'blower-enemy', [bubble?.pos.x ?? 0, 0, bubble?.pos.z ?? 0]);
const blowerAlly = actor('blower', 0, 'blower-ally', [bubble?.pos.x ?? 0, 0, bubble?.pos.z ?? 0]);
const blowerOutside = actor('blower', 1, 'blower-outside', [(bubble?.pos.x ?? 0) + SPECIALS.blower.rMax * SPECIALS.blower.blastMul + 2, 0, bubble?.pos.z ?? 0]);
G.actors.push(blowerEnemy, blowerAlly, blowerOutside);
if (bubble) blowerSystem.rayHit(bubble.pos.clone().add(new THREE.Vector3(0, 0, -6)), new THREE.Vector3(0, 0, 1), 12, 0, SPECIALS.blower.popDamage, blowerOwner);
const blowerDamage = { enemy: scalar(5000 - blowerEnemy.hp), ally: scalar(5000 - blowerAlly.hp), outside: scalar(5000 - blowerOutside.hp), calls: [...hitCalls], areas: [...areaCalls] };
blowerSystem.update(dt);
const blower = { duration: SPECIALS.blower.duration, max: SPECIALS.blower.max, inflate: SPECIALS.blower.inflate, rMin: SPECIALS.blower.rMin, rMax: SPECIALS.blower.rMax,
  popDamage: SPECIALS.blower.popDamage, blastMul: SPECIALS.blower.blastMul, damageMax: SPECIALS.blower.damageMax, damageMin: SPECIALS.blower.damageMin,
  released: bubbleBeforeHit, damage: blowerDamage, objectsAfterHit: blowerSystem.world.length, owner: actorTrace(blowerOwner), events: events.map(roundEvent) };
blowerSystem.clear();

// Two nearby bubbles chain through the source areaHit hook; the 100-HP target dies in the first blast.
resetTrace(); setSeed(0x424c4348);
const chainOwner = actor('blower', 0, 'blower-chain-owner');
const chainSystem = system([chainOwner]); start(chainOwner, chainSystem);
for (let i = 0; i < 60; i++) step(chainOwner, chainSystem, { fire: true });
step(chainOwner, chainSystem, { fire: false });
for (let i = 0; i < 60; i++) step(chainOwner, chainSystem, { fire: true });
step(chainOwner, chainSystem, { fire: false });
const chainBubbles = chainSystem.world.filter((w) => w.kind === 'bubble' && !w.dead);
const chainFirst = chainBubbles[0] ?? null, chainSecond = chainBubbles[1] ?? null;
const chainTarget = actor('blower', 1, 'blower-chain-victim', [chainFirst?.pos.x ?? 0, 0, chainFirst?.pos.z ?? 0]);
chainTarget.hp = 100;
const chainOutside = actor('blower', 1, 'blower-chain-outside', [(chainFirst?.pos.x ?? 0) + SPECIALS.blower.rMax * SPECIALS.blower.blastMul + 2, 0, chainFirst?.pos.z ?? 0]);
G.actors.push(chainTarget, chainOutside);
if (chainFirst) chainSystem.rayHit(chainFirst.pos.clone().add(new THREE.Vector3(0, 0, 6)), new THREE.Vector3(0, 0, -1), 12, 0, SPECIALS.blower.popDamage, chainOwner);
const chainExplosionCount = events.filter((e) => e.name === 'bomb:explode').length;
const chainTargetDamage = scalar(100 - chainTarget.hp);
const chainOutsideDamage = scalar(5000 - chainOutside.hp);
chainSystem.update(dt);
blower.chain = { first: chainFirst ? vec(chainFirst.pos) : [0, 0, 0], second: chainSecond ? vec(chainSecond.pos) : [0, 0, 0],
  targetDamage: chainTargetDamage, outsideDamage: chainOutsideDamage, targetAliveAfter: chainTarget.alive,
  explosions: chainExplosionCount, objectsAfter: chainSystem.world.filter((w) => w.kind === 'bubble').length };
chainSystem.clear();

// Jetpack: fixed-step body motion, source boost edge, first custom shot, timed return-home super jump.
resetTrace(); setSeed(0x4a455450);
const jetOwner = actor('jetpack', 0, 'jetpack-owner', [0, 0, 0]);
const jetSystem = system([jetOwner]); start(jetOwner, jetSystem);
const jetOrigin = vec(jetOwner.pos); let jetFirstShotFrame = 0, jetBoost = null, jetAt60 = null;
for (let frame = 1; frame <= 60; frame++) {
  const jump = frame === 22;
  step(jetOwner, jetSystem, { move: new THREE.Vector3(frame <= 40 ? 1 : 0, 0, 0), fire: frame <= 60, jump });
  if (!jetFirstShotFrame && customShots.length) jetFirstShotFrame = frame;
  if (frame === 22) jetBoost = { pos: vec(jetOwner.pos), vel: vec(jetOwner.vel) };
  if (frame === 60) jetAt60 = { pos: vec(jetOwner.pos), vel: vec(jetOwner.vel) };
}
let jetExpiryFrame = 60;
while (jetOwner.specialActive && jetExpiryFrame < 500) { step(jetOwner, jetSystem); jetExpiryFrame++; }
const jetMarker = jetSystem.world.find((w) => w.kind === 'return');
const jet = { duration: SPECIALS.jetpack.duration, height: SPECIALS.jetpack.height, speed: SPECIALS.jetpack.speed, accel: SPECIALS.jetpack.accel,
  boostValue: SPECIALS.jetpack.boost, boostGap: SPECIALS.jetpack.boostGap, interval: SPECIALS.jetpack.interval, projSpeed: SPECIALS.jetpack.projSpeed,
  range: SPECIALS.jetpack.range, directDamage: SPECIALS.jetpack.directDamage, splashRadius: SPECIALS.jetpack.splashRadius,
  origin: jetOrigin, firstShotFrame: jetFirstShotFrame, firstShot: customShots[0] ?? null, boostMotion: jetBoost, at60: jetAt60,
  expiryFrame: jetExpiryFrame, activeAfter: !!jetOwner.specialActive, superJump: jetOwner.superJumpState ? { home: jetOwner.superJumpState.home, to: vec(jetOwner.superJumpState.target), phase: jetOwner.superJumpState.phase } : null,
  markerReturning: !!jetMarker?.back, events: events.map(roundEvent) };
jetSystem.clear();

// Stamp: source delayed grounded smash, directional guard, and sub-triggered throw cleanup.
resetTrace(); setSeed(0x5354414d);
const stampOwner = actor('stamp', 0, 'stamp-owner');
stampOwner.hp = 100;
const stampEnemy = actor('stamp', 1, 'stamp-enemy', [0, 0, 1.6]);
const stampAlly = actor('stamp', 0, 'stamp-ally', [0, 0, 1.6]);
const stampFront = actor('stamp', 1, 'stamp-front', [0, 0, 2]);
const stampBack = actor('stamp', 1, 'stamp-back', [0, 0, -2]);
const stampSystem = system([stampOwner, stampEnemy, stampAlly, stampFront, stampBack]); start(stampOwner, stampSystem);
let stampSlamFrame = 0;
for (let frame = 1; frame <= 30 && !stampSlamFrame; frame++) {
  step(stampOwner, stampSystem, { fire: true });
  if (events.some((e) => e.name === 'hit' && e.event.victim === stampEnemy)) stampSlamFrame = frame;
}
const stampDamage = { enemy: scalar(5000 - stampEnemy.hp), ally: scalar(5000 - stampAlly.hp), calls: [...hitCalls] };
const stampHpBeforeGuard = stampOwner.hp;
stampOwner.damage(20, stampFront, 'fixture'); const stampHpAfterFront = stampOwner.hp;
stampOwner.damage(20, stampBack, 'fixture');
const stampGuard = { front: scalar(stampHpBeforeGuard - stampHpAfterFront), back: scalar(stampHpAfterFront - stampOwner.hp), hp: scalar(stampOwner.hp) };
const stampThrower = actor('stamp', 0, 'stamp-thrower');
const stampThrowSystem = system([stampThrower]); start(stampThrower, stampThrowSystem);
step(stampThrower, stampThrowSystem, { sub: true });
const thrownStamp = stampThrowSystem.world.find((w) => w.kind === 'stamp');
let stampThrowImpactFrames = 0;
while (stampThrowSystem.world.includes(thrownStamp) && stampThrowImpactFrames < 60) {
  step(stampThrower, stampThrowSystem); stampThrowImpactFrames++;
}
const stampThrowObject = thrownStamp ? snapshotObject(thrownStamp) : null;
const stampThrowBlast = areaCalls.at(-1) ?? null;
const stampThrowReason = events.map(roundEvent).findLast((e) => e.name === 'special:end')?.reason ?? null;
stampThrowSystem.clear();

// Replay at the source-measured ground impact so the positive and negative area samples are concrete.
resetTrace(); setSeed(0x5354414d);
const stampThrowerWithTargets = actor('stamp', 0, 'stamp-thrower');
const throwCenter = stampThrowBlast?.pos ?? [0, 0.3, 0];
const stampThrowVictim = actor('stamp', 1, 'stamp-throw-victim', [throwCenter[0], 0, throwCenter[2]]);
const stampThrowOutside = actor('stamp', 1, 'stamp-throw-outside', [throwCenter[0] + SPECIALS.stamp.throwRadius + 2, 0, throwCenter[2]]);
const stampThrowTargetSystem = system([stampThrowerWithTargets, stampThrowVictim, stampThrowOutside]);
start(stampThrowerWithTargets, stampThrowTargetSystem);
step(stampThrowerWithTargets, stampThrowTargetSystem, { sub: true });
let stampThrowTargetFrames = 0;
while (stampThrowTargetSystem.world.some((w) => w.kind === 'stamp') && stampThrowTargetFrames < 60) {
  step(stampThrowerWithTargets, stampThrowTargetSystem); stampThrowTargetFrames++;
}
const stamp = { duration: SPECIALS.stamp.duration, interval: SPECIALS.stamp.interval, reach: SPECIALS.stamp.reach, radius: SPECIALS.stamp.radius,
  damage: SPECIALS.stamp.damage, guardTime: SPECIALS.stamp.guardTime, deflectArc: SPECIALS.stamp.deflectArc,
  slamFrame: stampSlamFrame, damageResult: { ...stampDamage, front: scalar(5000 - stampFront.hp) }, guard: stampGuard, throwActiveAfter: !!stampThrower.specialActive,
  throwReason: stampThrowReason, throwObject: stampThrowObject, thrownSpeed: thrownStamp ? scalar(thrownStamp.vel.length()) : 0,
  throwImpactFrames: stampThrowImpactFrames, throwTargetFrames: stampThrowTargetFrames,
  throwLanding: stampThrowBlast?.pos ?? [0, 0.3, 0], throwBlastRadius: stampThrowBlast?.radius ?? 0,
  throwVictimDamage: scalar(5000 - stampThrowVictim.hp), throwOutsideDamage: scalar(5000 - stampThrowOutside.hp) };
stampSystem.clear(); stampThrowTargetSystem.clear();

// Booyah: a teammate cheer adds charge and gauge, then a full orb lands and blasts its source area.
resetTrace(); setSeed(0x424f4f59);
const booyahOwner = actor('booyah', 0, 'booyah-owner');
const cheerer = actor('booyah', 0, 'cheerer', [2, 0, 0]);
cheerer.special = 0;
const booyahSystem = system([booyahOwner, cheerer]); start(booyahOwner, booyahSystem);
booyahSystem.cheer(cheerer); const afterCheer = { charge: scalar(booyahOwner.specialActive.charge), helper: scalar(cheerer.special) };
booyahSystem.cheer(cheerer); const afterRepeat = { charge: scalar(booyahOwner.specialActive.charge), helper: scalar(cheerer.special), events: events.filter((e) => e.name === 'actor:cheer').length };
let booyahChargedFrame = 0;
for (let frame = 1; frame <= 300 && !booyahChargedFrame; frame++) { step(booyahOwner, booyahSystem); if (booyahOwner.specialActive.charge >= 1) booyahChargedFrame = frame; }
for (let i = 0; i < 5; i++) step(booyahOwner, booyahSystem);
step(booyahOwner, booyahSystem, { fire: true });
const orb = booyahSystem.world.find((w) => w.kind === 'orb');
let orbFlightFrames = 0;
while (orb && orb.phase === 'fly' && orbFlightFrames < 300) { step(booyahOwner, booyahSystem); orbFlightFrames++; }
const orbLanding = orb ? { phase: orb.phase, pos: vec(orb.pos), age: scalar(orb.t) } : null;
const orbEnemy = actor('booyah', 1, 'booyah-enemy', [orb?.pos.x ?? 0, 0, orb?.pos.z ?? 0]);
const orbAlly = actor('booyah', 0, 'booyah-ally', [orb?.pos.x ?? 0, 0, orb?.pos.z ?? 0]);
const orbOutside = actor('booyah', 1, 'booyah-outside', [(orb?.pos.x ?? 0) + SPECIALS.booyah.radius + 2, 0, orb?.pos.z ?? 0]);
G.actors.push(orbEnemy, orbAlly, orbOutside);
let orbFuseFrames = 0;
while (orb && booyahSystem.world.includes(orb) && orb.phase === 'fuse' && orbFuseFrames < 160) { step(booyahOwner, booyahSystem); orbFuseFrames++; }
const booyah = { charge: SPECIALS.booyah.charge, cheer: SPECIALS.booyah.cheer, cheerSpecial: SPECIALS.booyah.cheerSpecial,
  radius: SPECIALS.booyah.radius, killRadius: SPECIALS.booyah.killRadius, damageMax: SPECIALS.booyah.damageMax, damageMin: SPECIALS.booyah.damageMin,
  afterCheer, afterRepeat, chargedFrame: booyahChargedFrame, ownerActiveAfterThrow: !!booyahOwner.specialActive,
  orbFlightFrames, landing: orbLanding, fuseFrames: orbFuseFrames,
  damage: { enemy: scalar(5000 - orbEnemy.hp), ally: scalar(5000 - orbAlly.hp), outside: scalar(5000 - orbOutside.hp), calls: [...hitCalls] },
  objectsAfterBlast: booyahSystem.world.filter((w) => w.kind === 'orb').length, events: events.map(roundEvent) };
booyahSystem.clear();

// Zipcaster: controlled wall hit, body zip to its source-offset target, then return-home at duration expiry.
resetTrace(); setSeed(0x5a495043);
rayMode = 'zip'; rayTarget = { point: new THREE.Vector3(7, 2, 0), normal: new THREE.Vector3(-1, 0, 0) };
const zipOwner = actor('zipcaster', 0, 'zip-owner');
const zipEnemy = actor('zipcaster', 1, 'zip-enemy', [6.54, 0, 1.5]);
const zipSystem = system([zipOwner, zipEnemy]); start(zipOwner, zipSystem);
zipOwner.aimPoint.set(10, 4, 0);
step(zipOwner, zipSystem, { sub: true });
const zipTarget = zipOwner.specialActive?.zip?.to ? vec(zipOwner.specialActive.zip.to) : null;
let zipTravelFrames = 0;
while (zipOwner.specialActive?.body && zipTravelFrames < 120) { step(zipOwner, zipSystem); zipTravelFrames++; }
const zipAtImpact = { pos: vec(zipOwner.pos), body: !!zipOwner.specialActive?.body, hang: scalar(zipOwner.specialActive?.hang ?? 0), enemyDamage: scalar(5000 - zipEnemy.hp) };
let zipExpiryFrame = 1 + zipTravelFrames;
while (zipOwner.specialActive && zipExpiryFrame < 600) { step(zipOwner, zipSystem); zipExpiryFrame++; }
const zipMarker = zipSystem.world.find((w) => w.kind === 'return');
const zip = { duration: SPECIALS.zipcaster.duration, range: SPECIALS.zipcaster.range, speed: SPECIALS.zipcaster.speed, hangDuration: SPECIALS.zipcaster.hang,
  cooldown: SPECIALS.zipcaster.cooldown, impactDirect: SPECIALS.zipcaster.impactDirect, impactSplash: SPECIALS.zipcaster.impactSplash, impactRadius: SPECIALS.zipcaster.impactRadius,
  origin: [0, 0, 0], target: zipTarget, travelFrames: zipTravelFrames, atImpact: zipAtImpact, expiryFrame: zipExpiryFrame,
  activeAfter: !!zipOwner.specialActive, homeJump: zipOwner.superJumpState ? { home: zipOwner.superJumpState.home, target: vec(zipOwner.superJumpState.target), phase: zipOwner.superJumpState.phase } : null,
  markerReturning: !!zipMarker?.back, events: events.map(roundEvent) };
zipSystem.clear();

// Crab: source hull/armor, front absorption, rear rider damage, roll armor, gun shot and teardown.
resetTrace(); setSeed(0x43524142);
const crabOwner = actor('crab', 0, 'crab-owner'); crabOwner.hp = 100;
const crabFront = actor('crab', 1, 'crab-front', [0, 0, 3]);
const crabBack = actor('crab', 1, 'crab-back', [0, 0, -3]);
const crabSystem = system([crabOwner, crabFront, crabBack]); start(crabOwner, crabSystem);
const crabInitial = { hp: scalar(crabOwner.specialActive.hp), body: !!crabOwner.specialActive.body, inkProof: true, noSquid: !!crabOwner.specialActive.noSquid,
  hitRadius: scalar(crabOwner.hitR), hitHeight: scalar(crabOwner.hitH), weaponHidden: crabOwner.character.weaponHidden };
const crabFrontDamage = scalar(crabOwner.damage(100, crabFront, 'fixture'));
const crabAfterFront = { hp: scalar(crabOwner.specialActive.hp), rider: scalar(crabOwner.hp) };
const crabRiderBefore = crabOwner.hp; crabOwner.damage(40, crabBack, 'fixture'); const crabBackDamage = scalar(crabRiderBefore - crabOwner.hp);
const crabAfterBack = { hp: scalar(crabOwner.specialActive.hp), rider: scalar(crabOwner.hp) };
step(crabOwner, crabSystem, { squid: true });
const crabRoll = { roll: !!crabOwner.specialActive?.roll, speed: scalar(crabOwner.specialActive?.speed ?? 0) };
const crabRollDamage = scalar(crabOwner.damage(100, crabFront, 'fixture'));
const crabAfterRollHit = { hp: scalar(crabOwner.specialActive.hp), rider: scalar(crabOwner.hp) };
step(crabOwner, crabSystem, { squid: false, fire: true });
const crabShot = customShots[0] ?? null;
let crabExpiryFrame = 2;
while (crabOwner.specialActive && crabExpiryFrame < 600) { step(crabOwner, crabSystem); crabExpiryFrame++; }
const crab = { duration: SPECIALS.crab.duration, hp: SPECIALS.crab.hp, rollSpeed: SPECIALS.crab.rollSpeed, rollArmor: SPECIALS.crab.rollArmor,
  gunInterval: SPECIALS.crab.gunInterval, gunDamage: SPECIALS.crab.gunDamage, gunSpeed: SPECIALS.crab.gunSpeed,
  initial: crabInitial, prop: { gunMuzzle: vec(crabProp.gunMuzzle), cannonMuzzle: vec(crabProp.cannonMuzzle), seatAt: vec(crabProp.seatAt) },
  frontDamage: crabFrontDamage, afterFront: crabAfterFront, backDamage: crabBackDamage, afterBack: crabAfterBack,
  roll: crabRoll, rollDamage: crabRollDamage, afterRollHit: crabAfterRollHit, gunShot: crabShot,
  expiryFrame: crabExpiryFrame, activeAfter: !!crabOwner.specialActive, weaponHiddenAfter: crabOwner.character.weaponHidden,
  hitRadiusAfter: crabOwner.hitR ?? null, events: events.map(roundEvent) };
crabSystem.clear();

const fixture = { sourceBehavior: 'actual Actor / SpecialSystem / special-object execution with controlled headless physics and render-only canvas stubs',
  strikes: strike, blower, jetpack: jet, stamp, booyah, zipcaster: zip, crab };
const outputJson = JSON.stringify({ sources: sourcePaths.map((path, i) => ({ path, sha256: hashes[i] })), specials: fixture }, null, 2) + '\n';
const header = [
  '// Generated by tools/export-remaining-specials-source-fixtures.mjs. Do not edit by hand.', '#pragma once', '#include <array>', '#include <string_view>', 'namespace restoration::test {',
  'struct StrikeSourceFixture { double aimTime, flight, radius, duration, dps, pull; int boundaryFrames, confirmRejected, missileFrames, cleanupFrames; double enemyTickDamage, allyTickDamage, targetX, targetZ; };',
  `inline constexpr StrikeSourceFixture strikeSourceFixture{${[strike.aimTime, strike.flight, strike.radius, strike.duration, strike.dps, strike.pull, 15, strike.boundary.confirmRejected ? 1 : 0, strike.missileFrames, strike.cleanupFrames, strike.firstTick.enemy, strike.firstTick.ally, strike.target[0], strike.target[2]].map((v, i) => i >= 6 && i <= 9 ? String(v) : Number(v).toPrecision(17)).join(',')}};`,
  'struct BlowerSourceFixture { double duration, inflate, minRadius, maxRadius, popDamage, blastMul, damageMax, damageMin; int maxBubbles, released, removedAfterHit; double enemyDamage, allyDamage, outsideDamage, blastRadius, bubbleX, bubbleY, bubbleZ; int chainExplosions, chainObjectsAfter; double chainFirstX, chainFirstY, chainFirstZ, chainSecondX, chainSecondY, chainSecondZ, chainTargetDamage, chainOutsideDamage; int chainTargetAliveAfter; };',
  `inline constexpr BlowerSourceFixture blowerSourceFixture{${[blower.duration, blower.inflate, blower.rMin, blower.rMax, blower.popDamage, blower.blastMul, blower.damageMax, blower.damageMin, blower.max, 1, blower.objectsAfterHit === 0 ? 1 : 0, blower.damage.enemy, blower.damage.ally, blower.damage.outside, (blower.released?.radius ?? 0) * blower.blastMul, ...(blower.released?.pos ?? [0,0,0]), blower.chain.explosions, blower.chain.objectsAfter, ...blower.chain.first, ...blower.chain.second, blower.chain.targetDamage, blower.chain.outsideDamage, blower.chain.targetAliveAfter ? 1 : 0].map((v, i) => [8,9,10,18,19,28].includes(i) ? String(v) : Number(v).toPrecision(17)).join(',')}};`,
  'struct JetpackSourceFixture { double duration, height, speed, accel, boost, boostGap, interval, projectileSpeed, range, directDamage, splashRadius; int firstShotFrame, expiryFrame, returnedHome; double originX, originY, originZ, firstHorizontalSpeed, boostVelocityY, at60X, at60Y, at60Z; };',
  `inline constexpr JetpackSourceFixture jetpackSourceFixture{${[jet.duration, jet.height, jet.speed, jet.accel, jet.boostValue, jet.boostGap, jet.interval, jet.projSpeed, jet.range, jet.directDamage, jet.splashRadius, jet.firstShotFrame, jet.expiryFrame, jet.superJump?.home ? 1 : 0, ...jet.origin, Math.hypot(jet.at60.vel[0], jet.at60.vel[2]), jet.boostMotion.vel[1], ...jet.at60.pos].map((v, i) => [11,12,13].includes(i) ? String(v) : Number(v).toPrecision(17)).join(',')}};`,
  'struct StampSourceFixture { double duration, interval, reach, radius, damage, guardTime, deflectArc, throwSpeed; int slamFrame, throwEnded, frontBlocked, backAllowed, throwImpactFrames, throwTargetFrames; double enemyDamage, allyDamage, slamFrontDamage, guardFrontDamage, guardBackDamage, throwVictimDamage, throwOutsideDamage, throwLandingX, throwLandingY, throwLandingZ, throwBlastRadius; };',
  `inline constexpr StampSourceFixture stampSourceFixture{${[stamp.duration, stamp.interval, stamp.reach, stamp.radius, stamp.damage, stamp.guardTime, stamp.deflectArc, stamp.thrownSpeed, stamp.slamFrame, stamp.throwActiveAfter ? 0 : 1, stamp.guard.front === 0 ? 1 : 0, stamp.guard.back > 0 ? 1 : 0, stamp.throwImpactFrames, stamp.throwTargetFrames, stamp.damageResult.enemy, stamp.damageResult.ally, stamp.damageResult.front, stamp.guard.front, stamp.guard.back, stamp.throwVictimDamage, stamp.throwOutsideDamage, ...stamp.throwLanding, stamp.throwBlastRadius].map((v, i) => i >= 8 && i <= 13 ? String(v) : Number(v).toPrecision(17)).join(',')}};`,
  'struct BooyahSourceFixture { double charge, cheer, cheerSpecial, radius, killRadius, damageMax, damageMin; int chargedFrame, repeatedCheerIgnored, orbLandingFrames, fuseFrames; double helperGauge, enemyDamage, allyDamage, outsideDamage, landingX, landingY, landingZ; };',
  `inline constexpr BooyahSourceFixture booyahSourceFixture{${[booyah.charge, booyah.cheer, booyah.cheerSpecial, booyah.radius, booyah.killRadius, booyah.damageMax, booyah.damageMin, booyah.chargedFrame, booyah.afterRepeat.events === 1 ? 1 : 0, booyah.orbFlightFrames, booyah.fuseFrames, booyah.afterCheer.helper, booyah.damage.enemy, booyah.damage.ally, booyah.damage.outside, ...booyah.landing.pos].map((v, i) => i >= 7 && i <= 10 ? String(v) : Number(v).toPrecision(17)).join(',')}};`,
  'struct ZipcasterSourceFixture { double duration, range, speed, hang, cooldown, impactDirect, impactSplash, impactRadius; int travelFrames, expiryFrame, returnedHome, enemyHit; double targetX, targetY, targetZ; };',
  `inline constexpr ZipcasterSourceFixture zipcasterSourceFixture{${[zip.duration, zip.range, zip.speed, zip.hangDuration, zip.cooldown, zip.impactDirect, zip.impactSplash, zip.impactRadius, zip.travelFrames, zip.expiryFrame, zip.homeJump?.home ? 1 : 0, zip.atImpact.enemyDamage > 0 ? 1 : 0, ...(zip.target || [0,0,0])].map((v, i) => i >= 8 && i <= 11 ? String(v) : Number(v).toPrecision(17)).join(',')}};`,
  'struct CrabSourceFixture { double duration, hp, rollSpeed, rollArmor, gunInterval, gunDamage, gunSpeed; int frontAbsorbed, rearDamagedRider, rollAbsorbed, weaponRestored, expiryFrames; double hpAfterFront, riderDamageFromRear, hpAfterRollHit, riderHpAfter, gunMuzzleX, gunMuzzleY, gunMuzzleZ, cannonMuzzleX, cannonMuzzleY, cannonMuzzleZ, seatY; };',
  `inline constexpr CrabSourceFixture crabSourceFixture{${[crab.duration, crab.hp, crab.rollSpeed, crab.rollArmor, crab.gunInterval, crab.gunDamage, crab.gunSpeed, crab.frontDamage === 0 ? 1 : 0, crab.backDamage > 0 ? 1 : 0, crab.rollDamage === 0 ? 1 : 0, crab.weaponHiddenAfter ? 0 : 1, crab.expiryFrame, crab.afterFront.hp, crab.backDamage, crab.afterRollHit.hp, crab.afterRollHit.rider, ...crab.prop.gunMuzzle, ...crab.prop.cannonMuzzle, crab.prop.seatAt[1]].map((v, i) => i >= 7 && i <= 11 ? String(v) : Number(v).toPrecision(17)).join(',')}};`,
  `inline constexpr std::array<std::string_view, ${sourcePaths.length}> remainingSpecialSourceHashes{{${hashes.map((h) => `"${h}"`).join(',')}}};`, '}', '',
].join('\n');
const headerPath = new URL('../tests/fixtures/remaining_specials_source_fixtures.hpp', import.meta.url);
const jsonPath = new URL('../tests/fixtures/remaining_specials_source_fixtures.json', import.meta.url);
if (process.argv.includes('--check')) {
  if (readFileSync(headerPath, 'utf8') !== header) throw new Error('Remaining special C++ source fixture differs');
  if (readFileSync(jsonPath, 'utf8') !== outputJson) throw new Error('Remaining special JSON source fixture differs');
} else {
  writeFileSync(headerPath, header); writeFileSync(jsonPath, outputJson);
}
strikeSystem.clear(); blowerSystem.clear(); jetSystem.clear(); stampSystem.clear(); stampThrowSystem.clear(); booyahSystem.clear(); zipSystem.clear(); crabSystem.clear();
unlisten.forEach((off) => off()); Math.random = nativeRandom;
console.log(`Locked seven actual Actor/SpecialSystem behaviors; ${sourcePaths.length} SHA-256 source stamps`);
