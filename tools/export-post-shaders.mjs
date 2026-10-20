import { register } from 'node:module';
import { readFile, mkdir, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { createHash } from 'node:crypto';
register(new URL('./reference-loader.mjs', import.meta.url));
const THREE = await import('three');
const { GTAOShader, generateMagicSquareNoise } = await import('../splat-ref/node_modules/three/examples/jsm/shaders/GTAOShader.js');
const { PoissonDenoiseShader, generatePdSamplePointInitializer } = await import('../splat-ref/node_modules/three/examples/jsm/shaders/PoissonDenoiseShader.js');
const { OutputShader } = await import('../splat-ref/node_modules/three/examples/jsm/shaders/OutputShader.js');
const { LuminosityHighPassShader } = await import('../splat-ref/node_modules/three/examples/jsm/shaders/LuminosityHighPassShader.js');
const { UnrealBloomPass } = await import('../splat-ref/node_modules/three/examples/jsm/postprocessing/UnrealBloomPass.js');
const out = resolve(process.argv[2] || 'assets/reference/post');
await mkdir(out, { recursive: true });
function expand(s) { return s.replace(/#include\s+<([\w]+)>/g, (_, n) => { if (!THREE.ShaderChunk[n]) throw Error(n); return expand(THREE.ShaderChunk[n]); }); }
function fragment(s, defs = {}) {
  const weights=THREE.ColorManagement.getLuminanceCoefficients(new THREE.Vector3()).toArray().map(x=>x.toFixed(4));
  return '#version 450 core\n#define varying in\n#define texture2D texture\n#define textureCube texture\nlayout(location=0) out vec4 postColor;\n#define gl_FragColor postColor\n' +
    Object.entries(defs).map(([k,v]) => `#define ${k} ${v}\n`).join('') + `float luminance(const in vec3 rgb){return dot(vec3(${weights.join(',')}),rgb);}\n` + expand(s).replace(/\bprecision\s+\w+\s+\w+\s*;/g, '').replace(/\b(highp|mediump|lowp)\b/g, '');
}
const renderer = await readFile(new URL('../splat-ref/src/core/renderer.js', import.meta.url), 'utf8');
const screen = await readFile(new URL('../splat-ref/src/fx/screenfx.js', import.meta.url), 'utf8');
const grade = renderer.match(/fragmentShader:\s*\/\* glsl \*\/`([\s\S]*?)`/)[1];
const screenFrag = screen.match(/const FRAG = \/\* glsl \*\/`([\s\S]*?)`;/)[1];
const lensVertex=screen.match(/const LENS_VERT = \/\* glsl \*\/`([\s\S]*?)`;/)[1];
const lensFragment=screen.match(/const LENS_FRAG = \/\* glsl \*\/`([\s\S]*?)`;/)[1];
const bloom = new UnrealBloomPass(new THREE.Vector2(16, 16), .28, .45, 2.4);
const files = {
  'THREE-LICENSE.txt': await readFile(new URL('../splat-ref/node_modules/three/LICENSE', import.meta.url)),
  'grade.frag': fragment(grade),
  'screen.frag': fragment(screenFrag, { TAPS: 8 }),
  'lens.vert': '#version 450 core\n#define attribute in\n#define varying out\nin vec3 position;\n'+lensVertex,
  'lens.frag': fragment(lensFragment),
  'output.frag': fragment(OutputShader.fragmentShader, { NEUTRAL_TONE_MAPPING: '', SRGB_TRANSFER: '' }),
  'gtao.frag': fragment(GTAOShader.fragmentShader, { ...GTAOShader.defines, SAMPLES: 12, NORMAL_VECTOR_TYPE: 0 }),
  'denoise.frag': fragment(PoissonDenoiseShader.fragmentShader, { ...PoissonDenoiseShader.defines, NORMAL_VECTOR_TYPE: 0, SAMPLE_VECTORS: generatePdSamplePointInitializer(16, 2, 1) }),
  'highpass.frag': fragment(LuminosityHighPassShader.fragmentShader),
  'bloom-composite.frag': fragment(bloom.compositeMaterial.fragmentShader, bloom.compositeMaterial.defines),
};
for (let i=0;i<5;i++) files[`blur-${i}.frag`] = fragment(bloom.separableBlurMaterials[i].fragmentShader, bloom.separableBlurMaterials[i].defines);
const noise = generateMagicSquareNoise();
files['gtao-noise.bin'] = Buffer.from(noise.image.data);
// The source denoiser's simplex-noise texture is a fixed fixture, independent of gameplay RNG.
const { SimplexNoise } = await import('../splat-ref/node_modules/three/examples/jsm/math/SimplexNoise.js');
let seed=0x16ac19f1;
const random={random(){ seed=(Math.imul(seed,1664525)+1013904223)>>>0;return seed/4294967296; }};
const simplex=new SimplexNoise(random), pd=new Uint8Array(64*64*4);
for(let i=0;i<64;i++)for(let j=0;j<64;j++)for(let c=0;c<4;c++)pd[(i*64+j)*4+c]=(simplex.noise(i+(c%2)*64,j+Math.floor(c/2)*64)*.5+.5)*255;
files['denoise-noise.bin']=Buffer.from(pd);
files['bloom-kernels.json']=JSON.stringify(bloom.separableBlurMaterials.map(m=>Object.fromEntries(['centerWeight','gaussianOffsets','gaussianWeights'].map(k=>[k,m.uniforms[k].value]))),null,2)+'\n';
const hashes={},sourceHashes={};
for(const source of ['src/core/renderer.js','src/fx/screenfx.js','src/world/environment.js','node_modules/three/examples/jsm/shaders/GTAOShader.js','node_modules/three/examples/jsm/shaders/PoissonDenoiseShader.js','node_modules/three/examples/jsm/shaders/OutputShader.js','node_modules/three/examples/jsm/postprocessing/UnrealBloomPass.js'])sourceHashes[source]=createHash('sha256').update(await readFile(new URL('../splat-ref/'+source,import.meta.url))).digest('hex');
for(const [name,data] of Object.entries(files)){await writeFile(resolve(out,name),data);hashes[name]=createHash('sha256').update(data).digest('hex');}
await writeFile(resolve(out,'manifest.json'),JSON.stringify({source:'renderer.js / screenfx.js / Three.js r186 postprocessing',sourceHashes,hashes},null,2)+'\n');
bloom.dispose(); noise.dispose();
console.log(`Exported ${Object.keys(files).length} post assets to ${out}`);
