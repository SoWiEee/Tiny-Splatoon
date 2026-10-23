import { register } from 'node:module';
import { readFile, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
register(new URL('./reference-loader.mjs',import.meta.url));
const THREE=await import('three');
const {ScreenFX}=await import('../splat-ref/src/fx/screenfx.js');
const {emit}=await import('../splat-ref/src/core/ctx.js');
const names=['uSpeed','uStretch','uPunch','uBlast','uBlastRing','uChroma','uEdgeInk','uAura','uHeart','uHurt','uUrgency','uSwim','uFocus','uCharge','uShimmer','uKill','uFlash','uDesat','uSat','uFlood','uFloodDrip','uHole','uFloodClear'];
const lensKeys=['x','y','r','rx','ry','rot','I','I0','life','stick','pop','grow','vx','vy','age','trailT','hold','seed'];
const scenarios=[
  ['swim',{form:'swim',speed:11.8},''],['low_health',{hp:12},''],['enemy_ink',{speed:4,onEnemy:true},''],
  ['full_charge',{charging:true,charge:1},''],['special',{special:true},'special'],['jump_flight',{jump:'flight',speed:9},'flight'],
  ['damage',{hp:35},'damage'],['kill',{},'kill'],['water',{alive:false},'water'],['flood',{alive:false},'flood'],
  ['reveal',{invuln:1},'respawn'],['blast',{},'blast'],['finish',{phase:'finish'},''],['urgency',{remaining:9},'count'],
  ['reduced_motion',{form:'swim',speed:11.8,reduced:true},'flight'],['paused',{form:'swim',speed:11.8,paused:true},'damage'],
  ['rain',{rain:true},''],['emerge',{form:'swim',speed:8},'emerge'],['landing',{jump:'flight'},'landing'],
  ['impact',{},'impact'],['impact_own',{},'impact_own'],['impact_roll',{},'impact_roll'],['reset',{},'reset'],['go',{},'go'],
];
const cases=[];
let seed=1;const previousRandom=Math.random;
Math.random=()=>{seed=(Math.imul(seed,1664525)+1013904223)>>>0;return seed/4294967296;};
globalThis.matchMedia=()=>({matches:false});
try{
 for(const [name,opts,event] of scenarios){
  const camera=new THREE.PerspectiveCamera(60,16/9,.1,1000);camera.position.set(0,0,8);camera.lookAt(0,0,0);camera.updateMatrixWorld();
  const actor={isLocal:true,alive:true,team:0,enemyTeam:1,pos:new THREE.Vector3(),vel:new THREE.Vector3(),anim:{form:'kid'},hp:100,onEnemy:false,specialActive:false,weaponRunner:{charging:false,charge:0},invuln:0};
  const match={local:actor,state:'playing',time:180,paused:false,attract:false};
  const renderer={getDrawingBufferSize(v){return v.set(1600,900);},getClearColor(c){return c.setRGB(0,0,0);},getClearAlpha(){return 0;},getRenderTarget(){return null;},setRenderTarget(){},setClearColor(){},clear(){},render(){}};
  const G={mode:'match',match,camera,settings:{quality:'high',cameraShake:1},teamColors:[new THREE.Color(1,.25,.01),new THREE.Color(.02,.1,1)],projectiles:{clouds:[]}};
  const fx=new ScreenFX({renderer,setExtraPass(){}},G);seed=0x195016;
  const records=[];
  for(let frame=0;frame<90;frame++){
    const input={dt:1/60,alive:opts.alive??true,hp:opts.hp??100,speed:opts.speed??0,vy:0,form:opts.form??'kid',onEnemy:opts.onEnemy??false,special:opts.special??false,charging:opts.charging??false,charge:opts.charge??0,invuln:opts.invuln??0,remaining:opts.remaining??180,phase:opts.phase??'playing',jump:opts.jump??'',jumpTime:frame/60,jumpDuration:1.2,paused:opts.paused??false,reduced:opts.reduced??false,rain:opts.rain??false};
    if(event==='emerge'&&frame>=20)input.form='kid';if(event==='landing'&&frame>=30)input.jump='';
    actor.alive=input.alive;actor.hp=input.hp;actor.vel.set(input.speed,input.vy,0);actor.anim.form=input.form;actor.onEnemy=input.onEnemy;actor.specialActive=input.special;actor.weaponRunner.charging=input.charging;actor.weaponRunner.charge=input.charge;actor.invuln=input.invuln;actor.superJumpState=input.jump?{phase:input.jump,t:input.jumpTime,dur:input.jumpDuration}:null;
    match.state=input.phase;match.time=input.remaining;match.paused=input.paused;G.settings.cameraShake=input.reduced?.35:1;G.projectiles.clouds=input.rain?[{group:{position:new THREE.Vector3(0,4,0)},team:1,t:0,dur:10}]:[];
    let action=frame===0?event:'';
    if(event==='reset')action=frame===0?'damage':frame===30?'reset':'';
    if(action==='damage')emit('damage',{victim:actor,amount:55});
    if(action==='kill')emit('hit',{attacker:actor,killed:true});
    if(action==='water')fx._startFlood(null,'water');if(action==='flood')fx._startFlood({team:1},'ink');
    if(action==='respawn')fx._startReveal(actor);if(action==='special')emit('special:use',{actor,id:'storm'});
    if(action==='flight')emit('superjump',{actor,phase:'flight'});if(action==='blast')fx._blast(new THREE.Vector3(),1,G.teamColors[0]);
    if(action.startsWith('impact'))emit('weapon:impact',{pos:new THREE.Vector3(0,0,6.9),team:action==='impact_own'?0:1,kind:action==='impact_roll'?'roll':'shot'});
    if(action==='reset')fx.reset();if(action==='go')emit('match:state',{state:'playing',match});
    if(action==='count'){fx.s.urg=1;fx.s.urgBase=.07;}
    fx.update(input.dt,{match,_skipRender:true});
    const values=names.flatMap(n=>fx.U[n].value?.toArray?fx.U[n].value.toArray():[fx.U[n].value]);
    const lens=frame%15===0?fx.lens.parts.map(p=>[p.kind==='drop'?0:p.kind==='arm'?1:p.kind==='sat'?2:3,p.ch,...lensKeys.map(k=>p[k])]):null;
    records.push({input,action,values,lens});
  }
  G.mode='';fx.dispose();cases.push({name,records});
 }
}finally{Math.random=previousRandom;}
const hash=createHash('sha256').update(await readFile(new URL('../splat-ref/src/fx/screenfx.js',import.meta.url))).digest('hex');
const text=JSON.stringify({sourceHash:hash,names,lensKeys,cases})+'\n';
const path=new URL('../tests/fixtures/screen_effects_fixtures.json',import.meta.url);
if(process.argv.includes('--check')){if(await readFile(path,'utf8')!==text)throw Error('Stale screen effects fixture');}else await writeFile(path,text);
console.log(`Screen FX source: ${cases.length} scenarios / ${cases.reduce((n,c)=>n+c.records.length,0)} frames`);
