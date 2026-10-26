import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { inflateSync } from 'node:zlib';
import { createHash } from 'node:crypto';
import { resolve } from 'node:path';
const root=resolve(import.meta.dirname,'..'),directory=resolve(root,'assets/world/stage-textures');
const metadata=JSON.parse(await readFile(resolve(directory,'metadata.json'),'utf8'));
assert.equal(metadata.schema,1);assert.equal(metadata.texlib.names.length,46);assert.equal(metadata.texlib.size,512);
const hash=bytes=>createHash('sha256').update(bytes).digest('hex');
for(const [file,digest]of Object.entries(metadata.sourceHashes))assert.equal(hash(await readFile(resolve(root,'splat-ref',file))),digest,`Source texture input changed: ${file}`);
let count=0;
for(const item of [...Object.values(metadata.texlib.textures),...Object.values(metadata.props),...Object.values(metadata.murals)]){
  const raw=inflateSync(await readFile(resolve(directory,item.file)));
  assert.equal(raw.toString('ascii',0,8),'TSTEX001');
  assert.equal(raw.readUInt32LE(8),item.width);assert.equal(raw.readUInt32LE(12),item.height);assert.equal(raw.readUInt32LE(16),item.layers);
  assert.equal(raw.readUInt32LE(20),Number(item.srgb));assert.equal(raw.length,24+item.width*item.height*item.layers*4);
  assert.equal(hash(raw),item.sha256,`Source texture asset hash changed: ${item.file}`);
  const colors=new Set();for(let i=24;i<raw.length;i+=4*17){colors.add(raw.readUInt32LE(i));if(colors.size>64)break;}
  if(item.file==='props-chain.tex.z'){
    let transparent=0,opaque=0;
    for(let i=27;i<raw.length;i+=4){transparent+=raw[i]===0;opaque+=raw[i]===255;}
    assert.ok(transparent>100&&opaque>100,'Source fence mask must retain its openings and opaque wire');
  }else assert.ok(colors.size>8,`Blank or constant source texture: ${item.file}`);
  count++;
}
assert.equal(count,14);assert.equal(metadata.texlib.names[metadata.texlib.layers.gel],'gel');
for(const stage of ['tidewater','kelpline','halyard','saltpan','crossmarket','lockgate','terraces','cargo'])assert.ok(metadata.murals[stage].murals.length>=4,`Source mural table missing ${stage}`);
console.log(`${count} source GPU/Canvas texture packs passed dimensions, content hashes, source hashes and nonblank checks.`);
