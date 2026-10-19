import { readFile, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { resolve } from 'node:path';
import assert from 'node:assert/strict';

const root = resolve(import.meta.dirname, '..');
const args = process.argv.slice(2);
const check = args.includes('--check');
const outputArgs = args.filter(arg => arg !== '--check');
if (outputArgs.length > 1 || outputArgs[0]?.startsWith('--'))
  throw new Error('Usage: node tools/export-character-variant-fixtures.mjs [output.json] [--check]');
const outputArg = outputArgs[0];
const target = resolve(outputArg ?? resolve(root, 'tests/character_variant_fixtures.json'));

const { captureCharacterPose } = await import('./export-character-pose.mjs');
const THREE = await import('three');
const { Character } = await import('../splat-ref/src/game/character.js');
const { G } = await import('../splat-ref/src/core/ctx.js');
const { BONE_NAMES, BONE_PARENT, getRestPositions, getBoneInverses } = await import('../splat-ref/src/game/character-geo.js');
const STYLE = await import('../splat-ref/src/game/character-style.js');

const sourcePaths = [
  'splat-ref/src/config.js', 'splat-ref/src/core/ctx.js',
  'splat-ref/src/game/character.js', 'splat-ref/src/game/character-face.js',
  'splat-ref/src/game/character-geo.js', 'splat-ref/src/game/character-hair.js',
  'splat-ref/src/game/character-lod.js', 'splat-ref/src/game/character-mats.js',
  'splat-ref/src/game/character-outfit.js', 'splat-ref/src/game/character-style.js',
  'splat-ref/src/game/character-weapons.js',
];

async function sourceHashes() {
  const baseline = await readFile(resolve(root, 'docs/reference-baseline.sha256'), 'utf8');
  const hashes = {};
  for (const path of sourcePaths) {
    const hash = createHash('sha256').update(await readFile(resolve(root, path))).digest('hex');
    const entry = baseline.split(/\r?\n/).find(line => line.endsWith(`  ${path}`));
    if (!entry || entry.slice(0, 64).toLowerCase() !== hash) throw new Error(`Reference baseline mismatch: ${path}`);
    hashes[path] = hash;
  }
  return hashes;
}

function jsonValue(value) {
  if (value == null || typeof value !== 'object') return value;
  if (typeof value.toArray === 'function') return value.toArray();
  if (ArrayBuffer.isView(value) || Array.isArray(value)) return Array.from(value, jsonValue);
  return Object.fromEntries(Object.entries(value).map(([key, item]) => [key, jsonValue(item)]));
}

function planePhysics() {
  return {
    raycast(origin, direction, distance, out) {
      const t = Math.abs(direction.y) < 1e-12 ? -1 : -origin.y / direction.y;
      out.hit = t >= 0 && t <= distance;
      out.dist = out.hit ? t : distance;
      if (out.hit) { out.point.copy(origin).addScaledVector(direction, t); out.normal.set(0, 1, 0); }
      return out;
    },
  };
}

const selectedStyle = (i) => ({
  hair: i % STYLE.HAIR_STYLES,
  hat: i % STYLE.HATS.length,
  brows: i % STYLE.BROWS.length,
  skin: i % STYLE.SKIN_TONES.length,
  outfit: i % STYLE.OUTFITS.length,
  eyes: i % STYLE.IRIS.length,
});
const styleCases = Array.from({ length: 10 }, (_, i) => ({
  name: `appearance_${String(i).padStart(2, '0')}`,
  style: selectedStyle(i), isLocal: false, lodTier: 'game', cameraDistance: 4, lifeLevel: 2,
  weapon: 'shooter', frames: 60, kind: 'run', sampleFrames: [1, 15, 30, 60],
}));
const commonStyle = { hair: 7, hat: 1, brows: 3, skin: 8, outfit: 9, eyes: 7 };
const scenarios = [
  ...styleCases,
  { name: 'forms_local', style: commonStyle, isLocal: true, lodTier: 'game', cameraDistance: 5, lifeLevel: 2, weapon: 'shooter', frames: 90, kind: 'forms', sampleFrames: [1, 15, 30, 60, 90] },
  { name: 'forms_remote', style: commonStyle, isLocal: false, lodTier: 'game', cameraDistance: 5, lifeLevel: 2, weapon: 'shooter', frames: 90, kind: 'forms', sampleFrames: [1, 15, 30, 60, 90] },
  { name: 'lod_game_remote', style: commonStyle, isLocal: false, lodTier: 'game', cameraDistance: 3, lifeLevel: 2, weapon: 'shooter', frames: 60, kind: 'idle', sampleFrames: [1, 15, 30, 60] },
  { name: 'lod_far_remote', style: commonStyle, isLocal: false, lodTier: 'far', cameraDistance: 8, lifeLevel: 0, weapon: 'shooter', frames: 60, kind: 'idle', sampleFrames: [1, 15, 30, 60] },
  { name: 'weapon_near_remote', style: commonStyle, isLocal: false, lodTier: 'game', cameraDistance: 8, lifeLevel: 1, weapon: 'shooter', frames: 30, kind: 'aim', sampleFrames: [15, 30] },
  { name: 'weapon_far_remote', style: commonStyle, isLocal: false, lodTier: 'game', cameraDistance: 20, lifeLevel: 1, weapon: 'shooter', frames: 30, kind: 'aim', sampleFrames: [15, 30] },
  { name: 'weapon_far_local', style: commonStyle, isLocal: true, lodTier: 'game', cameraDistance: 20, lifeLevel: 1, weapon: 'shooter', frames: 30, kind: 'aim', sampleFrames: [15, 30] },
  { name: 'sub_visible_remote', style: commonStyle, isLocal: false, lodTier: 'game', cameraDistance: 5, lifeLevel: 2, weapon: 'shooter', subId: 'sticky', frames: 30, kind: 'sub', sampleFrames: [15, 30] },
  { name: 'sub_hidden_remote', style: commonStyle, isLocal: false, lodTier: 'game', cameraDistance: 5, lifeLevel: 2, weapon: 'shooter', subId: 'sticky', subPropHidden: true, frames: 30, kind: 'sub', sampleFrames: [15, 30] },
];

function inputFor(scenario, frame) {
  const dt = 1 / 60, t = frame * dt;
  const moving = scenario.kind === 'run';
  const state = { grounded: true, form: 'kid', localMove: { x: 0, z: moving ? 1 : 0 }, ink: 0.8, speed: moving ? 2 : 0 };
  if (scenario.kind === 'forms') {
    state.form = frame <= 15 ? 'kid' : frame <= 30 ? 'squid' : frame <= 60 ? 'swim' : 'climb';
    state.speed = state.form === 'kid' ? 0 : 1.8;
    state.localMove = { x: 0, z: state.speed ? 1 : 0 };
    if (state.form === 'climb') { state.vy = 1.8; state.wallNormal = new THREE.Vector3(0, 0, 1); }
  } else if (scenario.kind === 'aim') {
    state.firing = true; state.aimPitch = 0.3; state.charge = 0.65;
  } else if (scenario.kind === 'sub') {
    state.subAim = true;
    if (scenario.subPropHidden) state.subPropHidden = true;
  }
  const speed = state.speed;
  return { dt, position: [moving ? 0.25 * t : 0, 0, speed * t], yaw: 0.08 * Math.sin(t), state, events: [] };
}

function assertScenarios(data) {
  const byName = new Map(data.scenarios.map(scenario => [scenario.definition.name, scenario]));
  const at = (name, frame) => {
    const sample = byName.get(name)?.samples.find(candidate => candidate.frame === frame);
    assert.ok(sample, `Missing ${name}/${frame}`);
    return sample;
  };
  assert.equal(byName.get('lod_game_remote').definition.lodTier, 'game');
  assert.equal(byName.get('lod_far_remote').definition.lodTier, 'far');
  assert.equal(at('weapon_near_remote', 30).parts['weapon/right/body'].visible, true);
  assert.equal(at('weapon_far_remote', 30).parts['weapon/right/bodyFar'].visible, true);
  assert.equal(at('weapon_far_local', 30).parts['weapon/right/body'].visible, true);
  assert.equal(at('sub_visible_remote', 30).parts['sub/group'].visible, true);
  assert.equal(at('sub_hidden_remote', 30).parts['sub/group'].visible, false);
  for (const frame of [60, 90]) {
    const form = frame === 60 ? 'swim' : 'climb';
    assert.equal(at('forms_local', frame).state.form, form);
    assert.equal(at('forms_remote', frame).state.form, form);
    assert.equal(at('forms_local', frame).parts['squid/0/1'].visible, true, `local ${form} ghost`);
    assert.equal(at('forms_remote', frame).parts['squid/0/1'].visible, false, `remote ${form} ghost`);
    assert.equal(at('forms_remote', frame).parts['squid/0/3'].visible, false, `remote ${form} eyes`);
  }
  for (const scenario of data.scenarios) {
    assert.equal(scenario.rig.rest.length, data.boneNames.length, `${scenario.definition.name} rest rig`);
    assert.equal(scenario.rig.inverseBind.length, data.boneNames.length, `${scenario.definition.name} inverse bind`);
    for (const sample of scenario.samples) {
      assert.equal(sample.bones.length, data.boneNames.length, `${scenario.definition.name}/${sample.frame} bones`);
      assert.ok(sample.skinMatrices.flat().every(Number.isFinite), `${scenario.definition.name}/${sample.frame} skin matrices`);
    }
  }
}

async function exportFixtures() {
  const hashes = await sourceHashes();
  const result = {
    version: 1,
    generator: 'actual Character.update; source styles, forms, local/remote and LOD; no source patches',
    hashes,
    boneNames: BONE_NAMES,
    parents: BONE_NAMES.map(name => BONE_NAMES.indexOf(BONE_PARENT[name])),
    matrixConvention: 'column-major; skin = inverse(kid.matrixWorld) * bone.matrixWorld * inverseBind',
    styleCounts: { hair: STYLE.HAIR_STYLES, hat: STYLE.HATS.length, brows: STYLE.BROWS.length, skin: STYLE.SKIN_TONES.length, outfit: STYLE.OUTFITS.length, eyes: STYLE.IRIS.length },
    scenarios: [],
  };
  const saved = { scene: G.scene, physics: G.physics, actors: G.actors, mode: G.mode, camera: G.camera, local: G.local };
  const savedInnerHeight = globalThis.innerHeight;
  try {
    globalThis.innerHeight = 900;
    G.actors = []; G.mode = 'match'; G.local = null;
    for (const scenario of scenarios) {
      G.scene = new THREE.Scene(); G.physics = planePhysics();
      G.camera = new THREE.PerspectiveCamera(60, 16 / 9, 0.1, 100);
      G.camera.position.set(0, 2, scenario.cameraDistance); G.camera.lookAt(0, 1, 0); G.camera.updateMatrixWorld(true);
      const character = new Character({ name: `variant-${scenario.name}`, style: scenario.style, weapon: scenario.weapon, isLocal: scenario.isLocal });
      character.setLod(scenario.lodTier);
      character.setSub(scenario.subId ?? 'bomb');
      G.scene.add(character.root);
      const style = { ...character.style };
      const rest = getRestPositions(style), inverseBind = getBoneInverses(style);
      const rig = { rest: BONE_NAMES.map(name => rest[name].toArray()), inverseBind: inverseBind.map(matrix => matrix.toArray()) };
      const inputs = [], samples = [];
      try {
        for (let frame = 1; frame <= scenario.frames; frame++) {
          const input = inputFor(scenario, frame);
          inputs.push(jsonValue(input));
          character.root.position.fromArray(input.position); character.root.rotation.y = input.yaw;
          character.subPropHidden = Boolean(input.state.subPropHidden);
          for (const event of input.events) character.trigger(event.name, event.arg);
          character.update(input.dt, input.state);
          if (scenario.sampleFrames.includes(frame)) {
            assert.equal(character.lodTier, scenario.lodTier, `${scenario.name}/${frame} LOD tier`);
            assert.equal(character.lifeLv, scenario.lifeLevel, `${scenario.name}/${frame} life level`);
            samples.push(captureCharacterPose(character, frame));
          }
        }
      } finally { character.dispose(); }
      result.scenarios.push({
        definition: {
          name: scenario.name, style, subId: scenario.subId ?? 'bomb', weapon: scenario.weapon,
          isLocal: scenario.isLocal, lodTier: scenario.lodTier,
          cameraPosition: [0, 2, scenario.cameraDistance], lifeLevel: scenario.lifeLevel,
        },
        rig, inputs, samples,
      });
    }
  } finally {
    Object.assign(G, saved);
    if (savedInnerHeight === undefined) delete globalThis.innerHeight;
    else globalThis.innerHeight = savedInnerHeight;
  }
  const styles = result.scenarios.filter(scenario => scenario.definition.name.startsWith('appearance_')).map(scenario => scenario.definition.style);
  for (const key of Object.keys(result.styleCounts)) {
    const got = new Set(styles.map(style => style[key]));
    if (got.size !== result.styleCounts[key] || [...Array(result.styleCounts[key]).keys()].some(value => !got.has(value)))
      throw new Error(`Appearance scenarios do not cover every ${key} variant`);
  }
  assertScenarios(result);
  return result;
}

const data = await exportFixtures();
const text = JSON.stringify(data) + '\n';
if (check) {
  const expected = await readFile(target, 'utf8');
  if (text !== expected) throw new Error(`Stale source character variant fixtures: ${target}`);
} else await writeFile(target, text);
console.log(`${check ? 'Verified' : 'Exported'} ${data.scenarios.length} source character variants, ${data.scenarios.reduce((count, scenario) => count + scenario.samples.length, 0)} samples, ${Buffer.byteLength(text)} bytes -> ${target}`);
