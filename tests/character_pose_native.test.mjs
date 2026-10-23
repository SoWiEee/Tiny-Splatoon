import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { execFileSync } from 'node:child_process';
import { resolve } from 'node:path';

const root = resolve(import.meta.dirname, '..');
const executable = resolve(process.argv[2] ?? process.env.POSE_EVAL_EXE ?? resolve(root, 'build/character_pose_eval.exe'));
const oracle = JSON.parse(await readFile(resolve(root, process.argv[3] ?? 'tests/fixtures/character_pose_fixtures.json'), 'utf8'));
const firstDifferenceByScenario = new Map();
const rememberDifference = (scenario, message) => {
  if (!firstDifferenceByScenario.has(scenario)) firstDifferenceByScenario.set(scenario, message);
};
const close = (scenario, frame, path, actual, expected, tolerance = 0.001) => {
  if (!Number.isFinite(actual) || Math.abs(actual - expected) > tolerance)
    rememberDifference(scenario, `${scenario}/${frame} first difference at ${path}: native=${actual}, source=${expected}, error=${Math.abs(actual - expected)}`);
};
const quaternionSign = (actual, expected) => actual.reduce((dot, value, axis) => dot + value * expected[axis], 0) < 0 ? -1 : 1;
assert.equal(quaternionSign([0, 0, -1, 0], [0, 0, 1, 0]), -1);
assert.equal(quaternionSign([0, 0, 1, 0], [0, 0, 1, 0]), 1);
const quaternion = (scenario, frame, path, values, offset, expected) => {
  // q and -q encode the same rotation; skin matrices are still checked independently.
  const sign = quaternionSign(values.slice(offset, offset + 4), expected);
  for (let axis = 0; axis < 4; axis++) close(scenario, frame, `${path}[${axis}]`, sign * values[offset + axis], expected[axis]);
};
const vector3 = (value, fallback) => Array.isArray(value) ? value : value ? [value.x, value.y, value.z] : fallback;
const transformNames = ['root', 'model', 'kid', 'squid'];
const transformOffset = 0;
const channelOffset = transformNames.length * 11;
const boneOffset = channelOffset + 126;
const materialOffsetForBoneCount = boneCount => boneOffset + boneCount * 11;
const fullMaterialFields = [
  ['uTeam', 3], ['uShirt', 3], ['uShorts', 3], ['uShoe', 3], ['uSole', 3], ['uSock', 3], ['uStrap', 3],
  ['uIris', 3], ['uIris2', 3], ['uFlash', 3], ['uGlow', 3], ['uHurt', 4], ['uMouth', 4], ['uMouth2', 4],
  ['uLid', 4], ['uGaze', 4], ['uLook', 2], ['uWig', 3], ['uIwRimL', 3], ['uIwFill', 3],
  ['tankEmissive', 3], ['uIwRim', 4], ['uPattern', 1], ['uHurtSeed', 1], ['uTime', 1], ['uOpacity', 1],
  ['uFreckle', 1], ['uPupil', 1], ['coilEmissiveIntensity', 1],
];
const partStride = 29;

for (const scenario of oracle.scenarios) {
  const name = scenario.definition.name;
  const input = scenario.inputs.map(({ dt, position, yaw, state, events = [] }) => {
    const form = { kid: 0, squid: 1, swim: 2, climb: 3 }[state.form];
    const wall = vector3(state.wallNormal, [0, 0, 1]);
    const eventFields = events.flatMap(({ name: eventName, arg, direction, hand, side, duration }) => {
      const eventArg = typeof arg === 'object' && arg !== null ? arg : {};
      const eventDirection = vector3(direction ?? eventArg.direction, [eventArg.x ?? 0, eventArg.y ?? 0, eventArg.z ?? 1]);
      const amountSet = typeof arg === 'number' || Number.isFinite(eventArg.amount) || Number.isFinite(eventArg.amp);
      const amount = typeof arg === 'number' ? arg : eventArg.amount ?? eventArg.amp ?? 0;
      const directionSet = direction !== undefined || (typeof arg === 'object' && arg !== null);
      return [eventName, Number(amountSet), amount, Number(directionSet), ...eventDirection, eventArg.hand ?? hand ?? 0,
        eventArg.side ?? side ?? 1, eventArg.t ?? eventArg.dur ?? eventArg.duration ?? duration ?? 0.3];
    });
    return [dt, ...position, yaw, Number(state.grounded ?? true), form, state.speed ?? 0,
      state.localMove.x, state.localMove.z, Number(state.firing ?? false), state.charge ?? 0,
      state.aimPitch ?? 0, state.ink ?? 1, state.special ?? 0, Number(state.lowInk ?? false),
      Number(state.inEnemyInk ?? false), Number(state.invuln ?? false), Number(state.hp !== undefined), state.hp ?? 0,
      Number(state.subAim ?? false), Number(state.rolling ?? false), Number(state.visible ?? true),
      Number(state.vy !== undefined), state.vy ?? 0, Number(state.turnRate !== undefined), state.turnRate ?? 0,
      ...wall, events.length, ...eventFields, Number(state.subPropHidden ?? false)].join(' ');
  });
  let native;
  const partNames = Object.keys(scenario.samples[0].parts);
  for (const sample of scenario.samples)
    if (JSON.stringify(Object.keys(sample.parts)) !== JSON.stringify(partNames))
      rememberDifference(name, `${name}/${sample.frame} source part key order changed`);
  const team = scenario.samples[0].uniforms.uTeam.join(',');
  try {
    const definition = scenario.definition;
    const style = definition.style;
    const metadata = style ? [
      [style.hair, style.skin, style.outfit, style.eyes, style.hat, style.brows].join(','),
      [Number(definition.isLocal), definition.lifeLevel, ...(definition.cameraPosition ?? [])].join(','),
      definition.subId ?? 'bomb',
    ] : [];
    native = execFileSync(executable, [scenario.rig ? `variant-${name}` : 'pose-oracle', scenario.definition.weapon ?? 'shooter',
      scenario.definition.dance ?? '', String(scenario.definition.hurt ?? 0), String(scenario.definition.slope ?? 0),
      partNames.join('|'), team, ...metadata], {
      cwd: root, input: `${input.join('\n')}\n`, encoding: 'utf8', maxBuffer: 32 * 1024 * 1024, timeout: 30000,
    });
  } catch (error) {
    const diagnostic = [error.stderr, error.stdout].filter(Boolean).map(value => value.toString().trim()).filter(Boolean).join('\n');
    rememberDifference(name, `${name}/native evaluator failed: ${diagnostic || error.message}`);
    continue;
  }
  const output = native.trimEnd().split(/\r?\n/).map(row => row.split(',').map(Number));
  const boneNames = oracle.boneNames;
  for (const sample of scenario.samples) {
    const values = output[sample.frame - 1];
    const materialOffset = materialOffsetForBoneCount(boneNames.length);
    const skinMatrixOffset = materialOffset + 10;
    const fullMaterialOffset = skinMatrixOffset + boneNames.length * 16;
    const partOffset = fullMaterialOffset + 78 + 9 + 1;
    if (!values || values.length !== partOffset + partNames.length * partStride) {
      rememberDifference(name, `${name}/${sample.frame} native frame envelope: ${values?.length ?? 'missing'} values`);
      continue;
    }
    for (const [index, transformName] of transformNames.entries()) {
      const expected = sample[transformName], offset = transformOffset + index * 11;
      for (let axis = 0; axis < 3; axis++) close(name, sample.frame, `${transformName}.position[${axis}]`, values[offset + axis], expected.position[axis]);
      quaternion(name, sample.frame, `${transformName}.quaternion`, values, offset + 3, expected.quaternion);
      for (let axis = 0; axis < 3; axis++) close(name, sample.frame, `${transformName}.scale[${axis}]`, values[offset + 7 + axis], expected.scale[axis]);
      close(name, sample.frame, `${transformName}.visible`, values[offset + 10], Number(expected.visible));
    }
    for (let channel = 0; channel < sample.channels.length; channel++)
      close(name, sample.frame, `channels[${channel}]`, values[channelOffset + channel], sample.channels[channel]);
    for (let i = 0; i < 2; i++) close(name, sample.frame, `uLook[${i}]`, values[materialOffset + i], sample.uniforms.uLook[i]);
    for (let i = 0; i < 4; i++) close(name, sample.frame, `uGaze[${i}]`, values[materialOffset + 2 + i], sample.uniforms.uGaze[i]);
    for (let i = 0; i < 4; i++) close(name, sample.frame, `uLid[${i}]`, values[materialOffset + 6 + i], sample.uniforms.uLid[i]);
    for (const [bone, boneName] of boneNames.entries()) {
      const source = sample.bones[bone], offset = boneOffset + bone * 11;
      for (let axis = 0; axis < 3; axis++) close(name, sample.frame, `${boneName}.position[${axis}]`, values[offset + axis], source.position[axis]);
      quaternion(name, sample.frame, `${boneName}.quaternion`, values, offset + 3, source.quaternion);
      for (let axis = 0; axis < 3; axis++) close(name, sample.frame, `${boneName}.scale[${axis}]`, values[offset + 7 + axis], source.scale[axis]);
      close(name, sample.frame, `${boneName}.visible`, values[offset + 10], Number(source.visible));
      for (let component = 0; component < 16; component++)
        close(name, sample.frame, `${boneName}.skinMatrix[${component}]`, values[skinMatrixOffset + bone * 16 + component], sample.skinMatrices[bone][component]);
    }
    let materialIndex = 0;
    for (const [field, count] of fullMaterialFields) {
      const source = field === 'tankEmissive' ? sample.fillEmissive : field === 'coilEmissiveIntensity'
        ? [sample.coilEmissiveIntensity] : Array.isArray(sample.uniforms[field]) ? sample.uniforms[field] : [sample.uniforms[field]];
      for (let i = 0; i < count; i++)
        close(name, sample.frame, `uniforms.${field}[${i}]`, values[fullMaterialOffset + materialIndex++], source[i]);
    }
    for (let i = 0; i < 3; i++) close(name, sample.frame, `muzzle[${i}]`, values[fullMaterialOffset + 78 + i], sample.muzzle[i]);
    for (let i = 0; i < 3; i++) close(name, sample.frame, `muzzleLeft[${i}]`, values[fullMaterialOffset + 81 + i], sample.muzzleLeft[i]);
    for (let i = 0; i < 3; i++) close(name, sample.frame, `head[${i}]`, values[fullMaterialOffset + 84 + i], sample.head[i]);
    close(name, sample.frame, 'parts.count', values[fullMaterialOffset + 87], partNames.length);
    for (const [partIndex, partName] of partNames.entries()) {
      const source = sample.parts[partName], offset = partOffset + partIndex * partStride;
      close(name, sample.frame, `parts.${partName}.present`, values[offset], 1);
      for (let axis = 0; axis < 3; axis++) close(name, sample.frame, `parts.${partName}.position[${axis}]`, values[offset + 1 + axis], source.position[axis]);
      quaternion(name, sample.frame, `parts.${partName}.quaternion`, values, offset + 4, source.quaternion);
      for (let axis = 0; axis < 3; axis++) close(name, sample.frame, `parts.${partName}.scale[${axis}]`, values[offset + 8 + axis], source.scale[axis]);
      close(name, sample.frame, `parts.${partName}.visible`, values[offset + 11], Number(source.visible));
      close(name, sample.frame, `parts.${partName}.matrixOverride`, values[offset + 12], Number(Boolean(source.authoredMatrix)));
      if (source.authoredMatrix)
        for (let component = 0; component < 16; component++)
          close(name, sample.frame, `parts.${partName}.authoredMatrix[${component}]`, values[offset + 13 + component], source.authoredMatrix[component]);
    }
  }
}

assert.equal(firstDifferenceByScenario.size, 0,
  [...firstDifferenceByScenario.values()].join('\n'));
console.log(`${oracle.scenarios.length} source/native live pose scenarios passed at 0.001 tolerance`);
