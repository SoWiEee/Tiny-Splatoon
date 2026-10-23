// Development oracle for the three main kits that own nonstandard action state.
import { register } from 'node:module';
import { readFileSync, writeFileSync } from 'node:fs';
register(new URL('../tools/reference-loader.mjs', import.meta.url));
const THREE = await import('three');
const { G, on } = await import('../splat-ref/src/core/ctx.js');
const { WeaponRunner } = await import('../splat-ref/src/game/weapons.js');
await import('../splat-ref/src/game/kits/index.js');
const { MAIN_KITS } = await import('../splat-ref/src/game/kits/registry.js');
const { BOW_DEBUG } = await import('../splat-ref/src/game/kits/bow.js');
const { WEAPONS, SUBS } = await import('../splat-ref/src/config.js');

const dt = 1 / 60;
const cases = [
  { id: 'bow', hold: 0.2, jump: 0, total: 0.9 },
  { id: 'bow', hold: 0.6, jump: 0, total: 1.2 },
  { id: 'bow', hold: 1.1, jump: 0, total: 1.7 },
  { id: 'blade', hold: 0.1, jump: 0, total: 0.9 },
  { id: 'blade', hold: 0.4, jump: 0, total: 1.2 },
  { id: 'blade', hold: 0.9, jump: 0, total: 1.6 },
  { id: 'mitts', hold: 0.55, jump: 0, total: 1.0 },
  { id: 'mitts', hold: 0.8, jump: 0.45, total: 1.4 },
];
const rows = [];
const trace = { shots: 0, ringEvents: 0, leaps: 0, yaw: 0 };
on('weapon:fire', (event) => { trace.shots++; if (event?.dir) trace.yaw = Math.atan2(event.dir.x, event.dir.z); });
on('bow:ring', () => { trace.ringEvents++; });
on('mitts:leap', () => { trace.leaps++; });
const projectileSpawns = [];
const projectiles = {
  _muzzle(actor, out) { return out.set(actor.pos.x, actor.pos.y + 1, actor.pos.z); },
  _aimFrom(actor, muzzle, out) { return out.set(actor.aimPoint.x - muzzle.x, actor.aimPoint.y - muzzle.y, actor.aimPoint.z - muzzle.z).normalize(); },
  _ballistic() {},
  _spread() {},
  _new() { return { pos: new THREE.Vector3(), prev: new THREE.Vector3(), start: new THREE.Vector3(), vel: new THREE.Vector3() }; },
  _push(projectile) { projectileSpawns.push(projectile); },
  applyHit() {},
};
G.projectiles = projectiles;
G.teamColors = [new THREE.Color(1, 0.4, 0.1), new THREE.Color(0.2, 0.35, 1)];
G.paint = { splat() { return 0; } };
G.physics = {
  los() { return true; },
  segment() { return { hit: false, normal: new THREE.Vector3(), point: new THREE.Vector3(), block: -1 }; },
  raycast() { return { hit: false, normal: new THREE.Vector3(), point: new THREE.Vector3(), block: -1 }; },
};
G.level = { blocks: [], groundHeight() { return 0; } };
Math.random = () => 0.5;
const n = (v) => Number.isFinite(v) ? Number(v).toPrecision(17) : '0.0';

for (let caseIndex = 0; caseIndex < cases.length; caseIndex++) {
  const c = cases[caseIndex];
  MAIN_KITS[c.id].clear?.();
  projectileSpawns.length = 0;
  trace.shots = trace.ringEvents = trace.leaps = 0;
  trace.yaw = 0;
  G.time = 0;
  const actor = {
    weapon: WEAPONS[c.id], sub: SUBS.bomb,
    pos: new THREE.Vector3(), vel: new THREE.Vector3(), grounded: true, form: 'kid', alive: true,
    contacts: { wall: false, wallNormal: new THREE.Vector3(), wallBlock: -1 }, groundN: new THREE.Vector3(0, 1, 0),
    coyote: 0, jumpBuffer: 0,
    aimYaw: 0, aimPitch: 0, aimPoint: new THREE.Vector3(c.id === 'blade' ? 4 : 0, 1, c.id === 'blade' ? 20 : 12),
    yaw: 0, ink: 100, lastFire: 99, fireFacing: 0, team: 0, kidT: 99,
    intent: { move: new THREE.Vector3(), fire: false, jump: false },
    character: { trigger() {}, getMuzzle(out) { return out.set(0, 1.05, 0); } }, addTurf() {}, addTurfNoSpecial() {}, _nearCamera() { return false; },
  };
  G.actors = [actor];
  const runner = new WeaponRunner(actor);
  actor.weaponRunner = runner;
  let previousFire = false, previousJump = false;
  const holdFrames = Math.round(c.hold / dt), jumpFrames = Math.round(c.jump / dt), totalFrames = Math.round(c.total / dt);
  for (let frame = 1; frame <= totalFrames; frame++) {
    G.time += dt;
    const fire = frame <= holdFrames, jump = frame <= jumpFrames;
    actor.intent.fire = fire; actor.intent.jump = jump;
    if (c.id === 'mitts' && jump && !previousJump) MAIN_KITS.mitts.jump(runner, { fire, jump });
    runner.update(dt, { fire, firePressed: fire && !previousFire, sub: false, subReleased: false });
    previousFire = fire; previousJump = jump;
    const center = c.id === 'bow' ? BOW_DEBUG.arrows.find((p) => p.center) : null;
    rows.push({
      caseIndex, id: c.id, hold: c.hold, jump: c.jump, frame,
      ink: actor.ink, charge: runner.charge || 0, shots: trace.shots, ringEvents: trace.ringEvents,
      ring: runner.kit?.ring || 0, charging: +(['blade', 'mitts'].includes(c.id) ? !!runner.kit?.charging : !!runner.charging),
      firing: +runner.firingPose(), busy: +!!runner.busy(), leaping: +(c.id === 'mitts' && !!runner.kit?.leaping),
      leaps: trace.leaps, vx: actor.vel.x, vy: actor.vel.y, vz: actor.vel.z,
      yaw: trace.yaw, arrows: c.id === 'bow' ? BOW_DEBUG.arrows.length : 0,
      tier: center?.tier ?? -1, range: center?.range ?? 0, damage: center?.dmg ?? 0, lodge: +!!center?.lodge,
    });
  }
  MAIN_KITS[c.id].clear?.();
}

const output = ['#pragma once', '#include <array>', 'namespace restoration::test {',
  'struct CombatKitFixture { int caseIndex; const char* id; double hold,jump; int frame; double ink,charge,yaw,vx,vy,vz,range,damage; int shots,ringEvents,ring,charging,firing,leaping,leaps,arrows,tier,lodge,busy; };',
  `inline constexpr std::array<CombatKitFixture, ${rows.length}> combatKitFixtures{{`,
  ...rows.map((r) => `  {${r.caseIndex},"${r.id}",${n(r.hold)},${n(r.jump)},${r.frame},${[r.ink,r.charge,r.yaw,r.vx,r.vy,r.vz,r.range,r.damage].map(n).join(',')},${[r.shots,r.ringEvents,r.ring,r.charging,r.firing,r.leaping,r.leaps,r.arrows,r.tier,r.lodge,r.busy].join(',')}},`),
  '}};', '}', ''].join('\n');
const path = new URL('./fixtures/combat_kit_fixtures.hpp', import.meta.url);
if (process.argv.includes('--check')) {
  if (readFileSync(path, 'utf8') !== output) throw new Error('Combat kit source action oracle differs');
} else writeFileSync(path, output);
console.log(`Locked main kit source: ${rows.length} action/state/event snapshots`);
