import { spawnSync } from 'node:child_process';
import assert from 'node:assert/strict';
import { LOD_QUALITY, FADE_S, pickTier } from '../splat-ref/src/game/character-lod.js';

const executable=process.argv[2];
const qualities=['low','medium','high','ultra'];
const input=[];
const expected=[];
for (let q=0;q<qualities.length;q++) for(const local of [false,true]) {
  input.push('-1 0 0 0');
  let tier=-1,to=-1,fade=0;
  const Q=LOD_QUALITY[qualities[q]];
  const hero=local?Q.heroLocal:Q.hero;
  for(const px of [30,54,78,90,120,150,200,300,430,520,200,60,30]) {
    for(let frame=0;frame<22;frame++) {
      const dt=1/60;
      if(to>=0) {
        fade=Math.min(1,fade+dt/FADE_S);
        if(fade>=1){tier=to;to=-1;fade=0;}
      } else {
        const wanted=pickTier(px,tier,Q,hero);
        if(tier<0)tier=wanted;
        else if(wanted!==tier){to=wanted;fade=0;}
      }
      input.push(`${q} ${local?1:0} ${px} ${dt}`);
      expected.push([tier,to>=0?to:tier,fade,to>=0]);
    }
  }
}
const result=spawnSync(executable,[],{input:input.join('\n')+'\n',encoding:'utf8'});
assert.equal(result.status,0,result.stderr);
const actual=result.stdout.trim().split(/\r?\n/);
assert.equal(actual.length,expected.length);
for(let i=0;i<actual.length;i++) {
  const row=actual[i].trim().split(/\s+/).map(Number);
  const want=expected[i];
  assert.equal(row[0],want[0],`tier at ${i}`);
  assert.equal(row[1],want[1],`target at ${i}`);
  assert.ok(Math.abs(row[2]-want[2])<1e-5,`fade at ${i}`);
  assert.equal(row[3],Number(want[3]),`transition at ${i}`);
}
console.log(`Character LOD source oracle: ${actual.length} state updates across all quality tiers`);
