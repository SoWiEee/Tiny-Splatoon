import { register } from 'node:module';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { deflateSync } from 'node:zlib';
import { resolve } from 'node:path';
import { referenceBrowser } from './reference-browser-session.mjs';

const root = resolve(import.meta.dirname, '..');
const output = resolve(root, 'assets/reference/boss/shaders');
const check = process.argv.includes('--check');
if (process.argv.slice(2).some((arg) => arg !== '--check')) throw new Error('Usage: node tools/export-boss-materials.mjs [--check]');
execFileSync(process.execPath, ['tools/export-reference.mjs', '--check'], { cwd: root, stdio: 'pipe' });
register(new URL('./reference-loader.mjs', import.meta.url));
const THREE = await import('three');
const { buildSourceMaterialShader } = await import('./export-character-shaders.mjs');
const { makeUniforms, makeBossMaterials, makeCrabletMaterial } = await import('../splat-ref/src/boss/bossMats.js');

const hash = (bytes) => createHash('sha256').update(bytes).digest('hex');
const uniforms = makeUniforms();
const materials = { ...makeBossMaterials(uniforms), crablet: makeCrabletMaterial(uniforms) };
const artifacts = new Map();
function uniformText(uniforms) {
  const lines = [];
  const flatten = (value, out) => {
    if (typeof value === 'number' || typeof value === 'boolean') out.push(Number(value));
    else if (Array.isArray(value) || ArrayBuffer.isView(value)) for (const item of value) flatten(item, out);
    else if (value?.toArray) flatten(value.toArray(), out);
  };
  for (const [name, value] of Object.entries(uniforms)) {
    const values = [];
    flatten(value, values);
    if (values.length && values.every(Number.isFinite)) lines.push(`${name} ${values.length} ${values.join(' ')}`);
  }
  return `${lines.join('\n')}\n`;
}

for (const [name, material] of Object.entries(materials)) {
  const shader = buildSourceMaterialShader(material, { toneMapping: false, linearOutput: true, skinned: true });
  if (!shader.vertex.includes('attribute vec4 aM;') || !shader.fragment.includes('uFlash') || !shader.vertex.includes('boneTexture'))
    throw new Error(`Incomplete source Boss material shader: ${name}`);
  artifacts.set(`${name}.vert`, shader.vertex);
  artifacts.set(`${name}.frag`, shader.fragment);
  artifacts.set(`${name}.uniforms`, uniformText(shader.uniforms));
  material.userData = { digest: shader.digest, uniforms: shader.uniforms };
}

const browser = await referenceBrowser(root, 'source-boss-stencil-browser');
let stencil;
try {
  const image = await browser.evaluate(`(async()=>{
    const {makeStencilTexture}=await import('/src/boss/bossMats.js');const source=makeStencilTexture();await source.ready;await document.fonts.ready;
    await new Promise(resolve=>requestAnimationFrame(()=>requestAnimationFrame(resolve)));
    const canvas=source.tex.image,bytes=canvas.getContext('2d').getImageData(0,0,canvas.width,canvas.height).data;
    let s='';for(let i=0;i<bytes.length;i+=0x8000)s+=String.fromCharCode(...bytes.subarray(i,i+0x8000));
    return {width:canvas.width,height:canvas.height,srgb:source.tex.colorSpace==='srgb',pixels:btoa(s)};
  })()`);
  if (image.width !== 1024 || image.height !== 1024 || image.srgb) throw new Error('Invalid source Boss stencil atlas');
  const top = Buffer.from(image.pixels, 'base64'), bottom = Buffer.alloc(top.length), stride = image.width * 4;
  if (top.length !== image.width * image.height * 4) throw new Error('Truncated source Boss stencil atlas');
  for (let row = 0; row < image.height; row++) top.copy(bottom, row * stride, (image.height - 1 - row) * stride, (image.height - row) * stride);
  const header = Buffer.alloc(24);
  header.write('TSTEX001'); header.writeUInt32LE(image.width, 8); header.writeUInt32LE(image.height, 12);
  header.writeUInt32LE(1, 16); header.writeUInt32LE(0, 20);
  stencil = Buffer.concat([header, deflateSync(bottom, { level: 9 })]);
  artifacts.set('stencil.tex.z', stencil);
} finally { await browser.close(); }

const sourceFiles = ['splat-ref/src/boss/bossMats.js', 'tools/export-character-shaders.mjs'];
const manifest = Buffer.from(`${JSON.stringify({
  version: 1,
  source: Object.fromEntries(await Promise.all(sourceFiles.map(async (file) => [file, hash(await readFile(resolve(root, file)))]))),
  stencil: { file: 'stencil.tex.z', width: 1024, height: 1024, sha256: hash(stencil) },
  materials: Object.entries(materials).map(([name, material]) => ({
    name, sourceKey: material.customProgramCacheKey(), digest: material.userData.digest,
    vertex: `${name}.vert`, fragment: `${name}.frag`, uniforms: `${name}.uniforms`,
  })),
}, null, 2)}\n`);
artifacts.set('manifest.json', manifest);

await mkdir(output, { recursive: true });
for (const [name, content] of artifacts) {
  const file = resolve(output, name), bytes = Buffer.isBuffer(content) ? content : Buffer.from(content);
  if (check) {
    const current = await readFile(file).catch(() => null);
    if (!current?.equals(bytes)) throw new Error(`Stale source Boss material: ${name}`);
  } else await writeFile(file, bytes);
}
console.log(`${check ? 'Checked' : 'Exported'} ${Object.keys(materials).length} source Boss material shader sets and stencil atlas.`);
