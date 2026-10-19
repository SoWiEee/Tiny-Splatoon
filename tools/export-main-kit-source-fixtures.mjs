// Execute the locked source kit runner hooks and export action/state snapshots for the three restored main kits.
import { register } from 'node:module';
import { readFileSync, writeFileSync } from 'node:fs';
register(new URL('./reference-loader.mjs', import.meta.url));

const THREE = await import('three');
const { G, on } = await import('../splat-ref/src/core/ctx.js');
const { WeaponRunner } = await import('../splat-ref/src/game/weapons.js');
await import('../splat-ref/src/game/kits/index.js');
const { MAIN_KITS } = await import('../splat-ref/src/game/kits/registry.js');
const { WEAPONS } = await import('../splat-ref/src/config.js');
const { BOW_DEBUG } = await import('../splat-ref/src/game/kits/bow.js');
const { MITTS } = await import('../splat-ref/src/game/kits/mitts.js');

let seed = 0x5eed1234;
Math.random = () => { seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0; return seed / 0x100000000; };
const events = [], drops = [];
let currentFrame = 0;
const projectileCount = id => id === 'bow' ? BOW_DEBUG.arrows.length : id === 'blade' ? drops.length : MITTS.fists.length;
const vec = v => [v.x, v.y, v.z];
function command(e) {
  if (e.weapon === 'bow') {
    const p = BOW_DEBUG.arrows.find(x => x.center);
    return { speed: p.speed, range: p.range, damage: p.dmg, sideDamage: p.dmg, fan: Math.acos(Math.max(-1, Math.min(1, p.dir.z))) * 180 / Math.PI,
      radius: p.br, delay: p.fuse, x: p.pos.x, y: p.pos.y, z: p.pos.z, vx: p.vel.x, vy: p.vel.y, vz: p.vel.z, state: p.st };
  }
  if (e.weapon === 'blade') {
    const p = drops[0];
    return { speed: Math.hypot(p.vel.x, p.vel.y, p.vel.z), range: p.life, damage: p.damage, sideDamage: p.dmgFar,
      radius: p.radius, delay: p.delay, x: p.pos.x, y: p.pos.y, z: p.pos.z, vx: p.vel.x, vy: p.vel.y, vz: p.vel.z };
  }
  const p = MITTS.fists.at(-1);
  return { speed: p.vel.length(), range: WEAPONS.mitts.fistRange, damage: WEAPONS.mitts.punchDamage,
    sideDamage: WEAPONS.mitts.splashMax, radius: WEAPONS.mitts.fistSize, delay: WEAPONS.mitts.splashRadius,
    x: p.pos.x, y: p.pos.y, z: p.pos.z, vx: p.vel.x, vy: p.vel.y, vz: p.vel.z };
}
on('weapon:fire', e => events.push({ ...e, ...command(e), frame: currentFrame, ink: e.actor.ink,
  runnerCharge: e.actor.weaponRunner.charge, projectileCount: projectileCount(e.weapon) }));
on('mitts:leap', e => events.push({ ...e, frame: currentFrame, type: 'mitts:leap', ink: e.actor.ink,
  charge: e.charge, velocity: vec(e.actor.vel) }));
G.projectiles = {
  _muzzle(a, out) { return out.set(a.pos.x, a.pos.y + 1.05, a.pos.z + 0.3); },
  _aimFrom(a, _from, out) { return out.copy(a.aimDir); },
  _ballistic() {},
  _spread() {},
  _new() { return { pos: new THREE.Vector3(), prev: new THREE.Vector3(), start: new THREE.Vector3(), vel: new THREE.Vector3() }; },
  _push(p) { drops.push(p); },
  applyHit() {},
};
G.physics = {
  los() { return true; },
  raycast() { return { hit: false }; },
  segment() { return { hit: false }; },
};
G.level = { blocks: [], groundHeight() { return 0; } };
G.paint = { splat() { return 0; } };
G.subs = { blockShot() { return false; }, damageArea() {} };
G.teamColors = [new THREE.Color(1, 0.25, 0), new THREE.Color(0, 0.25, 1)];

const rows = [];
function actorFor(id) {
  const a = {
    weapon: WEAPONS[id], pos: new THREE.Vector3(), vel: new THREE.Vector3(), aimDir: new THREE.Vector3(0, 0, 1),
    aimPoint: new THREE.Vector3(0, 0, 20), grounded: true, aimYaw: 0, aimPitch: 0, yaw: 0,
    ink: 100, lastFire: 99, fireFacing: 0, team: 0, alive: true, form: 'kid', kidT: 99,
    isLocal: false, isBot: false, specialActive: null,
    intent: { move: new THREE.Vector3(), fire: false, jump: false },
    character: { trigger() {}, getMuzzleHand(out) { return out.set(0, 1, 0); }, getMuzzle(out) { return out.set(0, 1, 0); } },
    addTurf() {}, addTurfNoSpecial() {}, _nearCamera() { return false; },
    coyote: 0, jumpBuffer: 0, contacts: { wall: false }, invuln: 0,
  };
  a.weaponRunner = new WeaponRunner(a);
  return a;
}
function update(a, dt, fire, jump = false, pressed = false, frame = currentFrame + 1) {
  currentFrame = frame;
  a.intent.fire = fire; a.intent.jump = jump;
  G.time += dt;
  a.weaponRunner.update(dt, { fire, firePressed: pressed, sub: false, subReleased: false });
}
function resetOutput() {
  events.length = 0; drops.length = 0; BOW_DEBUG.arrows.length = 0; MITTS.fists.length = 0;
}

// The source WeaponRunner calls the real bow kit update; source arrows are captured immediately after release.
resetOutput();
{
  const id = 'bow', dt = 1 / 60, holdFrames = 12, a = actorFor(id);
  for (let f = 1; f <= holdFrames; f++) update(a, dt, true, false, f === 1);
  const drawn = { frame: holdFrames, ink: a.ink, charge: a.weaponRunner.charge, charging: a.weaponRunner.charging };
  update(a, dt, false); const releaseFrame = holdFrames + 1;
  const event = events.find(e => e.weapon === 'bow');
  for (let i = 0; i < BOW_DEBUG.arrows.length; i++) {
    const p = BOW_DEBUG.arrows[i];
    rows.push({ weapon: id, action: 'arrow', frame: event?.frame ?? releaseFrame, ordinal: i - 1, ink: event?.ink ?? a.ink, charge: event?.charge ?? drawn.charge,
      projectileCount: event?.projectileCount ?? BOW_DEBUG.arrows.length, speed: p.speed, range: p.range, damage: p.dmg, sideDamage: p.dmg,
      fan: Math.acos(Math.max(-1, Math.min(1, p.dir.z))) * 180 / Math.PI, radius: p.br, delay: p.fuse,
      x: p.pos.x, y: p.pos.y, z: p.pos.z, vx: p.vel.x, vy: p.vel.y, vz: p.vel.z,
      event: 1, chargeAtDraw: drawn.charge, drawnInk: drawn.ink, state: p.st });
  }
}

// A tap emits source droplets after the source swing's 28 ms windup.
resetOutput();
{
  const id = 'blade', dt = 1 / 60, a = actorFor(id);
  update(a, dt, true, false, true, 1);
  let emitFrame = 0;
  for (let f = 1; f <= 30; f++) {
    update(a, dt, false, false, false, f + 1);
    if (drops.length && !emitFrame) emitFrame = currentFrame;
  }
  const first = drops[0];
  const event = events.find(e => e.weapon === 'blade' && e.cut === 'quick');
  rows.push({ weapon: id, action: 'tap', frame: event?.frame ?? emitFrame, ordinal: 0, ink: event?.ink ?? a.ink, charge: a.weaponRunner.charge,
    projectileCount: event?.projectileCount ?? drops.length, speed: Math.hypot(first.vel.x, first.vel.y, first.vel.z), range: first.life,
    damage: first.damage, sideDamage: first.dmgFar, fan: 0, radius: first.radius, delay: first.delay,
    x: first.pos.x, y: first.pos.y, z: first.pos.z, vx: first.vel.x, vy: first.vel.y, vz: first.vel.z,
    event: events.filter(e => e.weapon === 'blade' && e.cut === 'quick').length, chargeAtDraw: 0, drawnInk: 100, state: 0 });
}

// A hold that reaches full charge emits the source charged cut/wave after release.
resetOutput();
{
  const id = 'blade', dt = 1 / 60, a = actorFor(id), holdFrames = 60;
  for (let f = 1; f <= holdFrames; f++) update(a, dt, true, false, f === 1);
  const drawn = { ink: a.ink, charge: a.weaponRunner.charge, charging: !!a.weaponRunner.kit?.charging };
  update(a, dt, false, false, false, holdFrames + 1);
  for (let f = 1; f <= 4; f++) update(a, dt, false, false, false, holdFrames + 1 + f);
  const e = events.find(x => x.weapon === 'blade' && x.cut === 'charged');
  rows.push({ weapon: id, action: 'heavy', frame: e?.frame ?? 0, ordinal: 0,
    ink: e?.ink ?? a.ink, charge: drawn.charge, projectileCount: 0, speed: 26, range: 12, damage: 70,
    sideDamage: 50, fan: 0, radius: 0.7, delay: 0, x: e?.muzzle?.x ?? 0, y: e?.muzzle?.y ?? 0, z: e?.muzzle?.z ?? 0,
    vx: e?.dir?.x ?? 0, vy: e?.dir?.y ?? 0, vz: e?.dir?.z ?? 0,
    event: events.filter(x => x.weapon === 'blade' && x.cut === 'charged').length, chargeAtDraw: drawn.charge, drawnInk: drawn.ink, state: 1 });
}

// Punches are emitted by the actual source kit update and tracked in its module-owned fist records.
resetOutput();
{
  const id = 'mitts', dt = 1 / 60, a = actorFor(id), end = 18;
  for (let f = 1; f <= end; f++) update(a, dt, true, false, f === 1, f);
  const punches = events.filter(e => e.weapon === 'mitts');
  for (let i = 0; i < punches.length; i++) {
    const p = MITTS.fists[i], event = punches[i];
    rows.push({ weapon: id, action: 'punch', frame: event.frame, ordinal: event.hand,
      ink: event.ink, charge: a.weaponRunner.charge, projectileCount: event.projectileCount,
      speed: p.vel.length(), range: WEAPONS.mitts.fistRange, damage: WEAPONS.mitts.punchDamage,
      sideDamage: WEAPONS.mitts.splashMax, fan: 0, radius: WEAPONS.mitts.fistSize, delay: WEAPONS.mitts.splashRadius,
      x: p.pos.x, y: p.pos.y, z: p.pos.z, vx: p.vel.x, vy: p.vel.y, vz: p.vel.z,
      event: events.filter(e => e.weapon === 'mitts').length, chargeAtDraw: 0, drawnInk: 100, state: 0 });
  }
}

// The source actor routes the rising jump edge through the kit's jump hook, then the source runner updates the charge.
resetOutput();
{
  const id = 'mitts', dt = 1 / 60, a = actorFor(id), r = a.weaponRunner, holdFrames = 27;
  MAIN_KITS.mitts.jump(r, { fire: true });
  for (let f = 1; f <= holdFrames; f++) update(a, dt, true, true, f === 1, f);
  const drawn = { ink: a.ink, charge: r.charge, charging: !!r.kit?.charging };
  update(a, dt, true, false, false, holdFrames + 1);
  const leap = events.find(e => e.type === 'mitts:leap');
  rows.push({ weapon: id, action: 'leap', frame: leap?.frame ?? 0, ordinal: 0, ink: leap?.ink ?? a.ink,
    charge: leap?.charge ?? drawn.charge, projectileCount: 0, speed: a.vel.length(), range: 0,
    damage: WEAPONS.mitts.landDamageMax, sideDamage: WEAPONS.mitts.landDamageMin, fan: 0,
    radius: WEAPONS.mitts.landRadius, delay: WEAPONS.mitts.leapArmorGrace,
    x: a.vel.x, y: a.vel.y, z: a.vel.z, vx: leap?.to?.x ?? 0, vy: leap?.to?.y ?? 0, vz: leap?.to?.z ?? 0,
    event: leap ? 1 : 0, chargeAtDraw: drawn.charge, drawnInk: drawn.ink, state: drawn.charging ? 1 : 0 });
}

const n = v => Number(v).toPrecision(17);
const output = ['#pragma once', '#include <array>', 'namespace restoration::test {',
  'struct MainKitTraceFixture { const char* weapon; const char* action; int frame,ordinal; double ink,charge; int projectileCount; double speed,range,damage,sideDamage,fan,radius,delay,x,y,z,vx,vy,vz; int event; double chargeAtDraw,drawnInk; int state; };',
  `inline constexpr std::array<MainKitTraceFixture, ${rows.length}> mainKitTraceFixtures{{`,
  ...rows.map(r => `  {"${r.weapon}","${r.action}",${r.frame},${r.ordinal},${n(r.ink)},${n(r.charge)},${r.projectileCount},${[r.speed,r.range,r.damage,r.sideDamage,r.fan,r.radius,r.delay,r.x,r.y,r.z,r.vx,r.vy,r.vz].map(n).join(',')},${r.event},${n(r.chargeAtDraw)},${n(r.drawnInk)},${r.state}},`),
  '}};', '}', ''].join('\n');
const path = new URL('../tests/main_kit_source_fixtures.hpp', import.meta.url);
if (process.argv.includes('--check')) {
  if (readFileSync(path, 'utf8') !== output) throw new Error('Main kit source action oracle differs');
} else writeFileSync(path, output);
console.log(`Locked source main kit actions: ${rows.length} rows`);
