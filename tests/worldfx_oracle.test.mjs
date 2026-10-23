import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { spawnSync } from 'node:child_process';

const fixture = JSON.parse(readFileSync(new URL('./fixtures/worldfx_source_fixtures.json', import.meta.url), 'utf8'));
assert.equal(fixture.source, 'splat-ref/src/fx/fx.js', 'fixture source path');
assert.equal(fixture.sha256, '9e8a646a6154824b64f9b35eeab1e4347f3bdb282ff7c337645dca14f8b88bc8', 'immutable source fx.js hash');
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
    if (['muzzleBlaster', 'muzzleCharger', 'beamImpact', 'dodgeSplash', 'dodgePlant'].includes(recipe)) return direct[1] === 'drops' && direct[2] === 'vel';
    if (recipe === 'dodgeSkid') return direct[1] === 'drops' && ['pos', 'vel'].includes(direct[2]);
    if (['flickCurtain', 'bounceSplash', 'sloshImpact', 'waterPlop'].includes(recipe)) return direct[1] === 'drops' && ['pos', 'vel'].includes(direct[2]);
    if (['spinUp', 'spinStream', 'spinFull'].includes(recipe)) return direct[1] === 'drops' && ['pos', 'vel'].includes(direct[2]);
    if (recipe === 'superJumpLand') return ['drops', 'puffs'].includes(direct[1]) && ['pos', 'vel'].includes(direct[2]);
    return false;
  }
  const directSprite = /\.(glows|puffs)\.(pos|vel)\[(\d+)\]$/.exec(path);
  if (directSprite)
    return (['chargeGlow', 'chargeFull'].includes(recipe) && directSprite[1] === 'glows')
      || (recipe === 'flickCurtain' && directSprite[1] === 'puffs');
  const render = /\.(drops|puffs|glows)\.render\.(aPosR|aPosSize|aVelS)\[(\d+)\]$/.exec(path);
  if (render) {
    const component = Number(render[3]) % 4;
    if (render[2] === 'aPosR' || render[2] === 'aPosSize') {
      if (component === 3) return false;
      return recipe === 'explosion' || recipe === 'slamWave' || (recipe === 'stormStart' && render[1] === 'puffs')
        || (recipe === 'flickCurtain' && render[1] === 'drops')
        || (recipe === 'flickCurtain' && render[1] === 'puffs')
        || (['chargeGlow', 'chargeFull'].includes(recipe) && render[1] === 'glows')
        || (recipe === 'superJumpLand' && render[1] === 'puffs')
        || (recipe === 'waterPlop' && render[1] === 'drops')
        || (recipe === 'dodgeSkid' && render[1] === 'drops');
    }
    if (render[2] === 'aVelS')
      return render[1] === 'drops' && ['burst', 'explosion', 'slamLaunch', 'slamWave', 'formPop', 'dive', 'emerge', 'muzzleBlaster', 'muzzleCharger', 'flickCurtain', 'bounceSplash', 'sloshImpact', 'beamImpact', 'spinUp', 'spinStream', 'spinFull', 'waterPlop', 'dodgeSplash', 'dodgeSkid', 'dodgePlant'].includes(recipe);
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
console.log(`WORLD_FX committed source fixture match ${names.length} recipes; exact Float32 values except trig-derived position/velocity vectors (observed max ${maxObservedTrigUlp} ULP${maxObservedTrigPath ? ` at ${maxObservedTrigPath}` : ''}; limit 1 ULP)`);
