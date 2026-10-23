import { spawnSync } from 'node:child_process';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { resolve } from 'node:path';

const root=resolve(import.meta.dirname,'..');
const executable=process.argv[2];
const fixture=JSON.parse(await readFile(resolve(root,'tests/fixtures/character_lod_fixtures.json'),'utf8'));
assert.equal(fixture.version,1,'Unsupported Character LOD fixture version');
assert.equal(fixture.source.path,'splat-ref/src/game/character-lod.js','fixture source attribution');
assert.equal(fixture.source.sha256,'47e1f70ed071cdf1d442ebad6b460b0f4a3140f518b69e2e80ab672915ec1dc2','immutable Character LOD source hash');
assert.deepEqual(fixture.qualities.map(row=>row.name),['low','medium','high','ultra'],
  'Character LOD fixture must cover every quality tier');
assert.equal(fixture.updateCount,2288,'Character LOD fixture must cover all 2,288 source updates');
assert.equal(fixture.scenarios.length,8,'Character LOD fixture must cover local and remote actors at every quality');

// Source regeneration and baseline verification live in the optional exporter check.

const input=[];
const expected=[];
const labels=[];
for(const scenario of fixture.scenarios) {
  const q=fixture.qualities.findIndex(quality=>quality.name===scenario.quality);
  assert.notEqual(q,-1,`Unknown quality in fixture scenario ${scenario.name}`);
  input.push('-1 0 0 0');
  for(let step=0;step<scenario.updates.length;step++) {
    const [pixels,dt,tier,target,fade,transition]=scenario.updates[step];
    input.push(`${q} ${Number(scenario.local)} ${pixels} ${dt}`);
    expected.push([tier,target,fade,transition]);
    labels.push(`${scenario.name}/${step}`);
  }
}
assert.equal(expected.length,fixture.updateCount,'Character LOD fixture update count did not match its rows');
const result=spawnSync(executable,[],{input:input.join('\n')+'\n',encoding:'utf8'});
assert.equal(result.status,0,result.stderr);
const actual=result.stdout.trim().split(/\r?\n/);
assert.equal(actual.length,expected.length);
for(let i=0;i<actual.length;i++) {
  const row=actual[i].trim().split(/\s+/).map(Number);
  const want=expected[i];
  assert.equal(row[0],want[0],`tier at ${labels[i]}`);
  assert.equal(row[1],want[1],`target at ${labels[i]}`);
  assert.ok(Math.abs(row[2]-want[2])<1e-5,`fade at ${labels[i]}`);
  assert.equal(row[3],want[3],`transition at ${labels[i]}`);
}
console.log(`Character LOD fixture oracle: ${actual.length} recorded source updates across all quality tiers`);
