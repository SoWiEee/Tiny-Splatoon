import { register } from 'node:module';
import { mkdir,readFile,writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { deflateSync } from 'node:zlib';
import { resolve } from 'node:path';
register(new URL('./reference-loader.mjs',import.meta.url));
const THREE=await import('three');
function gradient(){return {addColorStop(){}};}
const context=new Proxy({measureText:text=>({width:String(text).length*12}),createLinearGradient:gradient,createRadialGradient:gradient},{
  get(target,key){return key in target?target[key]:()=>{};},set(target,key,value){target[key]=value;return true;},
});
globalThis.document={createElement:()=>({width:0,height:0,getContext:()=>context})};
const {Environment}=await import('../splat-ref/src/world/environment.js');
const {G}=await import('../splat-ref/src/core/ctx.js');
const {MAPS}=await import('../splat-ref/src/config.js');
const {MAP_LAYOUTS}=await import('../splat-ref/src/world/maps.js');
const {Level}=await import('../splat-ref/src/world/level.js');
const {PropKit}=await import('../splat-ref/src/world/props.js');
const {dressingFor}=await import('../splat-ref/src/world/dressing.js');
const {inMode,layoutFor}=await import('../splat-ref/src/world/variants.js');
const {buildSourceMaterialShader}=await import('./export-character-shaders.mjs');
const out=resolve('assets/reference/environment'),check=process.argv.includes('--check');
const hash=b=>createHash('sha256').update(b).digest('hex');
async function artifact(name,bytes){const path=resolve(out,name);await mkdir(out,{recursive:true});bytes=Buffer.from(bytes);if(check){if(!bytes.equals(await readFile(path)))throw Error(`Stale environment scene ${name}`);}else await writeFile(path,bytes);return hash(bytes);}
// GPU sky/PMREM captures are exported separately; geometry, material hooks and motions run unchanged here.
class GeometryEnvironment extends Environment {
  _buildSky(){this.skyMat={defines:{}};this.sky=new THREE.Group();this.root.add(this.sky);}
  _bakeClouds(){} _rebuildEnvMap(){} _bakeFarReflection(){} _syncDeckShading(){}
}
class Writer {
  chunks=[];raw(b){this.chunks.push(Buffer.from(b));}u32(n){const b=Buffer.alloc(4);b.writeUInt32LE(n);this.raw(b);}str(s){const b=Buffer.from(s);this.u32(b.length);this.raw(b);}floats(a){const b=Buffer.alloc(a.length*4);a.forEach((v,i)=>{if(!Number.isFinite(v))throw Error('Nonfinite source environment geometry');b.writeFloatLE(v,i*4);});this.raw(b);}done(){return Buffer.concat(this.chunks);}
}
function flatten(v){if(typeof v==='number'||typeof v==='boolean')return [Number(v)];if(v?.toArray)return v.toArray();if(Array.isArray(v))return v.flatMap(flatten);return [];}
const shaders=new Map(),index=[];
for(const map of MAPS){
  const id=map.layout||map.id,scene=new THREE.Scene(),kit=new PropKit(scene,{castShadow:true,quality:'high'}),colliders=[];
  for(const item of dressingFor(id).filter(item=>inMode(item,'turf'))){const part=kit.add(item.type,item);if(part?.colliders)colliders.push(...part.colliders);}
  kit.build();kit.update(0,0);
  const L=new Level(layoutFor(MAP_LAYOUTS[id],'turf'),colliders);G.level=L;G.quality='high';
  const footprint=L.blocks.filter(b=>(b.aligned||Math.abs(b.axes[1].y)>.9999)&&b.aabbMax.y<.01&&b.aabbMax.y>-2.5&&b.aabbMin.y<-1).map(b=>({minX:b.aabbMin.x,maxX:b.aabbMax.x,minZ:b.aabbMin.z,maxZ:b.aabbMax.z,...(b.aligned?{}:{cx:b.center.x,cz:b.center.z,hx:b.half.x,hz:b.half.z,ax:b.axes[0].x,az:b.axes[0].z})}));
  const r={getClearColor:c=>c.set(0),getClearAlpha:()=>1,setClearColor(){},getRenderTarget:()=>null,setRenderTarget(){},render(){},xr:{enabled:false}};
  const e=new GeometryEnvironment(r,scene,{bounds:L.bounds,footprint,theme:map.theme||'day'});
  Environment.prototype._syncDeckShading.call(e);
  if(e._fieldMat){
    const field=buildSourceMaterialShader(e._fieldMat,{toneMapping:false,linearOutput:true});
    await artifact('deck-field.vert',field.vertex);await artifact('deck-field.frag',field.fragment);
    const entries=[...['uN','uField','uR'].map(k=>[k,e._fieldMat.uniforms[k].value]),...['uDeckFieldRect','uDeckFieldK','uDeckBox','uWetBox'].map(k=>[k,e.U[k].value])];
    await artifact(`deck-${id}.txt`,`${e._fieldRT.width} ${e._fieldRT.height}\n`+entries.map(([k,v])=>{const a=flatten(v);return `${k} ${a.length} ${a.join(' ')}`;}).join('\n')+'\n');
  }
  e.time=0;e._animate();e.root.updateMatrixWorld(true);
  const meshes=[];e.root.traverse(o=>{if(o.geometry?.attributes?.position&&o!==e.sea&&o!==e.sky)meshes.push(o);});
  const w=new Writer();w.raw('TSENVG01');w.u32(meshes.length);
  const motions=[];
  for(let j=0;j<meshes.length;j++){
    const o=meshes[j],g=o.geometry,m=o.material;
    const expanded=buildSourceMaterialShader(m,{toneMapping:false,linearOutput:true});
    if(o.isInstancedMesh){
      const defines='#define USE_INSTANCING\n'+(o.instanceColor?'#define USE_INSTANCING_COLOR\n':'');
      expanded.vertex=expanded.vertex.replace('#version 450 core\n','#version 450 core\n'+defines+'in mat4 instanceMatrix;\n'+(o.instanceColor?'in vec3 instanceColor;\n':''));
      expanded.fragment=expanded.fragment.replace('#version 450 core\n','#version 450 core\n'+defines);
    }
    const uniforms=Object.entries(expanded.uniforms).map(([k,v])=>[k,flatten(v)]).filter(([,v])=>v.length).map(([k,v])=>`${k} ${v.length} ${v.join(' ')}`).join('\n')+'\n';
    const key=hash(expanded.vertex+expanded.fragment+uniforms).slice(0,20);
    if(!shaders.has(key)){
      shaders.set(key,true);await artifact(key+'.vert',expanded.vertex);await artifact(key+'.frag',expanded.fragment);
      await artifact(key+'.uniforms',uniforms);
    }
    w.str(o.name||`mesh${j}`);w.str(key);
    const attrs=Object.entries(g.attributes).filter(([,a])=>!a.isInstancedBufferAttribute);w.u32(attrs.length);
    for(const [name,a] of attrs){w.str(name);w.u32(a.itemSize);w.u32(a.count);w.floats(Array.from({length:a.count*a.itemSize},(_,k)=>a.getComponent(Math.floor(k/a.itemSize),k%a.itemSize)));}
    const indices=Array.from(g.index?.array??Array.from({length:g.attributes.position.count},(_,k)=>k));w.u32(indices.length);indices.forEach(n=>w.u32(n));
    const count=o.isInstancedMesh?o.count:1;w.u32(count);
    for(let i=0;i<count;i++){
      let matrix=o.matrixWorld.clone(),color=new THREE.Color(1,1,1),phase=0;
      if(o.isInstancedMesh){const instance=new THREE.Matrix4();o.getMatrixAt(i,instance);matrix.multiply(instance);if(o.instanceColor)color.fromBufferAttribute(o.instanceColor,i);if(g.attributes.aPhase)phase=g.attributes.aPhase.getX(i);}
      w.floats([...matrix.toArray(),...color.toArray(),phase]);
      let kind='static',params=[];
      if(o===e.sailInst){kind='sail';const s=e.sailboats[i];params=[s.a,s.w,s.d,s.phase,s.s];}
      else if(o===e.buoyInst){kind='buoy';const b=e.buoys[i];params=[b.x,b.z,b.phase,b.s];}
      else if(o===e.gullInst){kind='gull';const q=e.gulls[i];params=[q.a0,q.w,q.cx,q.cz,q.r,q.h,q.s];}
      else if(o===e.ferris){kind='ferris';params=[...o.position.toArray(),o.rotation.y];}
      else if(o===e.lhBeam){kind='lighthouse';params=o.position.toArray();}
      else {const b=e.moored.find(b=>b.mesh===o);if(b){kind='moored';params=[b.spec.x,b.spec.z,b.phase,b.roll,o.rotation.y];}}
      motions.push([j,i,kind,params.length,...params].join(' '));
    }
    const s=expanded.renderState;w.floats([Number(s.transparent),Number(s.depthWrite),Number(s.depthTest),s.side,s.blending,Number(o.castShadow)]);
  }
  const raw=w.done(),header=Buffer.alloc(12);header.write('TSENVZ01');header.writeUInt32LE(raw.length,8);
  const packed=Buffer.concat([header,deflateSync(raw,{level:9})]);await artifact(`scene-${id}.bin.z`,packed);await artifact(`scene-${id}.motion`,motions.join('\n')+'\n');
  const foam=e.U.uFoamTex.value;
  if(foam?.image?.data){const h=Buffer.alloc(20);h.write('TSENV001');h.writeUInt32LE(foam.image.width,8);h.writeUInt32LE(foam.image.height,12);h.writeUInt32LE(8,16);const rgba=Buffer.alloc(foam.image.width*foam.image.height*4);foam.image.data.forEach((n,i)=>{rgba.fill(n,i*4,i*4+3);rgba[i*4+3]=255;});await artifact(`foam-${id}.rgba8.z`,deflateSync(Buffer.concat([h,rgba]),{level:9}));await artifact(`foam-${id}.txt`,[foam.image.width,foam.image.height,...e.U.uFoamRect.value.toArray(),e.U.uMarinaK.value.w].join(' ')+'\n');}
  const waterContours=e._marinaData?e._waterContours(e._marinaData.wet).length:0;
  if(e._marina&&waterContours===0)throw Error(`Missing PropKit water contours for ${id}`);
  index.push({id,meshes:meshes.length,instances:motions.length,waterContours,sha256:hash(packed)});
  const sets=e._marinaData?[e._marinaData.decks,e._marinaData.wet]:[e.footprint,[]];
  await artifact(`scene-${id}.rects`,sets.flatMap((set,k)=>set.map(r=>[k,r.cx,r.cz,r.hx,r.hz,r.ax,r.az].join(' '))).join('\n')+'\n');
  e.dispose();kit.dispose();console.log(`${check?'Checked':'Exported'} ${id} environment: ${meshes.length} meshes, ${motions.length} instances`);
}
await artifact('scene-manifest.json',JSON.stringify({source:hash(await readFile('splat-ref/src/world/environment.js')),materials:[...shaders.keys()].sort(),stages:index},null,2)+'\n');
