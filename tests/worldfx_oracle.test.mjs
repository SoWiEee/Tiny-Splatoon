import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFileSync } from 'node:fs';
import { spawnSync } from 'node:child_process';

const fixture = JSON.parse(readFileSync(new URL('./worldfx_source_fixtures.json', import.meta.url), 'utf8'));
const source = readFileSync(new URL('../splat-ref/src/fx/fx.js', import.meta.url));
assert.equal(createHash('sha256').update(source).digest('hex'), fixture.sha256, 'source fx.js hash');
const names = Object.keys(fixture.recipes);
const result = spawnSync(process.argv[2], [], { input: `${names.join('\n')}\n`, encoding: 'utf8', maxBuffer: 32 * 1024 * 1024 });
assert.equal(result.status, 0, result.stderr);
const actual = result.stdout.trim().split(/\r?\n/).map(line => JSON.parse(line));
assert.equal(actual.length, names.length);
let maxObservedTrigUlp = 0;
let maxObservedTrigPath = '';

function orderedFloatBits(value) {
  const view = new DataView(new ArrayBuffer(4));
  view.setFloat32(0, value, false);
  const bits = view.getUint32(0, false);
  return bits & 0x80000000 ? (~bits >>> 0) : (bits | 0x80000000);
}

function trigSensitive(path) {
  const recipe = path.split('.')[0];
  const direct = /\.(drops|puffs)\.(pos|vel)\[(\d+)\]$/.exec(path);
  if (direct) {
    if (recipe === 'burst') return direct[1] === 'drops' && direct[2] === 'vel';
    if (recipe === 'explosion') return true;
    if (recipe === 'slamLaunch') return direct[1] === 'drops' && direct[2] === 'vel';
    if (recipe === 'slamWave') return true;
    if (recipe === 'stormStart') return direct[1] === 'puffs';
    if (recipe === 'formPop' || recipe === 'dive' || recipe === 'emerge') return direct[1] === 'drops' && direct[2] === 'vel';
    return false;
  }
  const render = /\.(drops|puffs)\.render\.(aPosR|aPosSize|aVelS)\[(\d+)\]$/.exec(path);
  if (render) {
    const component = Number(render[3]) % 4;
    if (render[2] === 'aPosR' || render[2] === 'aPosSize') {
      if (component === 3) return false;
      return recipe === 'explosion' || recipe === 'slamWave' || (recipe === 'stormStart' && render[1] === 'puffs');
    }
    if (render[2] === 'aVelS')
      return render[1] === 'drops' && ['burst', 'explosion', 'slamLaunch', 'slamWave', 'formPop', 'dive', 'emerge'].includes(recipe);
  }
  return false;
}

function equalNumbers(expected, received, path) {
  if (Array.isArray(expected)) {
    assert.ok(Array.isArray(received), `${path}: expected array`);
    assert.equal(received.length, expected.length, `${path}: length`);
    for (let i = 0; i < expected.length; i++) equalNumbers(expected[i], received[i], `${path}[${i}]`);
    return;
  }
  if (expected && typeof expected === 'object') {
    assert.ok(received && typeof received === 'object', `${path}: expected object`);
    assert.deepEqual(Object.keys(received).sort(), Object.keys(expected).sort(), `${path}: keys`);
    for (const key of Object.keys(expected)) equalNumbers(expected[key], received[key], `${path}.${key}`);
    return;
  }
  if (typeof expected === 'number') {
    if (!/\[\d+\]$/.test(path)) {
      assert.equal(received, expected, path);
      return;
    }
    const receivedBits = orderedFloatBits(received), expectedBits = orderedFloatBits(expected);
    if (receivedBits === expectedBits) return;
    if (trigSensitive(path)) {
      const distance = Math.abs(receivedBits - expectedBits);
      assert.ok(distance <= 1, `${path}: Float32 ULP distance ${distance} (${received} != ${expected})`);
      if (distance > maxObservedTrigUlp) { maxObservedTrigUlp = distance; maxObservedTrigPath = path; }
      return;
    }
    assert.equal(receivedBits, expectedBits, path);
    return;
  }
  assert.equal(received, expected, path);
}

for (let i = 0; i < names.length; i++) {
  const name = names[i], sourceRecipe = fixture.recipes[name], native = actual[i];
  assert.equal(native.recipe, name);
  equalNumbers(sourceRecipe.immediatePools, native.immediatePools, `${name}.immediatePools`);
  equalNumbers(sourceRecipe.firstFramePools, native.firstFramePools, `${name}.firstFramePools`);
  equalNumbers(sourceRecipe.expired, native.expired, `${name}.expired`);
  for (const phase of ['immediatePools', 'firstFramePools']) {
    const counts = Object.fromEntries(['drops', 'puffs', 'glows', 'rings', 'shells', 'beams'].map(pool => [pool, native[phase][pool].count]));
    for (const [pool, count] of Object.entries(counts)) assert.equal(count, sourceRecipe[phase === 'immediatePools' ? 'immediate' : 'firstFrame'][pool], `${name}.${phase}.${pool} count`);
  }
}
console.log(`WORLD_FX source match ${names.length} recipes; exact Float32 values except trig-derived position/velocity vectors (observed max ${maxObservedTrigUlp} ULP${maxObservedTrigPath ? ` at ${maxObservedTrigPath}` : ''}; limit 1 ULP)`);
