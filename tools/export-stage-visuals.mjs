import { register } from 'node:module';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { deflateSync } from 'node:zlib';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { dirname, relative, resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

const root = resolve(import.meta.dirname, '..');
register(new URL('./reference-loader.mjs', import.meta.url));
const THREE = await import('three');

// PropKit's canvas is only used for texture decoration. Its geometry remains the actual source PropKit mesh, so a
// minimal canvas/context lets its deterministic builders run headlessly without substituting geometry or source code.
function gradient() { return { addColorStop() {} }; }
const context = new Proxy({
  measureText: (text) => ({ width: String(text).length * 12 }),
  createLinearGradient: gradient,
  createRadialGradient: gradient,
}, {
  get(target, key) { return key in target ? target[key] : () => {}; },
  set(target, key, value) { target[key] = value; return true; },
});
globalThis.document = { createElement: () => ({ width: 0, height: 0, getContext: () => context }) };

const { MAPS } = await import('../splat-ref/src/config.js');
const { MAP_LAYOUTS } = await import('../splat-ref/src/world/maps.js');
const { dressingFor } = await import('../splat-ref/src/world/dressing.js');
const { STAGES } = await import('../splat-ref/src/world/stages/index.js');
const { inMode, layoutFor } = await import('../splat-ref/src/world/variants.js');
const { Level } = await import('../splat-ref/src/world/level.js');
const { PropKit } = await import('../splat-ref/src/world/props.js');
const { PaintSystem } = await import('../splat-ref/src/world/paint.js');
const { createLevelMaterial } = await import('../splat-ref/src/world/levelMaterial.js');
const { buildSourceMaterialShader } = await import('./export-character-shaders.mjs');

const check = process.argv.includes('--check');
const output = resolve(root, 'assets/reference/stages');
const atlasSize = 4096;
const shaderOptions = { shadows: true, toneMapping: false, linearOutput: true, materialMaps: true };

function serialUniforms(uniforms) {
  const lines = [];
  const flatten = (value, out) => {
    if (typeof value === 'number' || typeof value === 'boolean') out.push(Number(value));
    else if (Array.isArray(value)) for (const item of value) flatten(item, out);
    else if (ArrayBuffer.isView(value)) for (const item of value) flatten(item, out);
    else if (value?.toArray) flatten(value.toArray(), out);
  };
  for (const [name, value] of Object.entries(uniforms)) {
    const values = [];
    flatten(value, values);
    if (values.length && values.every(Number.isFinite)) lines.push(`${name} ${values.length} ${values.join(' ')}`);
  }
  return lines.join('\n') + '\n';
}

function interleaveLevel(g) {
  const attrs = ['position', 'normal', 'color', 'paintUv', 'lightUv', 'faceUv', 'faceData', 'faceFlags', 'faceTan'];
  const counts = [3, 3, 3, 2, 2, 2, 4, 3, 3];
  const stride = counts.reduce((a, b) => a + b, 0);
  const vertexCount = g.attributes.position.count;
  const out = new Float32Array(vertexCount * stride);
  for (let v = 0; v < vertexCount; v++) {
    let at = v * stride;
    for (let a = 0; a < attrs.length; a++) {
      const attribute = g.attributes[attrs[a]];
      for (let c = 0; c < counts[a]; c++) out[at++] = attribute.getComponent(v, c);
    }
  }
  return { stride, vertices: vertexCount, data: out, indices: Uint32Array.from(g.index.array) };
}

function propMaterialMeshes(kit) {
  const result = new Map();
  const add = (mesh, world, instanceColor = null) => {
    const materialKey = Object.keys(kit.mat).find((key) => kit.mat[key] === mesh.material);
    if (!materialKey) throw new Error(`Missing PropKit material identity for ${mesh.name}`);
    let target = result.get(materialKey);
    if (!target) { target = { positions: [], normals: [], colors: [], uvs: [], indices: [] }; result.set(materialKey, target); }
    const g = mesh.geometry, p = g.attributes.position, n = g.attributes.normal, c = g.attributes.color, uv = g.attributes.uv;
    const base = target.positions.length / 3;
    const normalMatrix = new THREE.Matrix3().getNormalMatrix(world);
    const point = new THREE.Vector3(), normal = new THREE.Vector3();
    const col = instanceColor || new THREE.Color(1, 1, 1);
    for (let i = 0; i < p.count; i++) {
      point.fromBufferAttribute(p, i).applyMatrix4(world);
      normal.fromBufferAttribute(n, i).applyMatrix3(normalMatrix).normalize();
      target.positions.push(point.x, point.y, point.z);
      target.normals.push(normal.x, normal.y, normal.z);
      const cr = c ? c.getX(i) : 1, cg = c ? c.getY(i) : 1, cb = c ? c.getZ(i) : 1;
      target.colors.push(cr * col.r, cg * col.g, cb * col.b);
      target.uvs.push(uv ? uv.getX(i) : 0, uv ? uv.getY(i) : 0);
    }
    if (g.index) for (const ix of g.index.array) target.indices.push(base + ix);
    else for (let i = 0; i < p.count; i++) target.indices.push(base + i);
  };
  kit.group.updateMatrixWorld(true);
  for (const mesh of kit.group.children) {
    if (!(mesh.isMesh || mesh.isInstancedMesh) || !mesh.geometry?.attributes?.position) continue;
    if (mesh.isInstancedMesh) {
      const instance = new THREE.Matrix4(), world = new THREE.Matrix4();
      const color = new THREE.Color();
      for (let i = 0; i < mesh.count; i++) {
        mesh.getMatrixAt(i, instance);
        world.multiplyMatrices(mesh.matrixWorld, instance);
        if (mesh.instanceColor) color.fromBufferAttribute(mesh.instanceColor, i); else color.set(0xffffff);
        add(mesh, world, color);
      }
    } else add(mesh, mesh.matrixWorld);
  }
  return result;
}

function propShader(material, key) {
  const clone = material.clone();
  clone.vertexColors = true;
  // Cloth is flattened at the exported canonical pose; its source atlas and material remain active.
  if (key === 'cloth') clone.onBeforeCompile = () => {};
  return buildSourceMaterialShader(clone, shaderOptions);
}

function meshPack(sections) {
  const chunks = [];
  const writeU16 = (v) => { const b = Buffer.allocUnsafe(2); b.writeUInt16LE(v); chunks.push(b); };
  const writeU32 = (v) => { const b = Buffer.allocUnsafe(4); b.writeUInt32LE(v); chunks.push(b); };
  writeU32(sections.length);
  for (const section of sections) {
    const name = Buffer.from(section.name, 'utf8');
    if (name.length > 65535) throw new Error('Stage mesh section name too long');
    writeU16(name.length); chunks.push(name);
    writeU32(section.vertices); writeU32(section.indices.length); writeU32(section.stride);
    chunks.push(Buffer.from(section.data.buffer, section.data.byteOffset, section.data.byteLength));
    chunks.push(Buffer.from(section.indices.buffer, section.indices.byteOffset, section.indices.byteLength));
  }
  const raw = Buffer.concat(chunks);
  const header = Buffer.alloc(12);
  header.write('TSSTG001', 0, 'ascii'); header.writeUInt32LE(raw.length, 8);
  return { file: Buffer.concat([header, deflateSync(raw, { level: 9 })]), rawBytes: raw.length };
}

async function artifact(relativePath, content) {
  const target = resolve(output, relativePath);
  if (check) {
    const existing = await readFile(target).catch(() => null);
    if (!existing || !existing.equals(Buffer.isBuffer(content) ? content : Buffer.from(content)))
      throw new Error(`Generated stage visual artifact differs: ${relative(output, target)}`);
  } else {
    await mkdir(dirname(target), { recursive: true });
    const bytes = Buffer.isBuffer(content) ? content : Buffer.from(content);
    const existing = await readFile(target).catch(() => null);
    if (!existing?.equals(bytes)) await writeFile(target, bytes);
  }
}

async function sourcePaintShaders() {
  const source = await readFile(resolve(root, 'splat-ref/src/world/paint.js'), 'utf8');
  const extract = (name) => {
    const match = new RegExp(`const ${name} = /\\* glsl \\*/\\x60([\\s\\S]*?)\\x60;`).exec(source);
    if (!match) throw new Error(`Cannot find source ${name}`);
    return match[1].replaceAll('${BAND_L}', '0.55').replaceAll('${BAND_W}', '0.62').replaceAll('${BAND_R}', '0.1');
  };
  return {
    vertex: '#version 450 core\n#define attribute in\n#define varying out\n' + extract('PAINT_VS'),
    fragment: '#version 450 core\n#define varying in\nlayout(location=0) out vec4 iwPaintColor;\n#define gl_FragColor iwPaintColor\n' + extract('PAINT_FS').replace('precision highp float;', ''),
  };
}

async function main() {
  if (!check) execFileSync(process.execPath, ['tools/export-reference.mjs', '--check'], { cwd: root, stdio: 'pipe' });
  await mkdir(resolve(output, 'shaders'), { recursive: true });
  const paint = await sourcePaintShaders();
  const textureRoot = resolve(root, 'assets/reference/stage-textures');
  const textureMeta = JSON.parse(await readFile(resolve(textureRoot, 'metadata.json'), 'utf8'));
  if (textureMeta.schema !== 1 || textureMeta.texlib?.size !== 512 || !textureMeta.texlib?.layers || !textureMeta.texlib?.meta)
    throw new Error('Source stage texture capture metadata is incomplete');
  // createLevelMaterial needs real source texture handles and the captured material table to emit USE_TEXLIB/TL_SLOTS.
  // Native rendering binds the captured GPU data; these one-pixel handles only stand in for Three.js objects while
  // the source material's actual onBeforeCompile hook is expanded below.
  const arrayHandle = () => new THREE.DataArrayTexture(new Uint8Array(4), 1, 1, 1);
  const sourceTexlib = {
    albedo: arrayHandle(), normal: arrayHandle(), orm: arrayHandle(),
    layers: textureMeta.texlib.layers, meta: textureMeta.texlib.meta, stats: { size: textureMeta.texlib.size },
  };
  const muralHandle = (id) => {
    const captured = textureMeta.murals?.[id];
    if (!captured?.file || !Array.isArray(captured.murals)) throw new Error(`Missing captured source murals for ${id}`);
    const texture = new THREE.Texture();
    texture.userData = { murals: captured.murals };
    return texture;
  };
  const levelMat = createLevelMaterial(new THREE.Texture(), atlasSize, muralHandle('tidewater'), { texlib: sourceTexlib });
  const levelShader = buildSourceMaterialShader(levelMat, shaderOptions);
  const shaders = new Map([['level', levelShader]]);
  await artifact('shaders/level.vert', levelShader.vertex);
  await artifact('shaders/level.frag', levelShader.fragment);
  await artifact('shaders/level.uniforms', serialUniforms(levelShader.uniforms));
  const tlImage = levelMat.userData.uniforms.uTLTex.value.image;
  const tlHeader = Buffer.alloc(16);
  tlHeader.write('TSTL0001', 0, 'ascii'); tlHeader.writeUInt32LE(tlImage.width, 8); tlHeader.writeUInt32LE(tlImage.height, 12);
  await artifact('shaders/level.tl', Buffer.concat([tlHeader, Buffer.from(tlImage.data.buffer, tlImage.data.byteOffset, tlImage.data.byteLength)]));
  const textureIndex = [];
  for (const key of ['albedo', 'normal', 'orm']) {
    const info = textureMeta.texlib.textures[key];
    textureIndex.push(`${key} ../stage-textures/${info.file} ${info.width} ${info.height} ${info.layers} ${Number(info.srgb)}`);
  }
  for (const key of ['atlas', 'chain', 'blob']) {
    const info = textureMeta.props[key];
    textureIndex.push(`props-${key} ../stage-textures/${info.file} ${info.width} ${info.height} ${info.layers} ${Number(info.srgb)}`);
  }
  for (const [id, entry] of Object.entries(textureMeta.murals).sort(([a], [b]) => a.localeCompare(b)))
    textureIndex.push(`mural-${id} ../stage-textures/${entry.file} ${entry.width} ${entry.height} ${entry.layers} ${Number(entry.srgb)}`);
  await artifact('textures.index', textureIndex.join('\n') + '\n');
  await artifact('shaders/paint.vert', paint.vertex);
  await artifact('shaders/paint.frag', paint.fragment);
  const entries = [];
  for (const map of MAPS) {
    const id = map.layout || map.id;
    const layout = MAP_LAYOUTS[id];
    if (!layout) throw new Error(`Missing source stage layout ${id}`);
    const stageMaterial = createLevelMaterial(new THREE.Texture(), atlasSize, muralHandle(id), { texlib: sourceTexlib });
    const stageShader = buildSourceMaterialShader(stageMaterial, shaderOptions);
    if (stageShader.digest !== levelShader.digest) throw new Error(`Source level shader changed between mural sets for ${id}`);
    await artifact(`shaders/level-${id}.uniforms`, serialUniforms(stageShader.uniforms));
    for (const mode of ['turf', 'zones', 'boss']) {
      if (mode === 'boss' && map.noBoss) continue;
      const kit = new PropKit(new THREE.Scene(), { quality: 'high' });
      const sourceDressing = dressingFor(id).filter((item) => inMode(item, mode));
      try {
        for (const item of sourceDressing) kit.add(item.type, item);
        kit.build();
        kit.update(0, 0);
        const stage = new Level(layoutFor(layout, mode));
        const paintLayout = Object.create(PaintSystem.prototype);
        paintLayout.level = stage; paintLayout.size = atlasSize; paintLayout.pad = 8;
        paintLayout._layout(30);
        const geometry = stage.buildGeometry(atlasSize);
        const stageSection = interleaveLevel(geometry);
        const sections = [{ name: 'stage', ...stageSection }];
        const propMeshes = propMaterialMeshes(kit);
        let propVertexCount = 0, propIndexCount = 0;
        for (const [key, mesh] of propMeshes) {
          if (!mesh.indices.length) continue;
          const data = new Float32Array(mesh.positions.length + mesh.normals.length + mesh.colors.length + mesh.uvs.length);
          const stride = 11, vertices = mesh.positions.length / 3;
          for (let i = 0; i < vertices; i++) {
            const o = i * stride;
            data.set(mesh.positions.slice(i * 3, i * 3 + 3), o);
            data.set(mesh.normals.slice(i * 3, i * 3 + 3), o + 3);
            data.set(mesh.colors.slice(i * 3, i * 3 + 3), o + 6);
            data.set(mesh.uvs.slice(i * 2, i * 2 + 2), o + 9);
          }
          const indices = Uint32Array.from(mesh.indices);
          sections.push({ name: `props.${key}`, vertices, indices, stride, data });
          propVertexCount += vertices; propIndexCount += indices.length;
          if (!shaders.has(`props.${key}`)) {
            const shader = propShader(kit.mat[key], key);
            shaders.set(`props.${key}`, shader);
            await artifact(`shaders/props-${key}.vert`, shader.vertex);
            await artifact(`shaders/props-${key}.frag`, shader.fragment);
            await artifact(`shaders/props-${key}.uniforms`, serialUniforms(shader.uniforms));
            const state = shader.renderState;
            await artifact(`shaders/props-${key}.state`, `${Number(state.transparent)} ${Number(state.depthWrite)} ${Number(state.depthTest)} ${state.side} ${state.blending} ${state.depthFunc} ${Number(kit.mat[key].alphaToCoverage)} ${Number(kit.mat[key].polygonOffset)} ${kit.mat[key].polygonOffsetFactor} ${kit.mat[key].polygonOffsetUnits}\n`);
          }
        }
        if (!propVertexCount) throw new Error(`Source PropKit emitted no render vertices for ${id}/${mode}`);
        const file = `${id}-${mode}.stg.z`;
        const packed = meshPack(sections);
        await artifact(file, packed.file);
        const edgeNormalCount = (() => {
          const d = stageSection.data, stride = stageSection.stride;
          let count = 0;
          for (let i = 0; i < stageSection.vertices; i++) {
            const o = i * stride + 3;
            if ([d[o], d[o + 1], d[o + 2]].filter((n) => Math.abs(n) > 0.08).length > 1) count++;
          }
          return count;
        })();
        entries.push({ id: map.id, mode, file, vertexCount: stageSection.vertices, indexCount: stageSection.indices.length,
          propVertexCount, propIndexCount, exposedBevelNormalCount: edgeNormalCount, sourceProps: sourceDressing.length,
          rawBytes: packed.rawBytes, compressedBytes: packed.file.length,
          stageShaderDigest: levelShader.digest, paintShaderDigest: createHash('sha256').update(paint.vertex).update(paint.fragment).digest('hex') });
        geometry.dispose();
      } finally { kit.dispose(); }
    }
  }
  const manifest = {
    version: 1, generator: 'tools/export-stage-visuals.mjs', source: 'splat-ref', atlasSize, maxDensity: 30, pad: 8,
    geometrySource: 'Level.buildGeometry with source exposed-edge quarter-round bevel strips and octant corners',
    materialSource: 'levelMaterial.js MeshPhysicalMaterial hooks expanded from bundled Three.js shader chunks with captured texlib, mural, light and prop texture packs',
    inkSource: 'paint.js PAINT_FS + levelMaterial.js + inkShading.js; continuous RGBA atlas rendered by the source PAINT shader and dried per frame',
    propSource: 'PropKit high-quality geometry builders, merged and instanced meshes baked from source placements, with captured source atlas, chain-link and blob maps',
    textures: { index: 'textures.index', sourceDirectory: '../stage-textures', capture: textureMeta.schema, texlibLayers: textureMeta.texlib.names.length },
    shaders: { levelVertex: 'shaders/level.vert', levelFragment: 'shaders/level.frag', paintVertex: 'shaders/paint.vert', paintFragment: 'shaders/paint.frag',
      stageDigest: levelShader.digest, paintDigest: createHash('sha256').update(paint.vertex).update(paint.fragment).digest('hex'),
      props: Object.fromEntries([...shaders].filter(([key]) => key.startsWith('props.')).map(([key, value]) => [key, { vertex: `shaders/props-${key.slice(6)}.vert`, fragment: `shaders/props-${key.slice(6)}.frag`, uniforms: `shaders/props-${key.slice(6)}.uniforms`, digest: value.digest }])) },
    stages: entries,
  };
  await artifact('manifest.json', JSON.stringify(manifest, null, 2) + '\n');
  const license = (await readFile(resolve(root, 'LICENSE'), 'utf8')).replace(/\r\n?/g, '\n').replace('Copyright (c) 2025 SoWiEee', 'Copyright (c) 2010-2026 Three.js Authors');
  await artifact('LICENSE.three.txt', license);
  console.log(`${check ? 'Verified' : 'Exported'} ${entries.length} source stage render assets`);
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) await main();
