import assert from 'node:assert/strict';
import { createHash } from 'node:crypto';
import { readFile, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';

const root=resolve(import.meta.dirname,'..');
const target=resolve(root,'tests/fixtures/character_lod_fixtures.json');
const sourcePath='splat-ref/src/game/character-lod.js';
const source=await readFile(resolve(root,sourcePath));
const sourceHash=createHash('sha256').update(source).digest('hex');
const baseline=await readFile(resolve(root,'docs/reference-baseline.sha256'),'utf8');
const baselineEntry=baseline.split(/\r?\n/).find(line=>line.endsWith(`  ${sourcePath}`));
assert.ok(baselineEntry,`Missing baseline hash for ${sourcePath}`);
assert.equal(baselineEntry.slice(0,64).toLowerCase(),sourceHash,`Reference baseline mismatch: ${sourcePath}`);

const { FADE_S, HYST, LOD_QUALITY, pickTier }=await import('../splat-ref/src/game/character-lod.js');
const qualityNames=['low','medium','high','ultra'];
const qualities=qualityNames.map(name=>{
  const quality=LOD_QUALITY[name];
  return {name,hero:Number.isFinite(quality.hero)?quality.hero:null,
    heroLocal:Number.isFinite(quality.heroLocal)?quality.heroLocal:null,far:quality.far};
});
const samples=[30,54,78,90,120,150,200,300,430,520,200,60,30];
const dt=1/60,framesPerSample=22,scenarios=[];
for(let qualityIndex=0;qualityIndex<qualityNames.length;qualityIndex++) for(const local of [false,true]) {
  const qualityName=qualityNames[qualityIndex],quality=LOD_QUALITY[qualityName];
  const hero=local?quality.heroLocal:quality.hero;
  let tier=-1,to=-1,fade=0;
  const updates=[];
  for(const pixels of samples) for(let frame=0;frame<framesPerSample;frame++) {
    if(to>=0) {
      fade=Math.min(1,fade+dt/FADE_S);
      if(fade>=1){tier=to;to=-1;fade=0;}
    } else {
      const wanted=pickTier(pixels,tier,quality,hero);
      if(tier<0)tier=wanted;
      else if(wanted!==tier){to=wanted;fade=0;}
    }
    updates.push([pixels,dt,tier,to>=0?to:tier,fade,Number(to>=0)]);
  }
  scenarios.push({name:`${qualityName}_${local?'local':'remote'}`,quality:qualityName,local,updates});
}
const fixture={
  version:1,
  source:{path:sourcePath,sha256:sourceHash},
  fadeSeconds:FADE_S,
  hysteresis:HYST,
  qualities,
  samplePixels:samples,
  dt,
  framesPerSample,
  updateCount:scenarios.reduce((total,scenario)=>total+scenario.updates.length,0),
  scenarios,
};
const output=JSON.stringify(fixture,null,2)+'\n';
if(process.argv.includes('--check'))
  assert.equal(await readFile(target,'utf8'),output,`Stale Character LOD fixtures: ${target}`);
else await writeFile(target,output);
console.log(`${process.argv.includes('--check')?'Verified':'Wrote'} Character LOD source fixtures`);
