// Drives the actual reference FX recipes without a WebGL renderer. The pooled
// Float32 arrays are the source contract; count-only summaries are not enough
// to validate particle shape, colour, timing, velocity, or rendering output.
import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { readFile, writeFile } from 'node:fs/promises';
import { register } from 'node:module';
register(new URL('./reference-loader.mjs', import.meta.url));
const THREE = await import('three');
const source = readFileSync(new URL('../splat-ref/src/fx/fx.js', import.meta.url));
const checksum = createHash('sha256').update(source).digest('hex');
const check = process.argv.includes('--check');
if (process.argv.slice(2).some(arg => arg !== '--check')) throw new Error('Usage: node tools/export-worldfx-source-fixtures.mjs [--check]');
const origin = new THREE.Vector3(0, 0, 0), up = new THREE.Vector3(0, 1, 0);
const forward = new THREE.Vector3(0, 0, 1), velocity = new THREE.Vector3(2.5, 4, -1.75);
const diagonal = new THREE.Vector3(0.3, 0.2, 0.9);
const wallNormal = new THREE.Vector3(1, 0, 0), waterPos = new THREE.Vector3(0, -1.6, 0);
const orange = new THREE.Color('#ff8a14');
const nativeRandom = Math.random;
let seed = 0x6d2b79f5;
Math.random = () => { seed ^= seed << 13; seed ^= seed >>> 17; seed ^= seed << 5; return (seed >>> 0) / 4294967296; };
const { FX } = await import('../splat-ref/src/fx/fx.js');
const recipes = {};
const floats = (array, count) => Array.from(array.subarray(0, count));
function pool(fx, name, immediate) {
  const raw = (array, stride, count) => floats(array, stride * count);
  const render = (geometry, names, count) => Object.fromEntries(names.map(attr => [attr, raw(geometry.attributes[attr].array, geometry.attributes[attr].itemSize, count)]));
  if (name === 'drops') return {
    count: fx.dN, pos: raw(fx.dP, 3, fx.dN), vel: raw(fx.dV, 3, fx.dN), color: raw(fx.dC, 3, fx.dN),
    attrs: raw(fx.dA, 8, fx.dN),
    ...(!immediate && { render: render(fx.dGeo, ['aPosR', 'aVelS', 'aColA'], fx.dN) }),
  };
  const value = fx[name];
  const isSprite = name === 'puffs' || name === 'glows';
  const strides = isSprite ? [3, 3, 3, 14] : name === 'rings' ? [3, 3, 3, 3, 8] : name === 'shells' ? [3, 3, 3, 9, 4] : [3, 3, 5];
  const arrays = isSprite ? [value.P, value.V, value.C, value.X]
    : name === 'rings' ? [value.P, value.V, value.N, value.C, value.X]
      : name === 'shells' ? [value.P, value.V, value.C, value.X, value.A]
        : [value.P, value.C, value.X];
  const keys = isSprite ? ['pos', 'vel', 'color', 'attrs']
    : name === 'rings' ? ['pos', 'vel', 'normal', 'color', 'attrs']
      : name === 'shells' ? ['pos', 'vel', 'color', 'attrs', 'axis'] : ['pos', 'color', 'attrs'];
  const result = { count: value.n };
  arrays.forEach((array, i) => result[keys[i]] = raw(array, strides[i], value.n));
  if (!immediate) {
    const names = isSprite ? ['aPosSize', 'aColA', 'aMisc'] : name === 'rings' ? ['aPosR', 'aNrmT', 'aColA', 'aMisc']
      : name === 'shells' ? ['aPosR', 'aColA', 'aMisc', 'aAxis'] : ['aPosR', 'aColA', 'aMisc'];
    result.render = render(value.geo, names, value.n);
  }
  return result;
}
function snapshot(fx, immediate) {
  return Object.fromEntries(['drops', 'puffs', 'glows', 'rings', 'shells', 'beams'].map(name => [name, pool(fx, name, immediate)]));
}
for (const name of ['burst', 'explosion', 'slamLaunch', 'slamWave', 'stormStart', 'rainSheet', 'formPop', 'dive', 'emerge', 'wake', 'footstepOwn', 'footstepEnemy', 'footstepDry',
  'muzzleBlaster', 'muzzleCharger', 'flickCurtain', 'rollerSpray', 'dryFire', 'bombTrail', 'bounceSplash', 'beepPulse', 'beepPulseNoPoint',
  'shotTrail', 'shotTrailBig', 'sloshTrail', 'sloshTrailHead', 'sloshImpact', 'chargeGlow', 'chargeFull', 'laserDot', 'beamImpact', 'beamTrail', 'spinUp', 'spinStream', 'spinFull',
  'dodgeSplash', 'dodgeSkid', 'dodgePlant', 'mistAlpha20', 'mistAlpha30', 'leapRing', 'canopyBreakRing', 'wailRing',
  'drop', 'mist', 'climbDrip', 'superJumpTrail', 'superJumpLand', 'waterPlop', 'ring', 'glint', 'stormPuddle', 'stormFlash']) {
  const fx = new FX(new THREE.Scene(), { quality: 1 });
  seed = 0x6d2b79f5;
  if (name === 'burst') fx.burst(origin, up, orange, { count: 12, size: 0.1 });
  if (name === 'explosion') fx.explosion(origin, orange, 3);
  if (name === 'slamLaunch') fx.slamLaunch(origin, orange);
  if (name === 'slamWave') fx.slamWave(origin, orange, 5.2);
  if (name === 'stormStart') fx.stormStart(origin, orange, 3.4);
  if (name === 'rainSheet') fx.rainSheet(new THREE.Vector3(0, 4.6, 0), 3.4, orange, 1 / 60);
  if (name === 'formPop') fx.formPop(origin, orange, true, false);
  if (name === 'dive') fx.dive(origin, orange, 4);
  if (name === 'emerge') fx.emerge(origin, orange, 4);
  if (name === 'wake') fx.wake(origin, new THREE.Vector3(0, 0, 1), orange, 8);
  if (name.startsWith('footstep')) fx.footstep(origin, orange, name === 'footstepOwn' ? 1 : name === 'footstepEnemy' ? 2 : 0, new THREE.Vector3(0, 0, 1), 5);
  if (name === 'muzzleBlaster') fx.muzzle(origin, forward, orange, 'blaster');
  if (name === 'muzzleCharger') fx.muzzle(origin, forward, orange, 'charger');
  if (name === 'flickCurtain') fx.flickCurtain(origin, forward, orange, 50);
  if (name === 'rollerSpray') fx.rollerSpray(origin, forward, 1.1, orange, 0.7);
  if (name === 'dryFire') fx.dryFire(origin, forward);
  if (name === 'bombTrail') fx.bombTrail(origin, velocity, orange);
  if (name === 'bounceSplash') fx.bounceSplash(origin, up, orange);
  if (name === 'beepPulse') fx.beepPulse(origin, new THREE.Vector3(0, 0.01, 0), up, orange, 3.1, 0.7);
  if (name === 'beepPulseNoPoint') fx.beepPulse(origin, null, up, orange, 3.1, 0.7);
  if (name === 'shotTrail') fx.shotTrail(origin, velocity, orange, false);
  if (name === 'shotTrailBig') fx.shotTrail(origin, velocity, orange, true);
  if (name === 'sloshTrail') fx.sloshTrail(origin, velocity, orange, false);
  if (name === 'sloshTrailHead') fx.sloshTrail(origin, velocity, orange, true);
  if (name === 'sloshImpact') fx.sloshImpact(origin, up, diagonal, orange);
  if (name === 'chargeGlow') fx.chargeGlow(origin, orange, 0.65);
  if (name === 'chargeFull') fx.chargeFull(origin, orange);
  if (name === 'laserDot') fx.laserDot(origin, up, orange, 0.65);
  if (name === 'beamImpact') fx.beamImpact(origin, up, orange, 0.75);
  if (name === 'beamTrail') fx.beamTrail(origin, new THREE.Vector3(0.7, 1.1, 4.2), orange, 0.75);
  if (name === 'spinUp') fx.spinUp(origin, diagonal, orange, 0.72, false);
  if (name === 'spinStream') fx.spinUp(origin, diagonal, orange, 0.72, true);
  if (name === 'spinFull') fx.spinFull(origin, diagonal, orange);
  if (name === 'dodgeSplash') fx.dodgeSplash(origin, forward, orange);
  if (name === 'dodgeSkid') fx.dodgeSkid(origin, forward, orange, 0.35);
  if (name === 'dodgePlant') fx.dodgePlant(origin, forward, orange);
  if (name === 'mistAlpha20') fx.mist(origin, velocity, orange, 0.2, 0.2);
  if (name === 'mistAlpha30') fx.mist(origin, velocity, orange, 0.2, 0.3);
  if (name === 'leapRing') fx.ring(origin, up, orange, { radius: 1.3, life: 0.35, style: 0, alpha: 0.95, thickness: 1, snap: false });
  if (name === 'canopyBreakRing') fx.ring(origin, up, orange, { radius: 0.84, life: 0.3, style: 0, alpha: 0.95, thickness: 1, snap: false });
  if (name === 'wailRing') fx.ring(origin, forward, orange, { radius: 1.3, life: 0.3, style: 2, alpha: 0.9, thickness: 1, snap: false });
  if (name === 'drop') fx.drop(origin, velocity, orange, { size: 0.15, life: 1.2, gravity: 1, stretch: 1 });
  if (name === 'mist') fx.mist(origin, velocity, orange, 0.2, 0.25);
  if (name === 'climbDrip') fx.climbDrip(origin, wallNormal, orange);
  if (name === 'superJumpTrail') fx.superJumpTrail(origin, velocity, orange);
  if (name === 'superJumpLand') fx.superJumpLand(origin, orange);
  if (name === 'waterPlop') fx.waterPlop(waterPos, 0.6);
  if (name === 'ring') fx.ring(origin, up, orange, { radius: 0.8, life: 0.35, style: 0, alpha: 0.95, thickness: 1, snap: false });
  if (name === 'glint') fx.glint(origin, orange, 0.12);
  if (name === 'stormPuddle') fx.stormPuddle(origin, up, orange);
  if (name === 'stormFlash') fx.stormFlash(origin, orange, 3.4);
  const immediate = fx.stats();
  const immediatePools = snapshot(fx, true);
  fx.update(1 / 60);
  const firstFrame = fx.stats();
  const firstFramePools = snapshot(fx, false);
  for (let i = 0; i < 180; i++) fx.update(1 / 60);
  const expired = fx.stats();
  recipes[name] = { immediate, immediatePools, firstFrame, firstFramePools, expired };
  fx.dispose();
}
Math.random = nativeRandom;
const out = { source: 'splat-ref/src/fx/fx.js', sha256: checksum, rng: 'xorshift32(seed=0x6d2b79f5, Math.random call order)', vectorColor: '#ff8a14 (THREE.Color linear RGB)', recipes };
const path = new URL('../tests/fixtures/worldfx_source_fixtures.json', import.meta.url);
const data = JSON.stringify(out, (key, value) =>
  typeof value === 'number' && Object.is(value, -0) ? '__WORLD_FX_NEGATIVE_ZERO__' : value, 2)
  .replaceAll('"__WORLD_FX_NEGATIVE_ZERO__"', '-0') + '\n';
if (check) {
  if (await readFile(path, 'utf8') !== data) throw new Error('Stale World FX source fixtures');
} else await writeFile(path, data);
console.log(`${check ? 'Verified' : 'Wrote'} ${Object.keys(recipes).length} source FX recipes with pooled particle snapshots`);
