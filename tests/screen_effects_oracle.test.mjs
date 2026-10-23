import { readFileSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import assert from 'node:assert/strict';
const fixture=JSON.parse(readFileSync(new URL('./fixtures/screen_effects_fixtures.json',import.meta.url),'utf8'));
const records=fixture.cases.flatMap(c=>c.records.map(r=>({...r,scenario:c.name})));
const input=fixture.cases.map(c=>c.records.map(({input:i,action})=>[i.dt,+i.alive,i.hp,i.speed,i.vy,i.form,+i.onEnemy,+i.special,+i.charging,i.charge,i.invuln,i.remaining,i.phase,i.jump||'-',i.jumpTime,i.jumpDuration,+i.paused,+i.reduced,+i.rain,action||'-'].join(' ')).join('\n')+'\nend\n').join('');
const result=spawnSync(process.argv[2],[ ],{input,encoding:'utf8',maxBuffer:32*1024*1024});assert.equal(result.status,0,result.stderr);
const output=result.stdout.trim().split(/\r?\n/).map(s=>JSON.parse('['+s+']'));assert.equal(output.length,records.length);
let lensFrames=0;
for(let n=0;n<records.length;n++){
 const r=records[n],actual=output[n];
 assert.equal(actual[0].length,r.values.length);
 for(let k=0;k<r.values.length;k++)assert.ok(Math.abs(actual[0][k]-r.values[k])<1e-6,`${r.scenario}/${n}: uniform ${k}: ${actual[0][k]} != ${r.values[k]}`);
 if(r.lens){lensFrames++;assert.equal(actual[1].length,r.lens.length,`${r.scenario}/${n} lens count`);for(let p=0;p<r.lens.length;p++)for(let k=0;k<r.lens[p].length;k++)assert.ok(Math.abs(actual[1][p][k]-r.lens[p][k])<1e-8,`${r.scenario}/${n} lens ${p}/${k}: ${actual[1][p][k]} != ${r.lens[p][k]}`);}
}
console.log(`SCREEN_FX source match ${fixture.cases.length} scenarios, ${records.length} frames, ${lensFrames} full lens snapshots`);
