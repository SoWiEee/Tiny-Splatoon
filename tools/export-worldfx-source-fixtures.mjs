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
for (const name of ['burst', 'explosion', 'slamLaunch', 'slamWave', 'stormStart', 'rainSheet']) {
  const fx = new FX(new THREE.Scene(), { quality: 1 });
  seed = 0x6d2b79f5;
  if (name === 'burst') fx.burst(origin, up, orange, { count: 12, size: 0.1 });
  if (name === 'explosion') fx.explosion(origin, orange, 3);
  if (name === 'slamLaunch') fx.slamLaunch(origin, orange);
  if (name === 'slamWave') fx.slamWave(origin, orange, 5.2);
  if (name === 'stormStart') fx.stormStart(origin, orange, 3.4);
  if (name === 'rainSheet') fx.rainSheet(new THREE.Vector3(0, 4.6, 0), 3.4, orange, 1 / 60);
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
const path = new URL('../tests/worldfx_source_fixtures.json', import.meta.url);
const data = JSON.stringify(out, null, 2) + '\n';
if (check) {
  if (await readFile(path, 'utf8') !== data) throw new Error('Stale World FX source fixtures');
} else await writeFile(path, data);
console.log(`${check ? 'Verified' : 'Wrote'} ${Object.keys(recipes).length} source FX recipes with pooled particle snapshots`);
