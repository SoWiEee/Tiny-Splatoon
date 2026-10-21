import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { exportCharacterPose } from '../tools/export-character-pose.mjs';

test('actual Character oracle reproduces the checked-in pose and material samples', { timeout: 120000 }, async () => {
  const expected = await readFile(new URL('./character_pose_fixtures.json', import.meta.url), 'utf8');
  const actual = JSON.stringify(await exportCharacterPose()) + '\n';
  const hash = s => createHash('sha256').update(s).digest('hex');
  assert.equal(hash(actual), hash(expected), 'Source Character outputs changed; inspect the first pose divergence before replacing fixtures');
});

test('fixture envelope preserves the complete rig, live face/hair, forms, and all main weapons', async () => {
  const data = JSON.parse(await readFile(new URL('./character_pose_fixtures.json', import.meta.url), 'utf8'));
  assert.equal(data.boneNames.length, 87);
  assert.equal(new Set(data.boneNames).size, 87);
  assert.equal(data.scenarios.filter(s => s.definition.weapon).length, 12);
  const idle = data.scenarios.find(s => s.definition.name === 'idle').samples;
  assert.notDeepEqual(idle[0].hair, idle.at(-1).hair, 'Hair must remain a live spring state');
  assert.notDeepEqual(idle[0].uniforms.uGaze, idle.at(-1).uniforms.uGaze, 'Gaze must remain live');
  const forms = data.scenarios.find(s => s.definition.name === 'form_cycle').samples;
  assert.deepEqual(new Set(forms.map(s => s.state.form)), new Set(['kid', 'squid', 'swim', 'climb']));
  for (const form of ['squid', 'swim', 'climb']) {
    const sample = forms.find(s => s.state.form === form);
    assert.ok(sample.squid.visible, `${form}: source squid must be rendered`);
    assert.equal(sample.kid.visible, false, `${form}: kid must be hidden after transformation`);
  }
  for (const weapon of ['roller', 'brush', 'bucket', 'blade']) {
    const samples = data.scenarios.find(s => s.definition.name === `weapon_${weapon}`).samples;
    assert.notDeepEqual(samples[0].bones, samples.at(-1).bones, `${weapon}: splashing must animate the rig`);
  }
  for (const scenario of data.scenarios) for (const sample of scenario.samples) {
    assert.equal(sample.bones.length, 87);
    assert.equal(sample.skinMatrices.length, 87);
    assert.equal(sample.channels.length, 126);
    for (const matrix of sample.skinMatrices) {
      assert.equal(matrix.length, 16);
      assert.ok(matrix.every(Number.isFinite), `${scenario.definition.name}/${sample.frame}: nonfinite skin matrix`);
    }
    assert.equal(sample.uniforms.uLid.length, 4);
    assert.equal(sample.uniforms.uMouth2.length, 4);
    assert.ok(sample.parts['weapon/right/pivot']);
  }
});
