import { createHash } from 'node:crypto';
import { readFileSync, writeFileSync } from 'node:fs';
import { register } from 'node:module';
register(new URL('../tools/reference-loader.mjs', import.meta.url));
const { WEAPONS } = await import('../splat-ref/src/config.js');

const sourcePaths = [
  '../splat-ref/src/config.js',
  '../splat-ref/src/game/kits/brolly.js',
  '../splat-ref/src/game/kits/brolly-model.js',
];
const sources = sourcePaths.map((path) => readFileSync(new URL(path, import.meta.url), 'utf8'));
const [configSource, brollySource, modelSource] = sources;
const number = (text, pattern, label) => {
  const match = text.match(pattern);
  if (!match) throw new Error(`Could not extract source value: ${label}`);
  return Number(match[1]);
};
const heldMatch = brollySource.match(/const HELD\s*=\s*\{([^}]+)\}/);
if (!heldMatch) throw new Error('Could not extract HELD descriptor');
const held = Object.fromEntries([...heldMatch[1].matchAll(/(\w+)\s*:\s*(-?\d+(?:\.\d+)?)/g)].map((m) => [m[1], Number(m[2])]));
for (const key of ['up', 'fwd', 'r', 'pitchMin', 'pitchMax']) if (!(key in held)) throw new Error(`Missing HELD.${key}`);
const openThreshold = number(brollySource, /k\.open\s*<\s*([\d.]+)\s*\|\|\s*k\.grow\s*<\s*[\d.]+/, 'held open threshold');
const growThreshold = number(brollySource, /k\.open\s*<\s*[\d.]+\s*\|\|\s*k\.grow\s*<\s*([\d.]+)/, 'held grow threshold');
const launchH = number(brollySource, /const LAUNCH_H\s*=\s*([\d.]+)/, 'launched canopy crown height');
const launchR = number(brollySource, /LAUNCH_R\s*=\s*([\d.]+)/, 'launched canopy radius');
const thetaDegrees = number(modelSource, /const TH\s*=\s*([\d.]+)\s*\*\s*Math\.PI\s*\/\s*180/, 'canopy dome angle');
const rimR = number(modelSource, /export const RIM_R\s*=\s*([\d.]+)/, 'canopy rim radius');
const theta = thetaDegrees * Math.PI / 180;
const depth = rimR / Math.sin(theta) * (1 - Math.cos(theta));
const config = WEAPONS.brolly;
const fields = ['canopyHp', 'openDelay', 'openTime', 'closeTime', 'launchHold', 'regrowTime', 'canopyRegen', 'canopyRegenDelay', 'launchSpeed', 'launchLife', 'launchPaint'];
for (const key of fields) if (!Number.isFinite(config[key])) throw new Error(`Missing WEAPONS.brolly.${key}`);
const n = (v) => Number(v).toPrecision(17);
const hashes = sources.map((source) => createHash('sha256').update(source).digest('hex'));
const output = [
  '// Generated from splat-ref/src/config.js and game/kits/brolly*.js. Do not edit by hand.',
  '#pragma once',
  'namespace restoration::test {',
  'struct BrollySourceFixture { double canopyHp,openDelay,openTime,closeTime,launchHold,regrowTime,canopyRegen,canopyRegenDelay,launchSpeed,launchLife,launchPaint; double openThreshold,growThreshold,heldUp,heldForward,heldRadius,pitchMin,pitchMax,launchH,launchR,planeOffset; };',
  `inline constexpr BrollySourceFixture brollySourceFixture{${[...fields.map((key) => config[key]), openThreshold, growThreshold, held.up, held.fwd, held.r, held.pitchMin, held.pitchMax, launchH, launchR, -depth / 2].map(n).join(',')}};`,
  ...hashes.map((hash, i) => `inline constexpr char brollySourceHash${i}[] = "${hash}";`),
  '}',
  '',
].join('\n');
const path = new URL('../tests/brolly_source_fixtures.hpp', import.meta.url);
if (process.argv.includes('--check')) {
  if (readFileSync(path, 'utf8') !== output) throw new Error('Brolly source fixtures differ from locked source');
} else writeFileSync(path, output);
console.log('Locked Brolly source config and shield geometry fixtures');
