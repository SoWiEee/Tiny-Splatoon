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
const popOnly = process.argv.includes('--crablet-pop-only');
if (process.argv.slice(2).some((arg) => arg !== '--check' && arg !== '--crablet-pop-only'))
  throw new Error('Usage: node tools/export-boss-materials.mjs [--check] [--crablet-pop-only]');
execFileSync(process.execPath, ['tools/export-reference.mjs', '--check'], { cwd: root, stdio: 'pipe' });
register(new URL('./reference-loader.mjs', import.meta.url));
const THREE = await import('three');
const { buildSourceMaterialShader } = await import('./export-character-shaders.mjs');
const { makeUniforms, makeBossMaterials, makeCrabletMaterial } = await import('../splat-ref/src/boss/bossMats.js');
const { BossFX } = await import('../splat-ref/src/boss/bossModelFx.js');

const hash = (bytes) => createHash('sha256').update(bytes).digest('hex');
const uniforms = makeUniforms();
const materials = { ...makeBossMaterials(uniforms), crablet: makeCrabletMaterial(uniforms) };
const artifacts = new Map();
function popBlobMeshBytes(geometry) {
  const position = geometry.getAttribute('position'), normal = geometry.getAttribute('normal');
  const uv = geometry.getAttribute('uv'), index = geometry.index;
  if (!position || !normal || !uv || position.itemSize !== 3 || normal.itemSize !== 3 || uv.itemSize !== 2 ||
      position.count !== normal.count || position.count !== uv.count)
    throw new Error('Source BossFX low-quality icosahedron has an unsupported vertex layout');
  const vertices = position.count, indices = index?.count ?? vertices;
  if (!vertices || vertices > 100000 || !indices || indices > 300000 || indices % 3)
    throw new Error('Invalid source BossFX pop-blob mesh dimensions');
  const bytes = Buffer.alloc(16 + vertices * 32 + indices * 4);
  bytes.write('TSBOSSFX', 0, 'ascii');
  bytes.writeUInt32LE(vertices, 8); bytes.writeUInt32LE(indices, 12);
  let at = 16;
  for (let i = 0; i < vertices; ++i) for (const attribute of [position, normal, uv])
    for (let c = 0; c < attribute.itemSize; ++c) {
      const value = attribute.getComponent(i, c);
      if (!Number.isFinite(value)) throw new Error('Non-finite source BossFX pop-blob vertex');
      bytes.writeFloatLE(value, at); at += 4;
    }
  for (let i = 0; i < indices; ++i) {
    const value = index ? index.getX(i) : i;
    if (!Number.isInteger(value) || value < 0 || value >= vertices) throw new Error('Invalid source BossFX pop-blob index');
    bytes.writeUInt32LE(value, at); at += 4;
  }
  return bytes;
}
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

const sourcePopFx = new BossFX({ quality: 'low', ink: new THREE.Color(1, 1, 1) });
const popBlobGeometry = sourcePopFx.inkMesh.geometry;
const popBlobMaterial = sourcePopFx.inkMat;
const popBlobShader = buildSourceMaterialShader(popBlobMaterial, { toneMapping: false, linearOutput: true });
const popBlobMesh = popBlobMeshBytes(popBlobGeometry);
artifacts.set('crablet-pop-blob.vert', popBlobShader.vertex);
artifacts.set('crablet-pop-blob.frag', popBlobShader.fragment);
artifacts.set('crablet-pop-blob.uniforms', uniformText(popBlobShader.uniforms));
artifacts.set('crablet-pop-blob.mesh', popBlobMesh);

let stencil;
if (!popOnly) {
  const browser = await referenceBrowser(root, 'source-boss-stencil-browser');
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
}

const sourceFiles = [
  'splat-ref/src/boss/bossMats.js', 'splat-ref/src/boss/bossModel.js',
  'splat-ref/src/boss/bossModelFx.js', 'tools/export-character-shaders.mjs',
];
const sourceHashes = Object.fromEntries(await Promise.all(sourceFiles.map(async (file) => [file, hash(await readFile(resolve(root, file)))])));
const popBlobRecord = {
  sourceBehavior: 'BossCrablet.update pop -> BossFX.ink, quality low',
  geometry: { type: 'THREE.IcosahedronGeometry', radius: 1, detail: 0, vertices: popBlobGeometry.getAttribute('position').count,
    indices: popBlobGeometry.index?.count ?? popBlobGeometry.getAttribute('position').count, file: 'crablet-pop-blob.mesh', sha256: hash(popBlobMesh) },
  material: { type: 'THREE.MeshPhysicalMaterial', roughness: popBlobMaterial.roughness, metalness: popBlobMaterial.metalness,
    clearcoat: popBlobMaterial.clearcoat, clearcoatRoughness: popBlobMaterial.clearcoatRoughness,
    vertex: 'crablet-pop-blob.vert', fragment: 'crablet-pop-blob.frag', uniforms: 'crablet-pop-blob.uniforms', digest: popBlobShader.digest },
  particles: { quality: 'low', sourceRandomSeed: 9001, count: Math.round(26 * 0.4), speed: 4.2, spread: 1, size: 0.09, life: 1.2 },
};
if (popOnly) {
  for (const name of ['crablet-pop-blob.vert', 'crablet-pop-blob.frag', 'crablet-pop-blob.uniforms', 'crablet-pop-blob.mesh']) {
    const content = artifacts.get(name); artifacts.delete(name); artifacts.set(name, content);
  }
  for (const name of [...artifacts.keys()]) if (!name.startsWith('crablet-pop-blob.')) artifacts.delete(name);
  const manifestFile = resolve(output, 'manifest.json');
  const existingManifest = JSON.parse(await readFile(manifestFile, 'utf8'));
  existingManifest.source = sourceHashes;
  existingManifest.crabletPopBlob = popBlobRecord;
  artifacts.set('manifest.json', `${JSON.stringify(existingManifest, null, 2)}\n`);
} else {
const manifest = Buffer.from(`${JSON.stringify({
  version: 1,
  source: sourceHashes,
  stencil: { file: 'stencil.tex.z', width: 1024, height: 1024, sha256: hash(stencil) },
  materials: Object.entries(materials).map(([name, material]) => ({
    name, sourceKey: material.customProgramCacheKey(), digest: material.userData.digest,
    vertex: `${name}.vert`, fragment: `${name}.frag`, uniforms: `${name}.uniforms`,
  })),
  crabletPopBlob: popBlobRecord,
}, null, 2)}\n`);
artifacts.set('manifest.json', manifest);
}

await mkdir(output, { recursive: true });
for (const [name, content] of artifacts) {
  const file = resolve(output, name), bytes = Buffer.isBuffer(content) ? content : Buffer.from(content);
  if (check) {
    const current = await readFile(file).catch(() => null);
    if (!current?.equals(bytes)) throw new Error(`Stale source Boss material: ${name}`);
  } else await writeFile(file, bytes);
}
sourcePopFx.dispose();
console.log(`${check ? 'Checked' : 'Exported'} ${popOnly ? 'source BossFX pop blob' : `${Object.keys(materials).length} source Boss material shader sets, BossFX pop blob, and stencil atlas`}.`);
