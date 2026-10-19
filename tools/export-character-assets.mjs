import { register } from 'node:module';
import { execFileSync } from 'node:child_process';
import { readFile, writeFile, mkdir } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { resolve } from 'node:path';

const root=resolve(import.meta.dirname,'..'), directory=resolve(root,'assets/reference/characters');
const check=process.argv.includes('--check');
if(process.argv.slice(2).some(arg=>arg!=='--check')) throw new Error('Usage: node tools/export-character-assets.mjs [--check]');
execFileSync(process.execPath,['tools/export-reference.mjs','--check'],{cwd:root,stdio:'pipe'});
register(new URL('./reference-loader.mjs',import.meta.url));
await import('../splat-ref/src/game/kits/index.js');
const THREE=await import('three');
const { Character }=await import('../splat-ref/src/game/character.js');
const GEO=await import('../splat-ref/src/game/character-geo.js');
const STYLE=await import('../splat-ref/src/game/character-style.js');
const { G }=await import('../splat-ref/src/core/ctx.js');
const { WEAPONS,WEAPON_ORDER,SUBS,SUB_ORDER }=await import('../splat-ref/src/config.js');
const { SPECIAL_PROP_KINDS,getSpecialProp }=await import('../splat-ref/src/game/special-props.js');
G.quality='high';G.settings={quality:'high'};
await mkdir(directory,{recursive:true});

class Writer {
    chunks=[];
    raw(value){this.chunks.push(Buffer.from(value));}
    u32(value){if(!Number.isInteger(value)||value<0||value>0xffffffff)throw new Error(`Invalid integer ${value}`);const b=Buffer.alloc(4);b.writeUInt32LE(value);this.raw(b);}
    i32(value){const b=Buffer.alloc(4);b.writeInt32LE(value);this.raw(b);}
    number(value){if(!Number.isFinite(value))throw new Error('Non-finite asset');const b=Buffer.alloc(8);b.writeDoubleLE(value);this.raw(b);}
    string(value){const b=Buffer.from(value,'utf8');this.u32(b.length);this.raw(b);}
    vec(values){for(const value of values)this.number(value);}
    bytes(){return Buffer.concat(this.chunks);}
}
async function artifact(name,bytes){
    const target=resolve(directory,name);
    if(check){if(!(await readFile(target)).equals(bytes))throw new Error(`Stale character asset: ${name}`);}
    else await writeFile(target,bytes);
}
const meshes=new Map(), pending=[];
function geometry(g){
    if(!g || !g.isBufferGeometry) return '';
    const writer=new Writer(),count=g.attributes.position.count;
    writer.raw('TSMESH01');writer.u32(count);
    const indices=g.index?Array.from(g.index.array):Array.from({length:count},(_,i)=>i);
    writer.u32(indices.length);
    const attrs=Object.entries(g.attributes).sort(([a],[b])=>a.localeCompare(b));
    writer.u32(attrs.length);
    for(const [name,attr] of attrs){
        if(attr.count!==count || attr.itemSize<1 || attr.itemSize>4)throw new Error(`Invalid source attribute ${name}`);
        writer.string(name);writer.u32(attr.itemSize);
        writer.string(attr.array?.constructor.name||'Interleaved');writer.u32(attr.normalized?1:0);
        const data=Buffer.alloc(count*attr.itemSize*4);
        for(let i=0;i<count;i++)for(let j=0;j<attr.itemSize;j++){
            const value=attr.getComponent(i,j);
            if(!Number.isFinite(value))throw new Error(`Non-finite source attribute ${name}`);
            data.writeFloatLE(value,(i*attr.itemSize+j)*4);
        }
        writer.raw(data);
    }
    const indexBytes=Buffer.alloc(indices.length*4);
    for(let i=0;i<indices.length;i++){
        if(indices[i]<0||indices[i]>=count)throw new Error('Source index out of range');
        indexBytes.writeUInt32LE(indices[i],i*4);
    }
    writer.raw(indexBytes);
    const bytes=writer.bytes(),hash=createHash('sha256').update(bytes).digest('hex'),file=hash+'.mesh';
    if(!meshes.has(file)){
        meshes.set(file,{file,hash,vertices:count,indices:indices.length,attributes:attrs.map(([name,a])=>({name,size:a.itemSize,sourceType:a.array?.constructor.name||'Interleaved',normalized:a.normalized}))});
        pending.push(artifact(file,bytes));
    }
    return file;
}
const transform=o=>({position:o.position.toArray(),rotation:o.quaternion.toArray(),scale:o.scale.toArray(),visible:o.visible,
    authoredMatrix:o.matrixAutoUpdate?null:o.matrix.toArray()});
function semantics(c){
    const map=new Map([[c.root,'root'],[c.model,'model'],[c.kid,'kid'],[c.squidRoot,'squid'],[c.tank.group,'tank/group'],[c.tank.fill,'tank/fill'],[c.bomb.group,'sub/group']]);
    function tree(node,path){if(!node)return;if(!map.has(node))map.set(node,path);const binding=map.get(node);node.children.forEach((child,i)=>tree(child,`${binding}/${i}`));}
    for(const [side,w] of [['right',c.weapon],['left',c.weapon?.left]])if(w){
        for(const key of ['pivot','off','body','ink','bodyFar','inkFar','glow','drum','spin','muzzle'])if(w[key])map.set(w[key],`weapon/${side}/${key}`);
        for(const [key,node] of Object.entries(w.parts))tree(node,`weapon/${side}/parts/${key}`);
        tree(w.pivot,`weapon/${side}/pivot`);
    }
    if(c.weapon?.pivotL)tree(c.weapon.pivotL,'weapon/mirror');
    tree(c.squidRoot,'squid');tree(c.bomb.group,'sub/group');tree(c.tank.group,'tank/group');
    return map;
}
function material(c,m){
    if(m.name==='iw-brolly-ink')return 'brollyInk';
    if(m.name==='iw-brolly-cream')return 'brollyCream';
    if(m.customProgramCacheKey?.()==='iw-blade-edge-1')return 'bladeEdge';
    if(m.name==='iw-sponge')return 'sponge';
    if(m===c.weapon.bl?.am)return 'bladeAura';
    for(const [key,value] of Object.entries(c.mats))if(value===m)return key;
    if(m===c.tank.glass.material)return 'glass';
    const weapons=[c.weapon,c.weapon?.left].filter(Boolean);
    for(const w of weapons){
        if(m===w.body?.material||m===w.bodyFar?.material)return 'plastic';
        if(m===w.ink?.material||m===w.inkFar?.material)return 'ink';
        if(m===w.coil)return 'coil';
        if(w.lamps.includes(m))return 'lamp';
    }
    if(m===c.bomb.ink.material||m===c.bomb.spinInk?.material)return 'ink';
    if(m===c.bomb.group.children[0]?.material)return 'plastic';
    if(m.userData?.iwBase) return material(c,m.userData.iwBase);
    // Shared weapon plastic/ink materials recur on drum, spinner and sub parts.
    if(m.vertexColors && m.type==='MeshPhysicalMaterial')return 'plastic';
    if(m.type==='MeshPhysicalMaterial' && m.color?.equals(c.color))return 'ink';
    throw new Error(`Unclassified source material ${c.weapon.def.kind}: ${m.type}/${m.name} color=${m.color?.getHexString()} emissive=${m.emissive?.getHexString()}`);
}
function forest(c,roots,exclude=new Set(),lod=''){
    const bindings=semantics(c),nodes=[],indices=new Map(),offAliases=new Map();
    for(const [side,w] of [['right',c.weapon],['left',c.weapon?.left]]) if(w?.off){
        const visitAlias=(o,path)=>{offAliases.set(o,path);o.children.forEach((child,i)=>visitAlias(child,`${path}/${i}`));};
        visitAlias(w.off,`weapon/${side}/off`);
    }
    function visit(o,parent=-1,bone=-1,path='root'){
        if(exclude.has(o))return;
        if(o.isBone){const id=GEO.BONE_NAMES.indexOf(o.name);if(id<0)throw new Error(`Unknown bone ${o.name}`);o.children.forEach((child,i)=>visit(child,parent,id,`${path}/${i}`));return;}
        if(lod && o.name.startsWith('kid:')&&!o.name.endsWith(':'+lod))return;
        const current=nodes.length;
        const binding=bindings.get(o)||path;
        indices.set(o,current);
        nodes.push({parent,bone,binding,name:o.name,renderOrder:o.renderOrder,local:transform(o),mesh:o.isMesh?geometry(o.geometry):'',
            material:o.isMesh?material(c,o.material):'',skinned:!!o.isSkinnedMesh,bind:o.isSkinnedMesh?o.bindMatrix.toArray():new THREE.Matrix4().toArray()});
        o.children.forEach((child,i)=>visit(child,current,-1,`${binding}/${i}`));
    }
    for(const [i,o] of roots.entries())if(o)visit(o,-1,o.parent?.isBone?GEO.BONE_NAMES.indexOf(o.parent.name):-1,`root/${i}`);
    const aliasIndices=new Map();
    for(const [o,path] of offAliases){
        const sourceIndex=indices.get(o);
        if(sourceIndex===undefined)continue;
        if(bindings.get(o)===path){aliasIndices.set(path,sourceIndex);continue;}
        const slash=path.lastIndexOf('/'),parentPath=path.slice(0,slash),parent=aliasIndices.get(parentPath);
        if(parent===undefined)throw new Error(`Missing source part alias parent ${parentPath}`);
        const source=nodes[sourceIndex];
        aliasIndices.set(path,nodes.length);
        nodes.push({...source,parent,bone:-1,binding:path,name:`__alias__:${bindings.get(o)||''}`,mesh:'',skinned:false,bind:new THREE.Matrix4().toArray()});
    }
    return nodes;
}
const rigs=[],models=[];
for(let hair=0;hair<8;hair++)for(let hat=0;hat<4;hat++)for(let brows=0;brows<4;brows++){
    const style={hair,hat,brows,skin:0,outfit:0,eyes:0};
    const rest=GEO.getRestPositions(style),inverse=GEO.getBoneInverses(style),hm=GEO.getHairStyle(style).meta;
    rigs.push({hair,hat,brows,bones:GEO.BONE_NAMES.map((name,i)=>({name,parent:GEO.BONE_PARENT[name]?GEO.BONE_NAMES.indexOf(GEO.BONE_PARENT[name]):-1,rest:rest[name].toArray(),inverse:inverse[i].toArray()})),hairMeta:hm.map(m=>({direction:m.dir.toArray(),length:m.len,stiffness:m.K,gravity:m.G}))});
    const c=new Character({name:'native-assets',weapon:'shooter',style,isLocal:true});
    for(const lod of ['hero','game','far']){
        c.setLod(lod);c.root.updateMatrixWorld(true);
        const exclude=new Set([c.weapon.pivot,c.weapon.left?.pivot,c.weapon.pivotL,c.bomb.group].filter(Boolean));
        models.push({id:`body/${hair}/${hat}/${brows}/${lod}`,nodes:forest(c,[c.root],exclude,lod)});
    }
    c.dispose();
    if(!check && hat===0 && brows===0)console.log(`character hair ${hair+1}/8`);
}
const c=new Character({name:'native-assets',isLocal:true,style:{hair:0,hat:0,brows:0,skin:0,outfit:0,eyes:0}});
for(const id of WEAPON_ORDER){
    c.setWeapon(WEAPONS[id].kind);c._animWeapon(0,{},c.weapon);
    models.push({id:'main/'+id,nodes:forest(c,[c.weapon.pivot,c.weapon.left?.pivot,c.weapon.pivotL].filter(Boolean))});
}
for(const id of SUB_ORDER){c.setSub(SUBS[id].kind);models.push({id:'sub/'+id,nodes:forest(c,[c.bomb.group])});}
c.dispose();
function plain(value){
    if(value==null||typeof value==='number'||typeof value==='string'||typeof value==='boolean')return value;
    if(value.isBufferGeometry)return {mesh:geometry(value)};
    if(value.toArray)return value.toArray();
    if(Array.isArray(value))return value.map(plain);
    if(typeof value==='object')return Object.fromEntries(Object.entries(value).filter(([,v])=>typeof v!=='function').map(([key,v])=>[key,plain(v)]));
    throw new Error('Unsupported source metadata');
}
const specials=Object.fromEntries(SPECIAL_PROP_KINDS.map(kind=>[kind,plain(getSpecialProp(kind))]));
const catalog={schema:2,bones:GEO.BONE_NAMES,rigs,models,specials,meshes:[...meshes.values()],
    styles:plain({skin:STYLE.SKIN_TONES,outfits:STYLE.OUTFITS,iris:STYLE.IRIS,hairNames:STYLE.HAIR_STYLE_NAMES,hats:STYLE.HATS,brows:STYLE.BROWS,presets:STYLE.PRESETS})};
const out=new Writer();out.raw('TSCHAR02');out.u32(rigs.length);
for(const rig of rigs){
    out.u32(rig.hair);out.u32(rig.hat);out.u32(rig.brows);out.u32(rig.bones.length);
    for(const bone of rig.bones){out.string(bone.name);out.i32(bone.parent);out.vec(bone.rest);out.vec(bone.inverse);}
    out.u32(rig.hairMeta.length);for(const strand of rig.hairMeta){out.vec(strand.direction);out.vec([strand.length,strand.stiffness,strand.gravity]);}
}
out.u32(models.length);
for(const model of models){
    out.string(model.id);out.u32(model.nodes.length);
    for(const node of model.nodes){out.i32(node.parent);out.i32(node.bone);out.string(node.binding);out.string(node.name);out.string(node.mesh);out.string(node.material);out.i32(node.renderOrder);
        out.vec(node.local.position);out.vec(node.local.rotation);out.vec(node.local.scale);out.u32(node.local.visible?1:0);
        out.u32(node.local.authoredMatrix?1:0);if(node.local.authoredMatrix)out.vec(node.local.authoredMatrix);
        out.u32(node.skinned?1:0);out.vec(node.bind);}
}
await Promise.all(pending);
await artifact('catalog.bin',out.bytes());
await artifact('catalog.json',Buffer.from(JSON.stringify(catalog,null,2)+'\n'));
console.log(`${check?'Verified':'Exported'} ${rigs.length} rigs, ${models.length} models, ${meshes.size} unique source meshes, ${SPECIAL_PROP_KINDS.length} special props`);
