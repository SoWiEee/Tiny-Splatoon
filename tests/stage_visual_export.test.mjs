import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile, stat } from 'node:fs/promises';
import { inflateSync } from 'node:zlib';

const root = new URL('../assets/world/stages/', import.meta.url);

function parseMeshPack(buffer) {
  assert.equal(buffer.subarray(0, 8).toString('ascii'), 'TSSTG001');
  const expectedBytes = buffer.readUInt32LE(8);
  const raw = inflateSync(buffer.subarray(12));
  assert.equal(raw.length, expectedBytes, 'compressed stage payload length');
  let cursor = 0;
  const readU32 = () => { const value = raw.readUInt32LE(cursor); cursor += 4; return value; };
  const sectionCount = readU32();
  const sections = new Map();
  for (let i = 0; i < sectionCount; i++) {
    const nameLength = raw.readUInt16LE(cursor); cursor += 2;
    const name = raw.subarray(cursor, cursor + nameLength).toString('utf8'); cursor += nameLength;
    const vertices = readU32(), indices = readU32(), stride = readU32();
    const floats = new Float32Array(vertices * stride);
    for (let j = 0; j < floats.length; j++, cursor += 4) floats[j] = raw.readFloatLE(cursor);
    const elements = new Uint32Array(indices);
    for (let j = 0; j < elements.length; j++) elements[j] = readU32();
    sections.set(name, { vertices, indices, stride, floats, elements });
  }
  assert.equal(cursor, raw.length, 'stage payload has no unparsed bytes');
  return sections;
}

test('source-derived stage geometry and render material assets are present for every legal stage mode', async () => {
  const manifest = JSON.parse(await readFile(new URL('manifest.json', root), 'utf8'));
  assert.match(manifest.geometrySource, /Level\.buildGeometry/);
  assert.match(manifest.inkSource, /paint\.js.*inkShading\.js/);
  assert.ok(manifest.stages.length >= 20, 'all source stage/mode combinations are exported');
  assert.ok(manifest.shaders.levelVertex && manifest.shaders.levelFragment);
  assert.equal(manifest.textures?.texlibLayers, 46, 'source texlib layers are captured from the reference GPU');
  const index = await readFile(new URL('textures.index', root), 'utf8');
  for (const key of ['albedo', 'normal', 'orm', 'props-atlas', 'props-chain', 'props-blob', 'mural-tidewater'])
    assert.match(index, new RegExp(`^${key} \.\./stage-textures/[^\\n]+$`, 'm'));
  const tl = await readFile(new URL('shaders/level.tl', root));
  assert.equal(tl.subarray(0, 8).toString('ascii'), 'TSTL0001');
  assert.equal(tl.readUInt32LE(12), 3, 'source texlib table includes the three original rows');
  const levelShader = await readFile(new URL('shaders/level.frag', root), 'utf8');
  assert.match(levelShader, /#define USE_TEXLIB/);
  assert.match(levelShader, /uniform sampler2DArray tAlbedo/);
  assert.match(await readFile(new URL('shaders/level-tidewater.uniforms', root), 'utf8'), /^uMurA 48 /m,
    'per-stage source mural placement data is preserved');
  const paintShader = await readFile(new URL('shaders/props-paint.frag', root), 'utf8');
  const fenceShader = await readFile(new URL('shaders/props-fence.frag', root), 'utf8');
  const blobShader = await readFile(new URL('shaders/props-blob.frag', root), 'utf8');
  assert.match(paintShader, /#define USE_MAP/);
  assert.match(fenceShader, /#define USE_ALPHATEST/);
  assert.match(blobShader, /#define USE_MAP/);
  assert.match(await readFile(new URL('shaders/props-blob.state', root), 'utf8'), /^1 0 1 /,
    'source blob draw retains transparency and disabled depth writes');

  const ids = new Set();
  let checkedGeometry = false;
  for (const item of manifest.stages) {
    ids.add(`${item.id}-${item.mode}`);
    assert.ok(item.vertexCount > 0 && item.indexCount > 0, `${item.id}/${item.mode} has source level geometry`);
    assert.ok(item.propVertexCount > 0, `${item.id}/${item.mode} has PropKit render geometry`);
    const info = await stat(new URL(item.file, root));
    assert.equal(info.size, item.compressedBytes, `${item.file} size matches manifest`);
    if (checkedGeometry || item.id !== 'tidewater' || item.mode !== 'turf') continue;
    const meshes = parseMeshPack(await readFile(new URL(item.file, root)));
    const stage = meshes.get('stage');
    assert.ok(stage && stage.stride === 25 && stage.vertices === item.vertexCount);
    assert.ok([...meshes.keys()].some((name) => name.startsWith('props.')));
    assert.ok([...meshes.keys()].some((name) => name.endsWith('~ns')), 'source non-shadow-casting props stay separate');
    const animations=(await readFile(new URL(`${item.id}-${item.mode}.anim`,root),'utf8')).trim().split('\n');
    assert.ok(animations.length>20,'live prop motions must remain in the source export');
    for(const line of animations){
      const [name,kind,...numbers]=line.split(' ');
      assert.ok(meshes.has(name),`animation references missing mesh ${name}`);
      assert.ok(['y','z','blink','cloth'].includes(kind));
      assert.equal(numbers.length,21);
      assert.ok(numbers.every(n=>Number.isFinite(Number(n))));
    }
    let roundedEdgeNormals = 0;
    for (let i = 0; i < stage.vertices; i++) {
      const n = [stage.floats[i * stage.stride + 3], stage.floats[i * stage.stride + 4], stage.floats[i * stage.stride + 5]];
      if (n.filter((component) => Math.abs(component) > 0.08).length > 1) roundedEdgeNormals++;
    }
    assert.ok(roundedEdgeNormals > 0, `${item.id}/${item.mode} retains rounded exposed-edge normals`);
    checkedGeometry = true;
  }
  assert.ok(ids.has('tidewater-turf'));
  assert.ok(checkedGeometry, 'representative mesh pack was decoded');
});
