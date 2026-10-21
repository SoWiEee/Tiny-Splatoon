import {register} from 'node:module';
import {readFile,writeFile,mkdir} from 'node:fs/promises';
import {createHash} from 'node:crypto';
import {resolve} from 'node:path';
register(new URL('./reference-loader.mjs',import.meta.url));register(new URL('./device-reference-loader.mjs',import.meta.url));
const THREE=await import('three');
const {G}=await import('../splat-ref/src/core/ctx.js');
const {SUBS}=await import('../splat-ref/src/config.js');
const {getSpecialProp}=await import('../splat-ref/src/game/special-props.js');
const {getWeaponDef}=await import('../splat-ref/src/game/character-weapons.js');
const {getPlasticMaterial,getInkMaterial}=await import('../splat-ref/src/game/character-mats.js');
const {SubSystem,CURTAIN_VS,CURTAIN_FS}=await import('../splat-ref/src/game/subs.js');
const {SpecialSystem,propMesh,IMPL,sourcePropsReady}=await import('../splat-ref/src/game/specials.js');
const {buildSourceMaterialShader}=await import('./export-character-shaders.mjs');
await sourcePropsReady();
const out=resolve('assets/reference/devices'),check=process.argv.includes('--check'),files=new Map();
const hash=b=>createHash('sha256').update(b).digest('hex');
function artifact(name,b){b=Buffer.from(b);if(files.has(name)&&!files.get(name).equals(b))throw Error(`Source device asset key collision: ${name}`);files.set(name,b);return name;}
class Writer {chunks=[];raw(b){this.chunks.push(Buffer.from(b));}u32(n){const b=Buffer.alloc(4);b.writeUInt32LE(n);this.raw(b);}str(s){const b=Buffer.from(s);this.u32(b.length);this.raw(b);}floats(a){const b=Buffer.alloc(a.length*4);a.forEach((v,i)=>b.writeFloatLE(v,i*4));this.raw(b);}done(){return Buffer.concat(this.chunks);}}
function mesh(g){const w=new Writer(),p=g.attributes.position;w.raw('TSMESH01');w.u32(p.count);const ix=Array.from(g.index?.array??Array.from({length:p.count},(_,k)=>k));w.u32(ix.length);const attrs=Object.entries(g.attributes);w.u32(attrs.length);for(const [n,a]of attrs){if(a.count!==p.count)throw Error(`Unsupported device attribute ${n}`);w.str(n);w.u32(a.itemSize);w.str(a.array.constructor.name);w.u32(a.normalized?1:0);w.floats(Array.from({length:a.count*a.itemSize},(_,i)=>a.getComponent(Math.floor(i/a.itemSize),i%a.itemSize)));}ix.forEach(n=>w.u32(n));const b=w.done(),name=hash(b)+'.mesh';artifact(name,b);return name;}
const flat=v=>typeof v==='number'||typeof v==='boolean'?[Number(v)]:v?.toArray?v.toArray():Array.isArray(v)?v.flatMap(flat):[];
const models=[],summary=[];
let randomState=0x6d2b79f5;Math.random=()=>{randomState^=randomState<<13;randomState^=randomState>>>17;randomState^=randomState<<5;return (randomState>>>0)/4294967296;};
G.teamColors=[new THREE.Color(1,.2541520943200296,.0069954101845983935),new THREE.Color(.028426039499072558,.10461648408208657,1)];G.scene=new THREE.Scene();G.camera=null;G.actors=[];G.time=0;
const subs=new SubSystem(G.scene),sys=new SpecialSystem(G.scene);
const kitBuilders={};for(const kind of ['shaker','waddle','torpedo','tracer','boomerang'])kitBuilders[kind]=(await import(`../splat-ref/src/game/kits/${kind}.js`)).sourceWorldModel;
for(let team=0;team<2;team++){
  const templates=[];
  for(const kind of Object.keys(SUBS)){let model,refs;if(kitBuilders[kind]){refs=kitBuilders[kind](team);model=refs.outer??refs;}else {model=subs._prop(kind,team);refs=model.userData;}
    templates.push({kind,model,refs});}
  for(const kind of ['kraken','jetpack','speaker','missile']){
    const d=getSpecialProp(kind),model=propMesh(d,team,sys.glowMat(team)),refs={};
    if(kind==='speaker'){const cone=new THREE.Group();cone.position.copy(d.coneAt);const parts=new THREE.Group();parts.position.copy(d.coneAt).negate();parts.add(new THREE.Mesh(d.cone,getPlasticMaterial()),new THREE.Mesh(d.coneInk,getInkMaterial(G.teamColors[team])));cone.add(parts);model.add(cone);refs.cone=cone;}
    templates.push({kind,model,refs});
  }
  for(const [kind,tint,emission]of [['stormBase',.12,.16],['stormTop',.55,.08]]){const model=new THREE.Mesh(new THREE.IcosahedronGeometry(1,3),new THREE.MeshStandardMaterial({color:G.teamColors[team].clone().lerp(new THREE.Color(1,1,1),tint),roughness:.95,emissive:G.teamColors[team],emissiveIntensity:emission,transparent:true,opacity:.97}));model.castShadow=true;templates.push({kind,model,refs:{}});}
  const scan={team,pos:new THREE.Vector3(),mesh:new THREE.Group(),sub:SUBS.scan};subs._startCloud(scan);templates.push({kind:'scanCloud',model:scan.cloud,refs:{}});
  const mist={team,pos:new THREE.Vector3(),mesh:new THREE.Group(),sub:SUBS.mist};subs._startMist(mist);templates.push({kind:'mistCloud',model:mist.cloud,refs:{}});
  const sheet=new THREE.Mesh(new THREE.PlaneGeometry(SUBS.curtain.width,SUBS.curtain.height),new THREE.ShaderMaterial({uniforms:{uColor:{value:G.teamColors[team].clone()},uTime:{value:0},uLife:{value:1}},vertexShader:CURTAIN_VS,fragmentShader:CURTAIN_FS,transparent:true,depthWrite:false,side:THREE.DoubleSide}));sheet.position.y=SUBS.curtain.height/2+.05;const curtain=new THREE.Group();curtain.add(sheet);templates.push({kind:'curtainSheet',model:curtain,refs:{sheet}});
  for(const kind of ['waddle','torpedo']){const ring=(await import(`../splat-ref/src/game/kits/${kind}.js`)).sourceRing(kind==='waddle'?G.teamColors[team]:team);templates.push({kind:kind+'Ring',model:ring,refs:{}});}
  const {RIB_VS,RIB_FS}=await import('../splat-ref/src/game/kits/tracer.js');const ribbon=buildSourceMaterialShader(new THREE.ShaderMaterial({vertexShader:RIB_VS,fragmentShader:RIB_FS}),{toneMapping:false,linearOutput:true});artifact('tracer-ribbon.vert',ribbon.vertex);artifact('tracer-ribbon.frag',ribbon.fragment);
  const launched={team,dir:new THREE.Vector3(0,0,1),owner:{color:G.teamColors[team]}};(await import('../splat-ref/src/game/kits/brolly.js')).sourceWorldModel(launched);templates.push({kind:'brolly',model:launched.mesh,refs:{inner:launched.inner}});
  const actor={team,color:G.teamColors[team],yaw:0,pos:new THREE.Vector3(),alive:true,isLocal:false,_nearCamera:()=>false,character:{weapon:{pivot:new THREE.Group()},setVisible(){}}};const s={def:{hp:500,speed:2}};IMPL.crab.start.call(sys,actor,s);
  templates.push({kind:'crab',model:s.rig,refs:{hullG:s.hullG,ball:s.ball,gunSpin:s.gunSpin,...Object.fromEntries(s.legs.map((l,i)=>['legs'+i,l.g]))}});
  const stamp=getWeaponDef('sp_stamp');templates.push({kind:'stamp',model:new THREE.Group(),refs:{}});templates.at(-1).model.add(new THREE.Mesh(stamp.body,getPlasticMaterial()),new THREE.Mesh(stamp.ink,getInkMaterial(G.teamColors[team])));
  for(const {kind,model,refs} of templates){
    const tags=new Map();function tag(v,k){if(v?.isObject3D)tags.set(v,k);else if(Array.isArray(v))v.forEach((q,i)=>tag(q,k+i));}Object.entries(refs||{}).forEach(([k,v])=>tag(v,k));Object.entries(model.userData||{}).forEach(([k,v])=>tag(v,k));
    const nodes=[];model.traverse(o=>{const parent=nodes.findIndex(n=>n.object===o.parent);o.updateMatrix();nodes.push({object:o,parent,tag:tags.get(o)||'-'});});
    models.push(`${kind} ${team} ${nodes.length}`);
    for(const node of nodes){const o=node.object;let file='-',material='-';
      if(o.geometry){file=mesh(o.geometry);const data=buildSourceMaterialShader(o.material,{toneMapping:false,linearOutput:true});const uniforms=Object.entries(data.uniforms).map(([k,v])=>[k,flat(v)]).filter(([,v])=>v.length).map(([k,v])=>`${k} ${v.length} ${v.join(' ')}`).join('\n')+'\n';material=hash(data.vertex+data.fragment+uniforms).slice(0,20);artifact(material+'.vert',data.vertex);artifact(material+'.frag',data.fragment);artifact(material+'.uniforms',uniforms);}
      const mat=o.material;models.push([node.parent,node.tag,file,material,Number(o.visible),Number(o.castShadow),mat?Number(mat.transparent):0,mat?Number(mat.depthWrite):1,mat?.side||0,mat?.blending||1,o.userData.ph||0,...o.matrix.toArray()].join(' '));}
    summary.push({kind,team,nodes:nodes.length,meshes:nodes.filter(n=>n.object.geometry).length});
  }
}
artifact('models.txt',models.join('\n')+'\n');
const sources=['subs.js','specials.js','special-props.js','character-weapons.js',...['shaker','waddle','torpedo','tracer','boomerang'].map(k=>'kits/'+k+'.js')];
artifact('manifest.json',JSON.stringify({source:Object.fromEntries(await Promise.all(sources.map(async p=>[p,hash(await readFile('splat-ref/src/game/'+p))]))),models:summary,files:Object.fromEntries([...files].map(([n,b])=>[n,hash(b)]))},null,2)+'\n');
await mkdir(out,{recursive:true});for(const [n,b]of files){if(check){if(!b.equals(await readFile(resolve(out,n))))throw Error(`Stale source device visual ${n}`);}else await writeFile(resolve(out,n),b);}
console.log(`${check?'Checked':'Exported'} ${summary.length} source device/special models, ${files.size} assets`);
