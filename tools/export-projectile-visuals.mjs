import { register } from 'node:module';
import { readFile, mkdir, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';
register(new URL('./reference-loader.mjs', import.meta.url));
const THREE = await import('three');
const { Projectiles } = await import('../splat-ref/src/game/weapons.js');
const { buildCharacterShader } = await import('./export-character-shaders.mjs');
const root = resolve(import.meta.dirname, '..');
const output = resolve(root, 'assets/reference/projectiles');
const check = process.argv.includes('--check');
if (process.argv.slice(2).some(v => v !== '--check')) throw new Error('Usage: node tools/export-projectile-visuals.mjs [--check]');
await mkdir(output, { recursive: true });
async function artifact(name, data) {
  const bytes = Buffer.isBuffer(data) ? data : Buffer.from(data);
  const file = resolve(output, name);
  if (check) {
    const existing = await readFile(file);
    if (Buffer.isBuffer(data) ? !existing.equals(bytes) : existing.toString('utf8').replace(/\r\n/g, '\n') !== data)
      throw new Error(`Stale source projectile visual: ${name}`);
  } else await writeFile(file, bytes);
}
const system = new Projectiles(new THREE.Scene());
const { G } = await import('../splat-ref/src/core/ctx.js');
G.scene=new THREE.Scene();
// Expose private constructors in memory, preserving every source file on disk.
async function constructors(relative,names) {
  const url=pathToFileURL(resolve(root,relative));
  const source=(await readFile(url,'utf8')).replace(/((?:from\s*|import\s*)['"])(\.{1,2}\/[^'"]+)(['"])/g,(_,a,b,c)=>a+new URL(b,url).href+c);
  try {return await import('data:text/javascript;base64,'+Buffer.from(source+'\nexport { '+names.join(',')+' };').toString('base64'));}
  catch(error){throw new Error(`Source constructor ${relative}: ${String(error.message).slice(0,180)}`);}
}
const bow=(await constructors('splat-ref/src/game/kits/bow.js',['ensureRender'])).ensureRender();
const fist=(await constructors('splat-ref/src/game/kits/mitts.js',['ensureFistMesh'])).ensureFistMesh();
const { BladeFX }=await import('../splat-ref/src/game/kits/blade-fx.js');
const wave=BladeFX.waveMesh(new THREE.Color('#ff8a14')).mesh;
const sourceParts={arrowInk:bow.ink,arrowFletch:bow.fl,arrowGlow:bow.glow,arrowStreak:bow.streak,fist,wave};
for(const [name,part] of Object.entries(sourceParts)) {
  const geo=part.geometry,n=geo.attributes.position.count;
  const idx=geo.index?Array.from(geo.index.array):Array.from({length:n},(_,i)=>i);
  if(!geo.attributes.normal)geo.computeVertexNormals();
  const bytes=Buffer.alloc(16+n*48+idx.length*4);bytes.write('TSGEOM01');bytes.writeUInt32LE(n,8);bytes.writeUInt32LE(idx.length,12);
  let at=16;
  for(let i=0;i<n;i++)for(const [key,size,fallback] of [['position',3,0],['normal',3,0],['uv',2,0],['color',3,1],['aK',1,0]])
    for(let c=0;c<size;c++,at+=4)bytes.writeFloatLE(geo.attributes[key]?.getComponent(i,c)??fallback,at);
  for(const i of idx){bytes.writeUInt32LE(i,at);at+=4;}
  const mat=part.material;
  if(name==='arrowStreak')mat.vertexShader='uniform vec3 uTint;\n'+mat.vertexShader.replace('vC = vec3(1.0);','vC = uTint;');
  const shader=buildCharacterShader(mat,{toneMapping:false,linearOutput:true});
  await artifact(name+'.bin',bytes);await artifact(name+'.vert',shader.vertex);await artifact(name+'.frag',shader.fragment);
  const defaults=Object.entries(shader.uniforms).map(([k,v])=>[k,Array.isArray(v)?v:[v]])
    .filter(([,v])=>v.length&&v.every(x=>typeof x==='number'&&Number.isFinite(x)));
  await artifact(name+'.uniforms',defaults.map(([k,v])=>`${k} ${v.length} ${v.join(' ')}`).join('\n')+'\n');
}
const geometry = system.blobs.geometry;
const vertexCount = geometry.attributes.position.count, indices = geometry.index.array;
const mesh = Buffer.alloc(16 + vertexCount * 24 + indices.length * 4);
mesh.write('TSBLOB01'); mesh.writeUInt32LE(vertexCount, 8); mesh.writeUInt32LE(indices.length, 12);
let offset = 16;
for (let i = 0; i < vertexCount; i++) for (const key of ['position', 'normal'])
  for (let c = 0; c < 3; c++, offset += 4) mesh.writeFloatLE(geometry.attributes[key].getComponent(i, c), offset);
for (const index of indices) { mesh.writeUInt32LE(index, offset); offset += 4; }
await artifact('blob.bin', mesh);
const material = system.blobs.material;
material.defines = { ...material.defines, USE_INSTANCING: '', USE_INSTANCING_COLOR: '' };
const blob = buildCharacterShader(material, { toneMapping: false, linearOutput: true });
blob.vertex = blob.vertex.replace('in vec3 position;', 'in mat4 instanceMatrix; in vec3 instanceColor; in vec3 position;');
// Three enables fragment USE_COLOR for instance colors without enabling a vertex color attribute.
blob.fragment = blob.fragment.replace('#version 450 core', '#version 450 core\n#define USE_COLOR');
await artifact('blob.vert', blob.vertex); await artifact('blob.frag', blob.fragment);
const ribbon = buildCharacterShader(system._beamMesh().material, { linearOutput: true });
await artifact('ribbon.vert', ribbon.vertex); await artifact('ribbon.frag', ribbon.fragment);
const numeric = Object.entries(blob.uniforms).map(([name,value]) => [name,Array.isArray(value)?value:[value]])
  .filter(([,values]) => values.length && values.every(v=>typeof v==='number'&&Number.isFinite(v)));
await artifact('blob.uniforms', numeric.map(([name,values])=>`${name} ${values.length} ${values.join(' ')}`).join('\n')+'\n');
const fixtures=[];
for(let i=0;i<24;i++) {
  const kind=['shot','blast','drop','slosh'][i%4];
  const p={type:kind, pos:new THREE.Vector3(1+i*.23,2+i*.04,3+i*.7), start:new THREE.Vector3(1,2,2),
    vel:new THREE.Vector3(i%3===0?0:1+i, i%5===0?35:-2.4, i%3===0?0:24-i*.2),
    age:i===0?0:i===1?.004:.03+i*.043, life:1.2,seed:.027*i,size:.15,
    delay:i===23?.1:0,vis:kind==='blast'?.2:.1+i*.001,tail0:kind==='blast'?.5:.8,
    tailK:1.3,wob:.035,wobF:26,nose:.3,sats:i%5,
    owner:{color:new THREE.Color(i%2?'#2f5bff':'#ff8a14')}};
  system.list=[p]; system._draw();
  const input=[kind,...p.pos.toArray(),...p.vel.toArray(),...p.start.toArray(),p.age,p.life,p.seed,p.size,p.delay,p.vis,p.tail0,p.tailK,p.wob,p.wobF,p.nose,p.sats,i%2];
  const expected=[];
  for(let n=0;n<system.blobs.count;n++)expected.push(...system.blobs.instanceMatrix.array.slice(n*16,n*16+16),
    ...system.blobShape.array.slice(n*4,n*4+4),...system.blobs.instanceColor.array.slice(n*3,n*3+3));
  fixtures.push(input.join(' ')+' '+system.blobs.count+' '+expected.join(' '));
}
await artifact('blob-fixtures.txt',fixtures.length+'\n'+fixtures.join('\n')+'\n');
await artifact('manifest.json', JSON.stringify({schema:1, source:'splat-ref/src/game/weapons.js Projectiles',
  sourceSha256:createHash('sha256').update(await readFile(resolve(root,'splat-ref/src/game/weapons.js'))).digest('hex'),
  geometry:{vertices:vertexCount, indices:indices.length, sha256:createHash('sha256').update(mesh).digest('hex')},
  material:{uniforms:blob.uniforms,renderState:blob.renderState}, ribbon:{uniforms:ribbon.uniforms,renderState:ribbon.renderState}}, null, 2)+'\n');
console.log(`${check?'Checked':'Exported'} source projectile teardrop geometry/material and charger ribbons (${vertexCount} vertices).`);
