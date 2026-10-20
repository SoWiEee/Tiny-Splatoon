import test from 'node:test';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { resolve } from 'node:path';

const executable = resolve(process.argv[2] ?? 'build/Tiny-Splatoon.exe');
for (const args of [
  ['--reference-match', 'tidewater'],
  ['--reference-boss', 'tidewater'],
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

for (const [args, message] of [
  [['--reference-boss', 'tidewater', '--duration', '120'], /Boss duration must be/],
  [['--reference-boss', 'tidewater', '--duration', '240junk'], /Boss duration must be/],
  [['--reference-match', 'tidewater', '--duration', '240'], /--duration requires a Boss match/],
  [['--reference-boss', 'tidewater', '--capture', 'unused.png'], /cannot be combined/],
  [['--reference-match', 'tidewater', '--ui-capture', 'unused.png'], /--ui-capture requires the native menu/],
]) {
  test(`invalid Boss options fail before creating a window: ${args.join(' ')}`, () => {
    const result = spawnSync(executable, args, { encoding: 'utf8', timeout: 10000 });
    assert.ifError(result.error);
    assert.equal(result.status, 1, result.stdout + result.stderr);
    assert.match(result.stderr, message);
    assert.doesNotMatch(result.stdout, /GPU /);
  });
}

for (const args of [
  ['--capture', 'unused.png'],
  ['--paint-probe', '0'],
  ['--benchmark-report', 'unused.json'],
]) {
  test(`default native menu requires an explicit diagnostic entry for ${args[0]}`, () => {
    const result = spawnSync(executable, args, { encoding: 'utf8', timeout: 10000 });
    assert.ifError(result.error);
    assert.equal(result.status, 1, result.stdout + result.stderr);
    assert.match(result.stderr, /--native cannot be combined/);
    assert.doesNotMatch(result.stdout, /GPU /);
  });
}
