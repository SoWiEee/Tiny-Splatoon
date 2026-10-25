import test from 'node:test';
import { execFileSync } from 'node:child_process';
import { resolve } from 'node:path';

const root = resolve(import.meta.dirname, '..');
for (const oracle of ['combat-source-oracle', 'combat-kit-source-oracle']) {
  test(`${oracle} matches its actual-source export`, { timeout: 120000 }, () => {
    execFileSync(process.execPath, [`tests/${oracle}.mjs`, '--check'], {
      cwd: root, timeout: 120000, stdio: 'pipe',
    });
  });
}
