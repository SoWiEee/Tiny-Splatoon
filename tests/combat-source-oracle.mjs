// Development-only oracle: execute the locked source runner, record action boundaries and state.
import { register } from 'node:module';
import { readFileSync, writeFileSync } from 'node:fs';
register(new URL('../tools/reference-loader.mjs', import.meta.url));
const THREE = await import('three');
const { G } = await import('../splat-ref/src/core/ctx.js');
const { WeaponRunner } = await import('../splat-ref/src/game/weapons.js');
const { WEAPONS, SUBS } = await import('../splat-ref/src/config.js');
const rows=[];
for(const id of ['shooter','blaster','charger','roller','brush','bucket','spinner','twins']) {
  for(const dt of [1/60,1/120]) for(const hold of [0.1,1.2]) {
    let shots=0;
    const actor={weapon:WEAPONS[id],sub:SUBS.bomb,pos:new THREE.Vector3(),vel:new THREE.Vector3(),grounded:true,
      aimYaw:0,aimPitch:0,yaw:0,ink:100,lastFire:99,fireFacing:0,team:0,alive:true,
      intent:{move:new THREE.Vector3(),fire:false},character:{trigger(){}},addTurf(){},_nearCamera(){return false;}};
    G.actors=[actor];G.time=0;G.paint={splat(){return 0;}};
    G.projectiles=Object.fromEntries(['fireShooter','fireBlaster','fireCharger','fireFlick','fireSlosh','fireSwipe','fireStream','fireTwin'].map(k=>[k,()=>{shots++;}]));
    const runner=new WeaponRunner(actor);actor.weaponRunner=runner;
    let previous=false;
    for(let frame=1;frame<=Math.round(3/dt);++frame) {
      G.time+=dt;const fire=frame<=Math.round(hold/dt);actor.intent.fire=fire;
      runner.update(dt,{fire,firePressed:fire&&!previous,sub:false,subReleased:false});previous=fire;
      if(frame===1 || frame%Math.round(0.1/dt)===0)
        rows.push({id,dt,hold,frame,ink:actor.ink,charge:runner.charge,burst:runner.burstT,speed:runner.moveSpeed(),shots,charging:+runner.charging,rolling:+runner.rolling,firing:+runner.firingPose(),busy:+!!runner.busy()});
    }
  }
}
const n=v=>Number(v).toPrecision(17);
const output=['#pragma once','#include <array>','namespace restoration::test {',
  'struct CombatRunnerFixture { const char* id; double dt,hold; int frame; double ink,charge,burst,speed; int shots,charging,rolling,firing,busy; };',
  `inline constexpr std::array<CombatRunnerFixture, ${rows.length}> combatRunnerFixtures{{`,
  ...rows.map(r=>`  {"${r.id}",${n(r.dt)},${n(r.hold)},${r.frame},${[r.ink,r.charge,r.burst,r.speed].map(n).join(',')},${r.shots},${r.charging},${r.rolling},${r.firing},${r.busy}},`),
  '}};','}',''].join('\n');
const path=new URL('./fixtures/combat_runner_fixtures.hpp',import.meta.url);
if(process.argv.includes('--check')) {if(readFileSync(path,'utf8')!==output)throw new Error('Combat source action oracle differs');}
else writeFileSync(path,output);
console.log(`Locked WeaponRunner: ${rows.length} action/state snapshots`);
