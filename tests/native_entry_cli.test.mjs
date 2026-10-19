import test from 'node:test';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { resolve } from 'node:path';

const executable = resolve(process.argv[2] ?? 'build/Tiny-Splatoon.exe');
for (const args of [
  ['--reference-match', 'tidewater'],
  ['--reference-game', 'tidewater'],
  ['--paint-probe', '0'],
  ['--play-probe', '0'],
  ['--capture', 'unused.png'],
  ['--benchmark-report', 'unused.json'],
  ['--benchmark-warmup', '0'],
  ['--benchmark-frames', '1'],
]) {
  test(`native menu rejects conflicting ${args[0]} before creating a window`, () => {
    const result = spawnSync(executable, ['--native', ...args], { encoding: 'utf8', timeout: 10000 });
    assert.ifError(result.error);
    assert.equal(result.status, 1, result.stdout + result.stderr);
    assert.match(result.stderr, /--native cannot be combined/);
    assert.doesNotMatch(result.stdout, /GPU /);
  });
}
