import test from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import { resolve } from 'node:path';
import { register } from 'node:module';

const root = resolve(import.meta.dirname, '..');

test('loader resolves the vendored Three.js addon path', async () => {
  register(new URL('../tools/reference-loader.mjs', import.meta.url));
  const addon = await import('three/addons/controls/OrbitControls.js');
  assert.equal(typeof addon.OrbitControls, 'function');
});

test('reference export matches locked source and generated artifacts', () => {
  execFileSync(process.execPath, ['tools/export-reference.mjs', '--check'], { cwd: root, timeout: 120000 });
  const content = JSON.parse(readFileSync(resolve(root, 'assets/reference/content.json'), 'utf8'));
  assert.equal(content.schema, 2);
  assert.equal(content.stages.length, 23);
  assert.equal(content.stages.filter((stage) => stage.mode === 'boss').length, 7);
  assert.ok(!content.stages.some((stage) => stage.id === 'cargo' && stage.mode === 'boss'));
  assert.ok(content.stages.every((stage) => stage.blocks.length > 0 && stage.faces.length > 0));
  assert.ok(content.stages.every((stage) => stage.propColliderCount > 0 &&
    stage.propColliders.length === stage.propColliderCount &&
    stage.blocks.length === stage.layoutBlockCount + stage.propColliderCount));
  assert.ok(content.stages.every((stage) => stage.blocks.slice(stage.layoutBlockCount).every((block) =>
    block.hidden && block.faces.every((face) => face === -1))));
  assert.ok(content.stages.some((stage) => stage.propColliders.some((collider) => collider.obox)));
  assert.ok(content.stages.some((stage) => stage.propColliders.some((collider) => collider.rail)));
  assert.ok(content.stages.some((stage) => stage.propColliders.some((collider) => collider.roof)));
  assert.ok(content.layouts.halyard.single.some((piece) => piece.tag === 'ferry-deck'));
  assert.equal(content.config.WEAPON_ORDER.length, 12);
  assert.equal(content.config.SUB_ORDER.length, 15);
  assert.equal(content.config.SPECIAL_ORDER.length, 19);
  assert.match(content.config.weaponRange.$function, /weaponRange/);
  assert.match(content.config.PROGRESSION.xpForLevel.$function, /800 \+ lvl \* 350/);
  const cpp = readFileSync(resolve(root, 'src/world/ReferenceData.cpp'), 'utf8');
  assert.equal((cpp.match(/static const BlockData stage\d+Blocks\[\]/g) || []).length, content.stages.length);
  assert.equal((cpp.match(/static const FaceData stage\d+Faces\[\]/g) || []).length, content.stages.length);
  assert.match(cpp, /std::vector<BlockData>\(std::begin\(stage0Blocks\), std::end\(stage0Blocks\)\)/);
  const turf = content.stages.find((stage) => stage.id === 'halyard' && stage.mode === 'turf');
  const zones = content.stages.find((stage) => stage.id === 'halyard' && stage.mode === 'zones');
  assert.notEqual(turf.blocks.length, zones.blocks.length);
  assert.equal(turf.propColliderCount, 265);
  assert.equal(zones.propColliderCount, 297);
  const cargo = content.stages.find((stage) => stage.id === 'cargo' && stage.mode === 'turf');
  assert.ok(cargo.propColliderCount > 400);
  const fixtures = readFileSync(resolve(root, 'tests/reference_fixtures.hpp'), 'utf8');
  assert.match(fixtures, /std::array<GroundFixture, 851>/);
  assert.match(fixtures, /std::array<RayFixture, 920>/);
});

test('controller, actor and camera fixtures match the locked reference modules', () => {
  for (const subject of ['controller', 'player', 'camera'])
    execFileSync(process.execPath, [`tools/export-${subject}-fixtures.mjs`, '--check'], { cwd: root, timeout: 120000 });
});
