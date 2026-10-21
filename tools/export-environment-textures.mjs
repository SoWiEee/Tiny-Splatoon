import { createHash } from 'node:crypto';
import { deflateSync,inflateSync } from 'node:zlib';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { referenceBrowser } from './reference-browser-session.mjs';

const root = resolve(import.meta.dirname, '..'), directory = resolve(root, 'assets/reference/environment');
const check = process.argv.includes('--check');
if (process.argv.slice(2).some(arg => arg !== '--check')) throw new Error('Usage: node tools/export-environment-textures.mjs [--check]');
await mkdir(directory, { recursive: true });
const browser = await referenceBrowser(root, 'environment-export');
const files = {};
const capture = async expression => {
  const value = await browser.evaluate(`(()=>{const texture=${expression};const data=texture.data;const bytes=new Uint8Array(data.buffer,data.byteOffset,data.byteLength);let binary='';for(let i=0;i<bytes.length;i+=32768)binary+=String.fromCharCode(...bytes.subarray(i,i+32768));return {width:texture.width,height:texture.height,type:data.constructor.name,pixels:btoa(binary)}})()`);
  return { width: value.width, height: value.height, type: value.type, pixels: Buffer.from(value.pixels, 'base64') };
};
try {
  const gpu=await browser.evaluate(`(async()=>{
    const THREE=await import('three');const {Environment}=await import('/src/world/environment.js');
    class CaptureEnvironment extends Environment {
      _buildDock(){} _buildScenery(){this.lhBeam={visible:false};} _buildLife(){} _applyMarina(){} _fitShadow(){} _bakeFarReflection(){}
    }
    const renderer=new THREE.WebGLRenderer({antialias:false,preserveDrawingBuffer:true});
    renderer.setSize(64,64);renderer.outputColorSpace=THREE.LinearSRGBColorSpace;
    const scene=new THREE.Scene();
    window.__envCapture={renderer,env:new CaptureEnvironment(renderer,scene,{theme:'day'})};
    const gl=renderer.getContext(),ext=gl.getExtension('WEBGL_debug_renderer_info');return gl.getParameter(ext?ext.UNMASKED_RENDERER_WEBGL:gl.RENDERER);
  })()`);
  console.log(`Source environment capture GPU: ${gpu}`);
  files['wave.rgba8.z'] = await capture('({width:256,height:256,data:window.__envCapture.env.U.uWaveTex.value.image.data})');
  for (const theme of ['day', 'sunset', 'golden']) {
    if (theme !== 'day') await browser.evaluate(`window.__envCapture.env.setTheme('${theme}'); true`);
    const dimensions = await browser.evaluate(`(()=>{const e=window.__envCapture.env;return {cloud:[e._cloudRT.width,e._cloudRT.height],pmrem:[e._envRT.width,e._envRT.height]}})()`);
    for (const [kind, target] of [['cloud','_cloudRT'],['pmrem','_envRT']]) {
      const [width,height] = dimensions[kind];
      if (kind === 'pmrem' && (width !== 768 || height !== 1024)) throw new Error(`Unexpected source PMREM atlas ${width}×${height}`);
      files[`${kind}-${theme}.rgba16f.z`] = await capture(`(()=>{const c=window.__envCapture,r=c.renderer,rt=c.env.${target},a=new Uint16Array(${width}*${height}*4);r.readRenderTargetPixels(rt,0,0,${width},${height},a);return {width:${width},height:${height},data:a}})()`);
    }
    console.log(`Captured ${theme} cloud and PMREM.`);
  }
} finally { await browser.close(); }
const index = {};
const mismatches = [];
for (const [name, image] of Object.entries(files)) {
  const header=Buffer.alloc(20);
  header.write('TSENV001'); header.writeUInt32LE(image.width,8); header.writeUInt32LE(image.height,12); header.writeUInt32LE(image.type==='Uint16Array'?16:8,16);
  const raw=Buffer.concat([header,image.pixels]);
  const packed=deflateSync(raw,{level:9});
  index[name]={width:image.width,height:image.height,bits:image.type==='Uint16Array'?16:8,sha256:createHash('sha256').update(raw).digest('hex')};
  const path=resolve(directory,name);
  if(check){
    const current=await readFile(path);
    if(!packed.equals(current)){
      const expected=inflateSync(current);let changed=0,maxBits=0;
      for(let i=20;i<Math.min(raw.length,expected.length);i+=image.type==='Uint16Array'?2:1){const a=image.type==='Uint16Array'?raw.readUInt16LE(i):raw[i],b=image.type==='Uint16Array'?expected.readUInt16LE(i):expected[i];if(a!==b)changed++;maxBits=Math.max(maxBits,Math.abs(a-b));}
      await writeFile(resolve(root,'build',`environment-fresh-${name}`),packed);
      mismatches.push(`${name}: changed=${changed} maxRawDelta=${maxBits} bytes=${raw.length}/${expected.length}`);
    }
  }else await writeFile(path,packed);
}
const indexText=JSON.stringify(index,null,2)+'\n';
if(check){if((await readFile(resolve(directory,'textures.json'),'utf8')).replaceAll('\r\n','\n')!==indexText)mismatches.push('textures.json: raw texture hashes differ');}
else await writeFile(resolve(directory,'textures.json'),indexText);
if(mismatches.length)throw new Error(`Stale source environment textures:\n${mismatches.join('\n')}\nFresh captures saved under build/`);
console.log(`${check?'Checked':'Exported'} ${Object.keys(files).length} source environment textures.`);
