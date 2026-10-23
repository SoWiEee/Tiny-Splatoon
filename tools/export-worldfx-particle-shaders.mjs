import { createHash } from 'node:crypto';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { register } from 'node:module';
import { resolve } from 'node:path';

register(new URL('./reference-loader.mjs', import.meta.url));
const { buildSourceMaterialShader } = await import('./export-character-shaders.mjs');
const THREE = await import('three');
const { FX } = await import('../splat-ref/src/fx/fx.js');

function meshBytes(geometry, name) {
  const position = geometry.getAttribute('position'), index = geometry.index;
  if (!position || position.itemSize !== 3 || !(position.array instanceof Float32Array))
    throw new Error(`Source world FX base mesh must have Float32 vec3 positions: ${name}`);
  const vertices = position.count, indices = index?.count ?? vertices;
  if (!vertices || vertices > 10_000_000 || !indices || indices > 30_000_000 || indices % 3)
    throw new Error('Invalid source world FX base mesh dimensions');
  const bytes = Buffer.allocUnsafe(16 + vertices * 12 + indices * 4);
  bytes.write('TSWFX001', 0, 'ascii');
  bytes.writeUInt32LE(vertices, 8);
  bytes.writeUInt32LE(indices, 12);
  let at = 16;
  for (const value of position.array) {
    if (!Number.isFinite(value)) throw new Error('Non-finite source world FX base mesh vertex');
    bytes.writeFloatLE(value, at); at += 4;
  }
  for (let i = 0; i < indices; ++i) {
    const value = index ? index.getX(i) : i;
    if (!Number.isInteger(value) || value < 0 || value >= vertices) throw new Error('Source world FX base mesh index out of range');
    bytes.writeUInt32LE(value, at); at += 4;
  }
  return bytes;
}

const args = process.argv.slice(2), check = args.includes('--check'), paths = args.filter(arg => arg !== '--check');
if (paths.length > 1 || paths.some(arg => arg.startsWith('--'))) throw new Error('Usage: node tools/export-worldfx-particle-shaders.mjs [output-directory] [--check]');
const output = resolve(paths[0] ?? 'assets/reference/worldfx-particles');
if (!check) await mkdir(output, { recursive: true });
const source = await readFile(new URL('../splat-ref/src/fx/fx.js', import.meta.url));
const scene = new THREE.Scene(), fx = new FX(scene, { quality: 1 });
const pools = [
  ['drops', fx.dMesh], ['puffs', fx.puffs.mesh], ['glows', fx.glows.mesh],
  ['rings', fx.rings.mesh], ['shells', fx.shells.mesh], ['beams', fx.beams.mesh],
];
const entries = [];
async function artifact(name, content) {
  const path = resolve(output, name);
  if (check) {
    const saved = await readFile(path);
    const expected = Buffer.isBuffer(content) ? content : Buffer.from(content);
    if (!saved.equals(expected)) throw new Error(`Stale source world FX particle asset: ${name}`);
  } else await writeFile(path, content);
}

try {
  for (const [name, mesh] of pools) {
    if (!mesh?.isMesh || !mesh.material?.isShaderMaterial) throw new Error(`Missing source world FX ShaderMaterial: ${name}`);
    const shader = buildSourceMaterialShader(mesh.material, { toneMapping: false, linearOutput: true });
    const geometry = meshBytes(mesh.geometry, name);
    await artifact(`${name}.vert`, shader.vertex);
    await artifact(`${name}.frag`, shader.fragment);
    await artifact(`${name}.mesh`, geometry);
    entries.push({
      name,
      vertex: `${name}.vert`, fragment: `${name}.frag`, mesh: `${name}.mesh`,
      sourceKey: shader.sourceKey, digest: shader.digest, uniforms: shader.uniforms,
      renderState: {
        ...shader.renderState,
        alphaToCoverage: mesh.material.alphaToCoverage,
        polygonOffset: mesh.material.polygonOffset,
        polygonOffsetFactor: mesh.material.polygonOffsetFactor,
        polygonOffsetUnits: mesh.material.polygonOffsetUnits,
        renderOrder: mesh.renderOrder,
        frustumCulled: mesh.frustumCulled,
      },
    });
  }
  const manifest = JSON.stringify({
    version: 1,
    generator: 'source FX ShaderMaterial + buildSourceMaterialShader + source base geometry',
    source: { path: 'splat-ref/src/fx/fx.js', sha256: createHash('sha256').update(source).digest('hex') },
    entries,
  }, null, 2) + '\n';
  await artifact('manifest.json', manifest);
  console.log(`${check ? 'Verified' : 'Exported'} ${entries.length} source world FX particle shaders and meshes -> ${output}`);
} finally {
  fx.dispose();
}
