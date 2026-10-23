// Behavioral oracle for Sonar and Howl Box. This drives the live Actor and SpecialSystem implementations,
// including the actual Speaker world object; it does not make a fixture from config values alone.
import { createHash } from 'node:crypto';
import { readFileSync, writeFileSync } from 'node:fs';
import { register } from 'node:module';
register(new URL('../tools/reference-loader.mjs', import.meta.url));

const THREE = await import('three');
const { G, on } = await import('../splat-ref/src/core/ctx.js');
const { Actor } = await import('../splat-ref/src/game/actor.js');
const { SpecialSystem } = await import('../splat-ref/src/game/specials.js');
const { WeaponRunner } = await import('../splat-ref/src/game/weapons.js');
const { SubSystem } = await import('../splat-ref/src/game/subs.js');
const { PLAYER, SPECIALS, WEAPONS } = await import('../splat-ref/src/config.js');

const sourcePaths = [
  '../splat-ref/src/config.js',
  '../splat-ref/src/game/actor.js',
  '../splat-ref/src/game/specials.js',
  '../splat-ref/src/game/special-props.js',
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
Math.random = seededRandom(0x50a4a11);
const round = (x) => Number(Number(x || 0).toPrecision(15));
const vec = (v) => [round(v.x), round(v.y), round(v.z)];
const scalar = (x) => round(x);

G.teamColors = [new THREE.Color('#ff8a14'), new THREE.Color('#2f5bff')];
G.teamHex = ['#ff8a14', '#2f5bff'];
G.time = 0;
G.mode = 'match';
G.audio = null; G.fx = null; G.input = null; G.camera = null; G.rig = null; G.match = null;
G.level = {
  bounds: { minX: -100, maxX: 100, minZ: -100, maxZ: 100 },
  spawnPads: [new THREE.Vector3(100, 0, 100), new THREE.Vector3(-100, 0, -100)],
  spawnBarrier: 0, hasRails: false, blocks: [],
  groundHeight() { return 0; },
  pointInside() { return false; },
  queryBlocks() { return []; },
};
let raycasts = 0;
G.physics = {
  groundProbe(_x, _y, _z, _up, _down, _radius, hit) {
    if (hit) {
      hit.hit = true; hit.y = 0; hit.normal?.set(0, 1, 0);
      hit.face = -1; hit.u = 0; hit.v = 0; hit.block = -1; hit.center = false; hit.grate = false;
      return hit;
    }
    return { hit: true, y: 0, normal: new THREE.Vector3(0, 1, 0), face: -1, u: 0, v: 0 };
  },
  collideBody() { return { wall: false, ceiling: false, wallNormal: new THREE.Vector3() }; },
  raycast(from, _dir, _distance, _target, _includeGround) {
    raycasts++;
    return { hit: true, point: new THREE.Vector3(from.x, 0, from.z), normal: new THREE.Vector3(0, 1, 0), face: -1, u: 0, v: 0 };
  },
  los() { return true; },
};
G.paint = { sample() { return 0; }, splat() { return 0; } };
G.subs = {
  track(actor, team, time) { return SubSystem.prototype.track.call(this, actor, team, time); },
  blockActor() {},
};
G.projectiles = { applyHit() {} };

const events = [];
const eventNames = ['special:use', 'special:sonar', 'actor:tracked', 'special:start', 'special:end', 'special:wail', 'hit'];
const unlisten = eventNames.map((name) => on(name, (event) => events.push({ name, event })));
const cleanEvent = (entry) => {
  if (!entry) return null;
  const e = entry.event;
  return {
    name: entry.name,
    actorTeam: e.actor?.team ?? e.victim?.team ?? null,
    victimTeam: e.victim?.team ?? null,
    team: e.team ?? null,
    id: e.id ?? null,
    reason: e.reason ?? null,
    pos: e.pos ? vec(e.pos) : null,
    dir: e.dir ? vec(e.dir) : null,
    time: e.time == null ? null : round(e.time),
    damage: e.damage == null ? null : round(e.damage),
    killed: e.killed ?? null,
  };
};

function actorShell(specialId, team = 0, { bot = false, isLocal = false } = {}) {
  const actor = {
    specialId, special: 240, specialCost: () => 240, stats: { specials: 0 }, ink: 100,
    form: 'kid', climbing: false, submerged: false, team, teamHex: G.teamHex[team], color: G.teamColors[team], slot: team,
    pos: new THREE.Vector3(0, 0, 0), vel: new THREE.Vector3(), grounded: true, alive: true,
    aimYaw: 0, aimPitch: 0, aimDir: new THREE.Vector3(0, 0, 1), aimPoint: new THREE.Vector3(0, 0, 12),
    smoothY: 0, yaw: 0, isLocal, isBot: bot, bot,
    intent: { move: new THREE.Vector3(), jump: false, squid: false, fire: false, sub: false, special: false },
    status: { shield: 0, reveal: 0, revealTeam: -1, track: 0, trackTeam: -1, poison: 0 },
    character: {
      weapon: { pivot: { visible: true } }, left: { pivot: { visible: true } }, triggers: [], weaponHidden: false,
      trigger(name) { this.triggers.push(name); }, setWeapon(id) { this.weaponId = id; }, setSub(id) { this.subId = id; },
      setVisible() {}, setHurt() {}, update() {},
      getMuzzle(out) { return out.copy(this.actor.pos).add(new THREE.Vector3(0, 1.05, 0)); },
      aimReady() { return 1; },
    },
    weaponId: 'shooter', weapon: WEAPONS.shooter, sub: { kind: 'bomb' },
    weaponRunner: null, specialActive: null,
    _setClimb() {}, _nearCamera() { return false; },
    damage(amount, attacker, source) {
      this.damageCalls.push({ amount, attackerTeam: attacker?.team ?? null, source });
      this.hp -= amount;
      return this.hp <= 0;
    },
    hp: 5000, damageCalls: [],
    _resolve(_squid, _py, _stick) {
      if (this.grounded) this.vel.y = 0;
      else if (this.pos.y <= 0 && this.vel.y <= 0) { this.pos.y = 0; this.vel.y = 0; this.grounded = true; }
    },
  };
  actor.character.actor = actor;
  actor.weaponRunner = new WeaponRunner(actor);
  return actor;
}

class HeadlessCharacter {
  constructor({ weapon }) {
    this.root = { position: new THREE.Vector3(), rotation: { y: 0 } };
    this.weapon = { pivot: { visible: true } };
    this.left = { pivot: { visible: true } };
    this.weaponId = weapon; this.triggers = []; this.weaponHidden = false;
  }
  setSub(id) { this.subId = id; }
  setWeapon(id) { this.weaponId = id; }
  trigger(name) { this.triggers.push(name); }
  setVisible() {}
  setHurt() {}
  update() {}
}

function headlessActor(team, special = 'sonar') {
  const actor = new Actor({ team, name: `fixture-${team}`, weapon: 'shooter', special, isLocal: false, isBot: false, CharacterClass: HeadlessCharacter });
  actor.pos.set(team ? 0 : 20, 0, team ? 0 : 20);
  actor.grounded = true;
  actor.groundTeam = 0;
  actor.lastFire = 0;
  return actor;
}

// Sonar uses actual Actor._startSpecial -> SpecialSystem.start/body and SubSystem.track.
const sonarSystem = new SpecialSystem({ add() {}, remove() {} });
G.specials = sonarSystem;
const sonarOwner = actorShell('sonar', 0);
sonarOwner.pos.set(4, 0, -3); sonarOwner.vel.set(2.5, 0, -1.25);
const sonarEnemyA = actorShell('sonar', 1); sonarEnemyA.status.revealTeam = -1;
const sonarEnemyB = actorShell('sonar', 1); sonarEnemyB.pos.set(9, 0, 8);
const sonarDeadEnemy = actorShell('sonar', 1); sonarDeadEnemy.alive = false; sonarDeadEnemy.pos.set(-7, 0, 2);
const sonarAlly = actorShell('sonar', 0); sonarAlly.pos.set(10, 0, 10);
G.actors = [sonarOwner, sonarEnemyA, sonarEnemyB, sonarDeadEnemy, sonarAlly];
events.length = 0;
Actor.prototype._startSpecial.call(sonarOwner);
const sonarState = sonarOwner.specialActive;
const sonarStartEvents = events.map(cleanEvent);
const sonarInitial = {
  active: sonarState?.id ?? null,
  body: !!sonarState?.body,
  elapsed: scalar(sonarState?.t),
  duration: scalar(sonarState?.dur),
  ownerPos: vec(sonarOwner.pos),
  ownerVel: vec(sonarOwner.vel),
  enemies: [sonarEnemyA, sonarEnemyB, sonarDeadEnemy].map((actor) => ({
    alive: actor.alive, reveal: scalar(actor.status.reveal), revealTeam: actor.status.revealTeam,
    track: scalar(actor.status.track), trackTeam: actor.status.trackTeam,
  })),
  ally: { reveal: scalar(sonarAlly.status.reveal), track: scalar(sonarAlly.status.track) },
  events: sonarStartEvents,
};
let sonarFrames = 0;
for (let frame = 1; frame <= 60 && sonarOwner.specialActive; ++frame) {
  sonarFrames = frame;
  G.time = frame / 60;
  Actor.prototype._updateSpecial.call(sonarOwner, 1 / 60);
}
const sonarBody = {
  frames: sonarFrames,
  activeAfterCast: !!sonarOwner.specialActive,
  elapsed: scalar(sonarState.t),
  ownerPos: vec(sonarOwner.pos), ownerVel: vec(sonarOwner.vel),
  revealAfterCast: scalar(sonarEnemyA.status.reveal),
  endEvent: cleanEvent(events.findLast((entry) => entry.name === 'special:end')),
};

// Invoke the real Actor horizontal controller until steady speed with and without a source-applied reveal.
const measuredSpeed = (revealed) => {
  const actor = actorShell('sonar', 1);
  actor.status.reveal = revealed ? SPECIALS.sonar.duration : 0;
  actor.intent.move.set(0, 0, 1);
  for (let i = 0; i < 360; ++i) Actor.prototype._horizontal.call(actor, 1 / 60, false, false);
  return Math.hypot(actor.vel.x, actor.vel.z);
};
const sonarSpeed = { normal: scalar(measuredSpeed(false)), revealed: scalar(measuredSpeed(true)) };

// Real Actor.update plus the real WeaponRunner captures the extra revealed-ink burn after one shooter shot.
G.specials = { move() {}, weapon() { return false; }, tick() {} };
G.subs = { blockActor() {}, track(actor, team, time) { return SubSystem.prototype.track.call(this, actor, team, time); } };
G.physics = {
  groundProbe(_x, _y, _z, _up, _down, _radius, hit) {
    hit.hit = true; hit.y = 0; hit.normal.set(0, 1, 0); hit.face = -1; hit.u = 0; hit.v = 0; hit.block = -1; hit.center = false; hit.grate = false;
    return hit;
  },
  collideBody() { return { wall: false, ceiling: false, wallNormal: new THREE.Vector3() }; },
  raycast(from) { return { hit: false, point: new THREE.Vector3(from.x, 0, from.z), normal: new THREE.Vector3(0, 1, 0), face: -1, u: 0, v: 0 }; },
};
G.level.spawnPads = [new THREE.Vector3(100, 0, 100), new THREE.Vector3(-100, 0, -100)];
G.level.spawnBarrier = 0;
G.projectiles = { fireShooter() {} };
G.actors = [];
const inkActor = headlessActor(1);
inkActor.ink = 100;
inkActor.status.reveal = SPECIALS.sonar.duration;
inkActor.status.revealTeam = 0;
inkActor.intent.fire = true;
let inkAfterRunner = 100;
const runnerUpdate = WeaponRunner.prototype.update;
WeaponRunner.prototype.update = function (...args) {
  const result = runnerUpdate.apply(this, args);
  inkAfterRunner = this.a.ink;
  return result;
};
inkActor.update(1 / 60);
WeaponRunner.prototype.update = runnerUpdate;
const sonarInk = {
  before: 100,
  afterRunner: scalar(inkAfterRunner),
  afterActorUpdate: scalar(inkActor.ink),
  oneShotCost: scalar(WEAPONS.shooter.inkPerShot),
  observedMultiplier: scalar((100 - inkActor.ink) / (100 - inkAfterRunner)),
};

// Reveal and tracking timers use the real Actor update expiration path.
const revealExpiryActor = headlessActor(1);
revealExpiryActor.status.reveal = SPECIALS.sonar.duration;
revealExpiryActor.status.revealTeam = 0;
revealExpiryActor.status.track = SPECIALS.sonar.duration;
revealExpiryActor.status.trackTeam = 0;
revealExpiryActor.intent.fire = false;
for (let frame = 0; frame < 486; ++frame) revealExpiryActor.update(1 / 60);
const sonarExpiry = { seconds: 486 / 60, reveal: scalar(revealExpiryActor.status.reveal), track: scalar(revealExpiryActor.status.track) };

const sonarFixture = {
  id: 'sonar', sourceEvents: sonarStartEvents, initial: sonarInitial, body: sonarBody,
  speed: sonarSpeed, ink: sonarInk, expiry: sonarExpiry,
  config: { cast: scalar(SPECIALS.sonar.cast), duration: scalar(SPECIALS.sonar.duration), slow: scalar(SPECIALS.sonar.slow), inkMul: scalar(SPECIALS.sonar.inkMul) },
};

function wailScenario({ bot = false, placement = 'press', fixedStep = false } = {}) {
  const system = new SpecialSystem({ add() {}, remove() {} });
  G.specials = system;
  const owner = actorShell('wail', 0, { bot });
  owner.pos.set(0, 0, 0); owner.aimYaw = 0.2; owner.aimPitch = 0.8;
  owner.weaponHidden = false;
  const target = actorShell('wail', 1); target.pos.set(0, 0, 20); target.hp = 5000;
  const offAxis = actorShell('wail', 1); offAxis.pos.set(2.0, 0, 20); offAxis.hp = 5000;
  const ally = actorShell('wail', 0); ally.pos.set(0, 0, 20); ally.hp = 5000;
  G.actors = [owner, target, offAxis, ally];
  events.length = 0;
  raycasts = 0;
  G.physics.raycast = (from) => {
    raycasts++;
    return { hit: true, point: new THREE.Vector3(from.x, 0, from.z), normal: new THREE.Vector3(0, 1, 0), face: -1, u: 0, v: 0 };
  };
  const randomDraws = [];
  const nextRandom = Math.random;
  if (bot) Math.random = () => { const value = nextRandom(); randomDraws.push(value); return value; };
  Actor.prototype._startSpecial.call(owner);
  if (bot) Math.random = nextRandom;
  const held = owner.specialActive;
  const autoT = held.autoT == null ? null : scalar(held.autoT);
  const activeOnStart = !!owner.specialActive;
  const holdSpeed = scalar(held.speed);
  const weaponHiddenOnStart = owner.character.weaponHidden;
  const dt = 1 / 60;
  let frames = 0;
  let thresholdRejected = null;
  let consumed = false;
  if (placement === 'press') {
    system.tick(owner, held, 0.2);
    const beforeThreshold = system.weapon(owner, held, dt, { fire: true, firePressed: true });
    thresholdRejected = owner.specialActive === held && beforeThreshold;
    system.tick(owner, held, dt);
    consumed = system.weapon(owner, held, dt, { fire: true, firePressed: true });
    frames = 13;
  } else {
    const limit = placement === 'bot' ? 120 : 361;
    while (owner.specialActive === held && frames < limit) {
      consumed = system.weapon(owner, held, dt, { fire: false, firePressed: false });
      if (owner.specialActive !== held) break;
      system.tick(owner, held, dt);
      frames++;
    }
    // Actor.update calls weapon before tick; the final check happens at the elapsed source threshold.
    if (owner.specialActive === held) consumed = system.weapon(owner, held, dt, { fire: false, firePressed: false });
  }
  const ended = owner.specialActive == null;
  const botUniform = bot ? randomDraws.find((value) => Math.abs(0.45 + value * 0.5 - autoT) < 1e-14) : null;
  const speaker = system.world.find((item) => item.kind === 'speaker');
  if (speaker) {
    const targetCenter = speaker.mouth.clone().addScaledVector(speaker.dir, 18);
    target.pos.copy(targetCenter).add(new THREE.Vector3(0, -0.8, 0));
    offAxis.pos.copy(target.pos).add(new THREE.Vector3(2.0, 0, 0));
    ally.pos.copy(target.pos);
  }
  const spawnEvent = cleanEvent(events.findLast((entry) => entry.name === 'special:wail'));
  const placedAt = scalar(held.t);
  const placementFixture = {
    bot: !!bot, placement, activeOnStart, holdSpeed, weaponHiddenOnStart, autoT, botUniform,
    thresholdRejected, frames, placedAt, ended, consumed,
    weaponHiddenAfterPlace: owner.character.weaponHidden,
    speaker: speaker ? { kind: speaker.kind, phase: speaker.phase, pos: vec(speaker.pos), mouth: vec(speaker.mouth), dir: vec(speaker.dir), range: scalar(speaker.range), radius: scalar(speaker.radius) } : null,
    event: spawnEvent,
  };
  if (!speaker) {
    system.clear();
    return { placement: placementFixture, world: null };
  }
  if (fixedStep) {
    let chargeFrames = 0, blastFrames = 0, fadeFrames = 0;
    while (speaker.phase === 'charge' && chargeFrames < 300) { speaker.update(dt); chargeFrames++; }
    while (speaker.phase === 'blast' && blastFrames < 600) { speaker.update(dt); blastFrames++; }
    const steppedLineDamage = scalar(5000 - target.hp);
    let finalAlive = true;
    while (fadeFrames < 120 && finalAlive) { finalAlive = speaker.update(dt); fadeFrames++; }
    return {
      placement: placementFixture,
      world: { chargeFrames, blastFrames, fadeFrames, steppedLineDamage, finalAlive, finalPhase: speaker.phase },
    };
  }
  const chargeAccepted = speaker.update(SPECIALS.wail.charge);
  const afterCharge = { alive: chargeAccepted, phase: speaker.phase, time: scalar(speaker.t) };
  const raycastsBeforeBlast = raycasts;
  const blastAccepted = speaker.update(SPECIALS.wail.blast);
  const afterBlast = {
    alive: blastAccepted, phase: speaker.phase, time: scalar(speaker.t),
    lineDamage: scalar(5000 - target.hp), offAxisDamage: scalar(5000 - offAxis.hp),
    allyDamage: scalar(5000 - ally.hp), raycastsDuringBlast: raycasts - raycastsBeforeBlast,
    damageCalls: target.damageCalls.map((call) => ({ amount: scalar(call.amount), source: call.source })),
  };
  const fadeAccepted = speaker.update(SPECIALS.wail.fade);
  const afterFade = { alive: fadeAccepted, phase: speaker.phase, time: scalar(speaker.t) };
  system.clear();
  return { placement: placementFixture, world: { afterCharge, afterBlast, afterFade } };
}

const wailPress = wailScenario({ placement: 'press' });
const wailTimeout = wailScenario({ placement: 'timeout' });
const wailBot = wailScenario({ bot: true, placement: 'bot' });
const wailFixedStep = wailScenario({ placement: 'press', fixedStep: true });
const wailFixture = {
  id: 'wail', press: wailPress, timeout: wailTimeout, bot: wailBot, fixedStep: wailFixedStep,
  config: {
    holdSpeed: scalar(SPECIALS.wail.holdSpeed), holdTime: scalar(SPECIALS.wail.holdTime),
    charge: scalar(SPECIALS.wail.charge), blast: scalar(SPECIALS.wail.blast), fade: 0.6,
    radius: scalar(SPECIALS.wail.radius), range: scalar(SPECIALS.wail.range), dps: scalar(SPECIALS.wail.dps),
  },
};

const outputJson = JSON.stringify({ sources: sourcePaths.map((path, i) => ({ path, sha256: hashes[i] })), sonar: sonarFixture, wail: wailFixture }, null, 2) + '\n';
const header = [
  '// Generated by tools/export-sonar-wail-source-fixtures.mjs. Do not edit by hand.',
  '#pragma once', '#include <array>', '#include <string_view>', 'namespace restoration::test {',
  'struct SonarSourceFixture { double cast, reveal, slow, inkMul, observedSlow, observedInkMul, expirySeconds, revealAfterExpiry, trackAfterExpiry; int castFrames, enemyCount, trackedCount, activeAfterCast; double x, y, z, vx, vy, vz; };',
  `inline constexpr SonarSourceFixture sonarSourceFixture{${[
    sonarFixture.config.cast, sonarFixture.config.duration, sonarFixture.config.slow, sonarFixture.config.inkMul,
    sonarSpeed.revealed / sonarSpeed.normal, sonarInk.observedMultiplier,
    sonarExpiry.seconds, sonarExpiry.reveal, sonarExpiry.track,
    sonarBody.frames, sonarInitial.enemies.length, sonarInitial.enemies.filter((e) => e.track > 0).length,
    sonarBody.activeAfterCast ? 1 : 0, ...sonarBody.ownerPos, ...sonarBody.ownerVel,
  ].map((x, index) => [9, 10, 11, 12].includes(index) ? String(x) : Number(x).toPrecision(17)).join(',')}};`,
  'struct WailSourceFixture { double holdSpeed, fireMin, holdTime, botAutoTime, botUniform, charge, blast, fade, radius, range, dps; int pressFrames, timeoutFrames, botFrames, thresholdRejected, wallPiercing, stepChargeFrames, stepBlastFrames, stepFadeFrames; double x, y, z, mouthX, mouthY, mouthZ, dirX, dirY, dirZ, lineDamage, offAxisDamage, steppedLineDamage; };',
  `inline constexpr WailSourceFixture wailSourceFixture{${[
    wailFixture.config.holdSpeed, 0.2, wailFixture.config.holdTime,
    wailFixture.bot.placement.autoT,
    wailFixture.bot.placement.botUniform,
    wailFixture.config.charge, wailFixture.config.blast, wailFixture.config.fade,
    wailFixture.config.radius, wailFixture.config.range, wailFixture.config.dps,
    wailFixture.press.placement.frames, wailFixture.timeout.placement.frames, wailFixture.bot.placement.frames,
    wailFixture.press.placement.thresholdRejected ? 1 : 0,
    wailFixture.press.world.afterBlast.raycastsDuringBlast === 0 && wailFixture.press.world.afterBlast.lineDamage > 0 ? 1 : 0,
    wailFixture.fixedStep.world.chargeFrames,
    wailFixture.fixedStep.world.blastFrames,
    wailFixture.fixedStep.world.fadeFrames,
    ...wailFixture.press.placement.speaker.pos, ...wailFixture.press.placement.speaker.mouth, ...wailFixture.press.placement.speaker.dir,
    wailFixture.press.world.afterBlast.lineDamage, wailFixture.press.world.afterBlast.offAxisDamage,
    wailFixture.fixedStep.world.steppedLineDamage,
  ].map((x, index) => [11, 12, 13, 14, 15, 16, 17, 18].includes(index) ? String(x) : Number(x).toPrecision(17)).join(',')}};`,
  `inline constexpr std::array<std::string_view, ${sourcePaths.length}> sonarWailSourceHashes{{${hashes.map((hash) => `"${hash}"`).join(',')}}};`,
  '}', '',
].join('\n');
const headerPath = new URL('../tests/fixtures/sonar_wail_source_fixtures.hpp', import.meta.url);
const jsonPath = new URL('../tests/fixtures/sonar_wail_source_fixtures.json', import.meta.url);
if (process.argv.includes('--check')) {
  if (readFileSync(headerPath, 'utf8') !== header) throw new Error('Sonar/Wail C++ source fixture differs');
  if (readFileSync(jsonPath, 'utf8') !== outputJson) throw new Error('Sonar/Wail JSON source fixture differs');
} else {
  writeFileSync(headerPath, header);
  writeFileSync(jsonPath, outputJson);
}
unlisten.forEach((off) => off());
Math.random = random;
console.log(`Locked actual Sonar and Wail source behavior; ${sourcePaths.length} SHA-256 source stamps`);
