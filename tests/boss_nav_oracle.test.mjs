import { readFile } from 'node:fs/promises';
import { execFileSync } from 'node:child_process';
import assert from 'node:assert/strict';

const expected = JSON.parse(await readFile(new URL('./fixtures/boss_nav_fixtures.json', import.meta.url), 'utf8'));
const rows = execFileSync(process.argv[2], [], { encoding: 'utf8', timeout: 600000 }).trim().split(/\r?\n/);
const near = (got, want, label) => assert.ok(Math.abs(got - want) < 0.00002, `${label}: ${got} != ${want}`);
let line = 0, checks = 0;
for (const stage of expected) {
  const head = rows[line++].split(' ');
  assert.deepEqual(head.slice(0, 2), ['STAGE', stage.id]);
  near(+head[2], stage.floorY, `${stage.id} floorY`);
  assert.equal(+head[3], stage.plan, `${stage.id} plan cells`);
  for (const probe of stage.probes) {
    const row = rows[line++].split(' ');
    assert.equal(row[0], 'PROBE');
    assert.equal(+row[1], probe.cell, `${stage.id} probe cell`);
    for (const [column, key] of [[2, 'x'], [3, 'z'], [4, 'wall'], [5, 'floor'], [6, 'y']]) near(+row[column], probe[key], `${stage.id} ${key}`);
    assert.equal(+row[7], +probe.pose, `${stage.id} pose`);
    assert.equal(+row[8], +probe.turn, `${stage.id} turn`);
    near(+row[9], probe.cast, `${stage.id} cast`);
    checks += 9;
  }
  const path = rows[line++].split(' ');
  assert.equal(path[0], 'PATH');
  assert.equal(+path[1], stage.path.length, `${stage.id} path length`);
  for (let i = 0; i < stage.path.length; i++) {
    near(+path[i * 2 + 2], stage.path[i][0], `${stage.id} path ${i} x`);
    near(+path[i * 2 + 3], stage.path[i][1], `${stage.id} path ${i} z`);
    checks += 2;
  }
}
assert.equal(line, rows.length);
console.log(`BossNav ${checks} source observations match across ${expected.length} stages`);
