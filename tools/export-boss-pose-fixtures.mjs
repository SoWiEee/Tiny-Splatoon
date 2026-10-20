import { register } from 'node:module';
import { readFile, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
register(new URL('./reference-loader.mjs',import.meta.url));
const THREE=await import('three');
const {BossModel}=await import('../splat-ref/src/boss/bossModel.js');
const moves={slam:[1.15,1.9,1],barrage:[.85,2,.8],sweep:[1.15,2.1,.9],charge:[1.15,1.5,.9],crablets:[.9,.8,.6],frenzy:[1.2,3,1.7]};
const cases=[];
const base=()=>({dt:1/60,position:[0,0,0],yaw:0,speed:0,turn:0,hurt:0,phase:1,stunned:false,dead:false,hasAim:false,aim:[0,0,0],move:'',movePhase:'',moveTime:0,phaseDuration:0,hasParams:false,wall:true,count:3,rings:[],barrels:[],flash:0,weakFlash:false,trigger:''});
function timeline(name,frames,make){cases.push({name,inputs:Array.from({length:frames},(_,f)=>({...base(),...make(f,f/60)}))});}
timeline('idle',180,()=>({}));
timeline('walk',180,(_,t)=>({position:[0,0,t*3],speed:3}));
timeline('turn',180,(_,t)=>({yaw:t*.6,turn:.6}));
timeline('aim',180,(_,t)=>({hasAim:true,aim:[Math.sin(t)*8,3,12]}));
timeline('intro',210,(_,t)=>({move:'intro',movePhase:'act',moveTime:t,phaseDuration:3.4}));
timeline('roar',150,(_,t)=>({move:'roar',movePhase:'act',moveTime:t,phaseDuration:1.9,phase:3}));
timeline('phase_change',300,(f)=>({phase:f<60?1:f<180?2:3}));
timeline('hurt',180,(f)=>({hurt:f>=30&&f<45?.8:0,flash:f===60?1:0,weakFlash:true}));
timeline('stun_recovery',240,(f)=>({stunned:f>=30&&f<150,phaseDuration:2}));
timeline('death',420,(f)=>({phase:3,dead:f>=60}));
for(const [move,d]of Object.entries(moves)){
 const frames=Math.ceil((d[0]+d[1]+d[2]+.8)*60);
 timeline(move,frames,(_,t)=>{
  const p=t<d[0]?0:t<d[0]+d[1]?1:t<d[0]+d[1]+d[2]?2:3;
  return {move:p===3?'':move,movePhase:['tele','act','rec',''][p],moveTime:t-(p===0?0:p===1?d[0]:d[0]+d[1]),phaseDuration:d[Math.min(p,2)],phase:move==='frenzy'?3:move==='crablets'?2:1,
   hasParams:true,rings:[0,.9],barrels:[[3,0,5,.85],[-3,0,7,1.11],[0,0,10,1.37]],count:3,wall:true,
   position:move==='charge'?[0,0,Math.max(0,Math.min(d[1],t-d[0]))*8]:[0,0,0],speed:move==='charge'&&p===1?8:0,
   hasAim:true,aim:[move==='sweep'?Math.sin(t)*8:0,move==='slam'?0:2,12]};
 });
}
const hashes={};for(const file of ['bossAnim.js','bossModel.js','bossModelGeo.js','bossMats.js'])hashes[file]=createHash('sha256').update(await readFile(new URL('../splat-ref/src/boss/'+file,import.meta.url))).digest('hex');
let boneNames;
for(const scenario of cases){
 const model=new BossModel({ink:new THREE.Color(.0258,.102,1),weak:new THREE.Color(1,.258,.0037),quality:'high'});await model.ready;
 boneNames??=model.rig.bones.map(b=>b.name);const records=[];
 for(let frame=0;frame<scenario.inputs.length;frame++){
  const i=scenario.inputs[frame];model.root.position.fromArray(i.position);model.root.rotation.y=i.yaw;
  if(i.flash>0)model.flash(i.flash,i.weakFlash);if(i.trigger)model.trigger(i.trigger);
  model.update(i.dt,{speed:i.speed,turn:i.turn,hurt:i.hurt,phase:i.phase,stunned:i.stunned,dead:i.dead,move:i.move||null,movePhase:i.movePhase||null,moveT:i.moveTime,phaseDur:i.phaseDuration,aim:i.hasAim?new THREE.Vector3(...i.aim):null,
   params:i.hasParams?{rings:i.rings,b:i.barrels,n:i.count,wall:i.wall?1:0}:null});
  model.root.updateMatrixWorld(true);
  if(frame%15===0||frame===scenario.inputs.length-1){
   const invRoot=model.root.matrixWorld.clone().invert();
   const skin=model.rig.bones.map((b,n)=>invRoot.clone().multiply(b.matrixWorld).multiply(model.skeleton.boneInverses[n]).toArray());
   const shapes=model.hitShapes.map(h=>[...h.pos.toArray(),h.r,+h.weak,+h.active]);
   const uniforms=Object.fromEntries(Object.entries(model.U).flatMap(([name,u])=>{const v=u.value;if(typeof v==='number')return [[name,[v]]];if(v?.isVector2||v?.isVector3||v?.isVector4||v?.isColor||v?.isMatrix4)return [[name,v.toArray()]];return [];}));
   records.push({frame,skin,shapes,uniforms});
  }
 }
 scenario.records=records;model.dispose();
}
const text=JSON.stringify({sourceHashes:hashes,boneNames,cases})+'\n',path=new URL('../tests/boss_pose_fixtures.json',import.meta.url);
if(process.argv.includes('--check')){if(await readFile(path,'utf8')!==text)throw Error('Stale source Boss pose fixture');}else await writeFile(path,text);
console.log(`Source Boss pose: ${cases.length} scenarios, ${boneNames.length} bones, ${cases.reduce((n,c)=>n+c.inputs.length,0)} updates, ${cases.reduce((n,c)=>n+c.records.length,0)} complete snapshots`);
