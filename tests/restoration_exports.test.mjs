import test from 'node:test';
import { execFileSync } from 'node:child_process';
import { resolve } from 'node:path';

const root = resolve(import.meta.dirname, '..');
for (const exporter of [
  'arsenal', 'character-assets', 'character-render-data', 'character-shaders',
  'character-weapon-rigs', 'character-render-fixtures', 'ai-nav-fixtures', 'ai-bot-oracles',
  'brolly-source-fixtures', 'main-weapon-source-fixtures', 'main-kit-source-fixtures',
  'specials-source-fixtures', 'sonar-wail-source-fixtures', 'kraken-source-fixtures',
  'special-prop-data', 'remaining-specials-source-fixtures',
  'bot-special-control-oracles', 'character-variant-fixtures',
]) {
  test(`${exporter} matches its actual-source export`, { timeout: 120000 }, () => {
    execFileSync(process.execPath, [`tools/export-${exporter}.mjs`, '--check'], {
      cwd: root, timeout: 120000, stdio: 'pipe',
    });
  });
}
for (const oracle of ['combat-source-oracle', 'combat-kit-source-oracle']) {
  test(`${oracle} matches its actual-source export`, { timeout: 120000 }, () => {
    execFileSync(process.execPath, [`tests/${oracle}.mjs`, '--check'], {
      cwd: root, timeout: 120000, stdio: 'pipe',
    });
  });
}
