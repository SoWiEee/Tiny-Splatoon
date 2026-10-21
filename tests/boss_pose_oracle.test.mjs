import { readFileSync, writeFileSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import assert from 'node:assert/strict';
const fixture=JSON.parse(readFileSync(new URL('./boss_pose_fixtures.json',import.meta.url),'utf8'));
const samples=[];
const input=fixture.cases.map(c=>{
 const byFrame=new Map(c.records.map(r=>[r.frame,r]));
 return 'case\n'+c.inputs.map((i,f)=>{
  const record=byFrame.get(f);if(record)samples.push({scenario:c.name,...record});
  return [i.dt,...i.position,i.yaw,i.speed,i.turn,i.hurt,i.phase,+i.stunned,+i.dead,+i.hasAim,...i.aim,i.move||'-',i.movePhase||'-',i.moveTime,i.phaseDuration,+i.hasParams,+i.wall,i.count,i.flash,+i.weakFlash,i.trigger||'-',i.rings.length,...i.rings,i.barrels.length,...i.barrels.flat(),record?1:0].join(' ');
 }).join('\n')+'\nend\n';
}).join('');
const result=spawnSync(process.argv[2],[],{input,encoding:'utf8',maxBuffer:32*1024*1024});assert.equal(result.status,0,result.stderr);
const output=result.stdout.trim().split(/\r?\n/).map(s=>JSON.parse(s));assert.equal(output.length,samples.length);
const failures=[],stats=new Map();
function compare(actual,expected,path,state){
 if(!actual||actual.length!==expected.length){state.failures++;state.first??=`${path} length ${actual?.length} != ${expected.length}`;return;}
 for(let n=0;n<expected.length;n++){
  if(Array.isArray(expected[n]))compare(actual[n],expected[n],path+'/'+n,state);
  else if(typeof expected[n]==='string'){if(actual[n]!==expected[n]){state.failures++;state.first??=`${path}/${n}: ${actual[n]} != ${expected[n]}`;}}
  else{const error=Math.abs(actual[n]-expected[n]);state.maxError=Math.max(state.maxError,error);if(!Number.isFinite(error)||error>.001){state.failures++;state.first??=`${path}/${n}: ${actual[n]} != ${expected[n]}`;}}
 }
}
for(let n=0;n<samples.length;n++){
 const expected=samples[n],actual=output[n];let state=stats.get(expected.scenario);if(!state){state={scenario:expected.scenario,failures:0,maxError:0,first:null};stats.set(expected.scenario,state);}
 const first=state.first;
 compare(actual.skin,expected.skin,`frame ${expected.frame} bone`,state);compare(actual.shapes,expected.shapes,`frame ${expected.frame} shape`,state);compare(actual.poseSockets,expected.poseSockets,`frame ${expected.frame} pose socket`,state);
 for(const [name,values]of Object.entries(expected.uniforms))compare(actual.uniforms[name],values,`frame ${expected.frame} material ${name}`,state);
 if(first===null&&state.first!==null)failures.push(state);
}
const report={scenarios:fixture.cases.length,updates:fixture.cases.reduce((n,c)=>n+c.inputs.length,0),snapshots:samples.length,bones:fixture.boneNames.length,tolerance:.001,passed:stats.size-failures.length,results:[...stats.values()]};
if(process.argv[3])writeFileSync(process.argv[3],JSON.stringify(report,null,2)+'\n');
for(const state of failures)console.error(`${state.scenario}: ${state.first}; differences=${state.failures}, max=${state.maxError}`);
assert.equal(failures.length,0,`Boss source pose ${report.passed}/${report.scenarios} passed`);
console.log(`BOSS_POSE ${report.scenarios} scenarios / ${report.updates} updates / ${report.snapshots} complete 100-bone/socket/material snapshots match source`);
