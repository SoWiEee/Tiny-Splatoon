import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, readdirSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve, dirname } from 'node:path';
import { execFileSync } from 'node:child_process';

test('desktop post shaders reproduce the locked renderer and Three.js sources',()=>{
  const output=mkdtempSync(join(tmpdir(),'tiny-post-'));
  try{
    execFileSync(process.execPath,['tools/export-post-shaders.mjs',output],{cwd:resolve(import.meta.dirname,'..'),timeout:60000});
    const expected=resolve(import.meta.dirname,'../assets/reference/post');
    assert.deepEqual(readdirSync(output).sort(),readdirSync(expected).sort());
    for(const name of readdirSync(output))assert.deepEqual(readFileSync(join(output,name)),readFileSync(join(expected,name)),name);
  }finally{assert.equal(dirname(resolve(output)),resolve(tmpdir()));rmSync(output,{recursive:true,force:true});}
});

test('shadow character materials retain source hooks and stable hardware PCF',()=>{
  execFileSync(process.execPath,['tools/export-character-shadow-shaders.mjs','--check'],{cwd:resolve(import.meta.dirname,'..'),timeout:60000});
  const path=resolve(import.meta.dirname,'../assets/reference/characters/shaders-shadow');
  const manifest=JSON.parse(readFileSync(join(path,'manifest.json'),'utf8'));
  assert.equal(manifest.entries.length,23);
  const skin=readFileSync(join(path,'skin.frag'),'utf8');
  assert.match(skin,/#define USE_SHADOWMAP/);assert.match(skin,/#define SHADOWMAP_TYPE_PCF/);assert.match(skin,/float s9 = 0.0/);
  assert.ok(!skin.includes('#define TONE_MAPPING\n'));
});
