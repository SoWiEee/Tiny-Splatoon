import { readFile, writeFile, mkdir } from 'node:fs/promises';
import { deflateSync, inflateSync } from 'node:zlib';
import { createHash } from 'node:crypto';
import { resolve } from 'node:path';
import { referenceBrowser } from './reference-browser-session.mjs';
const root=resolve(import.meta.dirname,'..'),directory=resolve(root,'assets/reference/stage-textures');
const checking=process.argv.includes('--check');
if(process.argv.slice(2).some(v=>v!=='--check'))throw new Error('Usage: node tools/export-stage-textures.mjs [--check]');
await mkdir(directory,{recursive:true});
const browser=await referenceBrowser(root,'source-stage-texture-browser');
const metadata={schema:1,format:'TSTEX001 + uint32LE width,height,layers,sRGB + bottom-up RGBA8; zlib compressed',texlib:{},props:{},murals:{},sourceHashes:{}};
const hash=bytes=>createHash('sha256').update(bytes).digest('hex');
async function texture(file,width,height,layers,srgb,bytes){
  const header=Buffer.alloc(24);header.write('TSTEX001');header.writeUInt32LE(width,8);header.writeUInt32LE(height,12);header.writeUInt32LE(layers,16);header.writeUInt32LE(srgb?1:0,20);
  const raw=Buffer.concat([header,bytes]),packed=deflateSync(raw,{level:9});
  if(checking){
    const accepted=inflateSync(await readFile(resolve(directory,file)));
    if(!accepted.equals(raw))throw new Error(`Source texture byte mismatch: ${file}`);
  }else await writeFile(resolve(directory,file),packed);
  return {file,width,height,layers,srgb,sha256:hash(raw),bytes:packed.length};
}
try{
  const info=await browser.evaluate(`(async()=>{
    const THREE=await import('three');
    await document.fonts.load('120px "Titan One"');await document.fonts.load('800 100px Rubik');await document.fonts.ready;
    const {createTextureLibrary}=await import('/src/world/texlib.js');
    const renderer=new THREE.WebGLRenderer({antialias:false});renderer.setSize(512,512);renderer.outputColorSpace=THREE.SRGBColorSpace;
    const initTarget=renderer.initRenderTarget.bind(renderer);renderer.initRenderTarget=target=>{window.__textureTarget=target;return initTarget(target);};
    const lib=await createTextureLibrary(renderer,{size:512});
    window.__textureTarget.textures.forEach(texture=>{texture.generateMipmaps=false;});
    const {PropKit}=await import('/src/world/props.js');
    const kit=new PropKit(new THREE.Scene(),{castShadow:true,quality:'high'});
    await document.fonts.ready;await new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));
    window.__sourceTexture={THREE,renderer,lib,kit};
    window.__base64=bytes=>{let s='';for(let i=0;i<bytes.length;i+=0x8000)s+=String.fromCharCode(...bytes.subarray(i,i+0x8000));return btoa(s);};
    return {size:lib.size,names:lib.names,layers:lib.layers,meta:lib.meta,renderer:renderer.getContext().getParameter(renderer.getContext().RENDERER)};
  })()`);
  metadata.texlib={...info,textures:{}};
  console.log(`Source GPU texture library ready: ${info.names.length} layers at ${info.size}px.`);
  for(let channel=0;channel<3;channel++){
    const pixels=Buffer.alloc(info.size*info.size*4*info.names.length);
    for(let layer=0;layer<info.names.length;layer++){
      const encoded=await browser.evaluate(`(()=>{const {renderer}=window.__sourceTexture;const target=window.__textureTarget;if(!target)throw new Error('Source texture target is absent');renderer.setRenderTarget(target,${layer});const bytes=new Uint8Array(${info.size*info.size*4});renderer.readRenderTargetPixels(target,0,0,${info.size},${info.size},bytes,undefined,${channel});return window.__base64(bytes);})()`);
      Buffer.from(encoded,'base64').copy(pixels,layer*info.size*info.size*4);
    }
    const name=['albedo','normal','orm'][channel];
    metadata.texlib.textures[name]=await texture(`texlib-${name}.tex.z`,info.size,info.size,info.names.length,channel===0,pixels);
    console.log(`Captured source ${name} array.`);
  }
  async function canvasTexture(expression,file){
    const image=await browser.evaluate(`(()=>{const t=${expression};if(!t?.image?.getContext)throw new Error('Source canvas missing');const c=t.image,bytes=c.getContext('2d').getImageData(0,0,c.width,c.height).data;return {width:c.width,height:c.height,srgb:t.colorSpace==='srgb',pixels:window.__base64(bytes)};})()`);
    const top=Buffer.from(image.pixels,'base64'),bottom=Buffer.alloc(top.length),stride=image.width*4;
    for(let row=0;row<image.height;row++)top.copy(bottom,row*stride,(image.height-1-row)*stride,(image.height-row)*stride);
    return texture(file,image.width,image.height,1,image.srgb,bottom);
  }
  for(const [name,field] of [['atlas','atlas'],['chain','chain'],['blob','blobTex']])metadata.props[name]=await canvasTexture(`window.__sourceTexture.kit.${field}`,`props-${name}.tex.z`);
  const stages=['tidewater','kelpline','halyard','saltpan','crossmarket','lockgate','terraces','cargo'];
  for(const id of stages){
    const rows=await browser.evaluate(`(async()=>{const {createMuralTexture}=await import('/src/world/murals.js');window.__mural=await createMuralTexture(${JSON.stringify(id)});return window.__mural.userData.murals;})()`);
    metadata.murals[id]={...(await canvasTexture('window.__mural',`murals-${id}.tex.z`)),murals:rows};
    console.log(`Captured source ${id} murals.`);
  }
  for(const file of ['src/world/texlib.js','src/world/props.js','src/world/murals.js','assets/fonts/TitanOne-latin.woff2','assets/fonts/Rubik-latin.woff2'])metadata.sourceHashes[file]=hash(await readFile(resolve(root,'splat-ref',file)));
  const text=JSON.stringify(metadata,null,2)+'\n';
  if(checking){if((await readFile(resolve(directory,'metadata.json'),'utf8')).replace(/\r\n/g,'\n')!==text)throw new Error('Source texture metadata mismatch');}
  else await writeFile(resolve(directory,'metadata.json'),text);
  console.log(`${checking?'Checked':'Exported'} source surface, prop and mural textures.`);
}finally{await browser.close();}
