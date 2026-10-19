import { createHash } from 'node:crypto';
import { readFileSync, writeFileSync } from 'node:fs';
import { register } from 'node:module';
register(new URL('./reference-loader.mjs', import.meta.url));
const { WEAPONS } = await import('../splat-ref/src/config.js');

const weapons = {
  bow: ['../splat-ref/src/game/kits/bow.js'],
  blade: ['../splat-ref/src/game/kits/blade.js'],
  mitts: ['../splat-ref/src/game/kits/mitts.js'],
};
const configPath = '../splat-ref/src/config.js';
const hash = (text) => createHash('sha256').update(text).digest('hex');
const flatten = (value, prefix = '', out = []) => {
  if (typeof value === 'number' && Number.isFinite(value)) out.push([prefix, value]);
  else if (Array.isArray(value)) value.forEach((child, index) => flatten(child, `${prefix}.${index}`, out));
  else if (value && typeof value === 'object')
    for (const [key, child] of Object.entries(value)) flatten(child, prefix ? `${prefix}.${key}` : key, out);
  return out;
};
const fieldName = (path) => path.replace(/[^a-zA-Z0-9]+(.)/g, (_, c) => c.toUpperCase());
const number = (v) => Number(v).toPrecision(17);

const lines = [
  '// Generated from splat-ref/src/config.js and game/kits/{bow,blade,mitts}.js. Do not edit by hand.',
  '#pragma once',
  'namespace restoration::test {',
];
for (const [id, kitPaths] of Object.entries(weapons)) {
  const sourcePaths = [configPath, ...kitPaths];
  const sources = sourcePaths.map((path) => readFileSync(new URL(path, import.meta.url), 'utf8'));
  const values = flatten(WEAPONS[id]).filter(([path]) => path !== 'id' && path !== 'name' && path !== 'kind');
  const fields = values.map(([path]) => fieldName(path));
  if (new Set(fields).size !== fields.length) throw new Error(`Duplicate ${id} fixture field name`);
  lines.push(`struct ${id[0].toUpperCase()}${id.slice(1)}SourceFixture {`);
  for (let i = 0; i < values.length; i += 4)
    lines.push(`  double ${values.slice(i, i + 4).map(([path]) => fieldName(path)).join(', ')};`);
  lines.push('};');
  lines.push(`inline constexpr ${id[0].toUpperCase()}${id.slice(1)}SourceFixture ${id}SourceFixture{`);
  for (let i = 0; i < values.length; i += 4)
    lines.push(`  ${values.slice(i, i + 4).map(([, value]) => number(value)).join(', ')}${i + 4 < values.length ? ',' : ''}`);
  lines.push('};');
  sources.forEach((source, index) => lines.push(`inline constexpr char ${id}SourceHash${index}[] = "${hash(source)}";`));
}
lines.push('}', '');
const output = lines.join('\n');
const target = new URL('../tests/main_weapon_source_fixtures.hpp', import.meta.url);
if (process.argv.includes('--check')) {
  if (readFileSync(target, 'utf8') !== output) throw new Error('Main weapon source fixtures differ from locked source');
} else writeFileSync(target, output);
console.log('Locked Bow, Blade, and Mitts source tuning fixtures');
