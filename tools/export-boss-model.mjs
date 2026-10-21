import { createHash } from 'node:crypto';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { execFileSync } from 'node:child_process';
import { register } from 'node:module';
import { resolve } from 'node:path';

const root = resolve(import.meta.dirname, '..');
const output = resolve(root, 'assets/reference/boss');
const check = process.argv.includes('--check');
if (process.argv.slice(2).some((arg) => arg !== '--check')) throw new Error('Usage: node tools/export-boss-model.mjs [--check]');
execFileSync(process.execPath, ['tools/export-reference.mjs', '--check'], { cwd: root, stdio: 'pipe' });
register(new URL('./reference-loader.mjs', import.meta.url));
const THREE = await import('three');
const { buildRig, buildParts, buildCrablet, CONT, MC, LEGS, ARMS, BODY, HEAD } = await import('../splat-ref/src/boss/bossModelGeo.js');

const hash = (data) => createHash('sha256').update(data).digest('hex');
const text = (value) => Buffer.from(value, 'ascii');
const string = (value) => Buffer.concat([Buffer.from([text(value).length]), text(value)]);
const number = (value) => { if (!Number.isFinite(value)) throw new Error('Non-finite boss model value'); return Math.fround(value); };

function packMesh(name, positions, normals, uv, joints, weights, material, indices) {
  const count = positions.length / 3;
  if (normals.length !== count * 3 || uv.length !== count * 2 || joints.length !== count * 4 || weights.length !== count * 4 || material.length !== count * 4)
    throw new Error(`Incomplete source attributes in ${name}`);
  const vertices = Buffer.alloc(count * 68);
  let offset = 0;
  for (let i = 0; i < count; i++) {
    for (let j = 0; j < 3; j++) { vertices.writeFloatLE(number(positions[i * 3 + j]), offset); offset += 4; }
    for (let j = 0; j < 3; j++) { vertices.writeFloatLE(number(normals[i * 3 + j]), offset); offset += 4; }
    for (let j = 0; j < 2; j++) { vertices.writeFloatLE(number(uv[i * 2 + j]), offset); offset += 4; }
    for (let j = 0; j < 4; j++) { const bone = joints[i * 4 + j]; if (bone > 255) throw new Error(`Boss rig exceeds byte index: ${bone}`); vertices.writeUInt8(bone, offset++); }
    for (let j = 0; j < 4; j++) { vertices.writeFloatLE(number(weights[i * 4 + j]), offset); offset += 4; }
    for (let j = 0; j < 4; j++) { vertices.writeFloatLE(number(material[i * 4 + j]), offset); offset += 4; }
  }
  const element = Buffer.alloc(indices.length * 4);
  for (let i = 0; i < indices.length; i++) element.writeUInt32LE(indices[i], i * 4);
  return { name, count, triangles: indices.length / 3, data: Buffer.concat([string(name), u32(count), u32(indices.length), vertices, element]) };
}
function u32(value) { const out = Buffer.alloc(4); out.writeUInt32LE(value); return out; }
function rigData(name, rig, meshes, sockets = [], poseSockets = []) {
  const bones = rig.bones.map((bone) => ({
    name: bone.name, parent: bone.parent ? bone.parent.userData.i : -1,
    position: [bone.position.x, bone.position.y, bone.position.z],
  }));
  const boneBytes = bones.map((bone) => Buffer.concat([string(bone.name), i16(bone.parent), ...bone.position.map(f32)]));
  return {
    name, bones, meshes, sockets, poseSockets,
    data: Buffer.concat([string(name), u32(bones.length), ...boneBytes, u32(meshes.length), ...meshes.map((mesh) => mesh.data),
      u32(sockets.length), ...sockets.flatMap((socket) => [string(socket.name), u16(socket.bone), ...socket.offset.map(f32), f32(socket.radius), Buffer.from([socket.weak ? 1 : 0, socket.active ? 1 : 0])]),
      u32(poseSockets.length), ...poseSockets.flatMap((socket) => [string(socket.name), u16(socket.bone), ...socket.offset.map(f32)])]),
  };
}
function i16(value) { const out = Buffer.alloc(2); out.writeInt16LE(value); return out; }
function u16(value) { const out = Buffer.alloc(2); out.writeUInt16LE(value); return out; }
function u8(value) { return Buffer.from([value ? 1 : 0]); }
function f32(value) { const out = Buffer.alloc(4); out.writeFloatLE(number(value)); return out; }
function v3(value) { return value.toArray().map(f32); }
function f64(value) { if (!Number.isFinite(value)) throw new Error('Non-finite boss pose-rig value'); const out = Buffer.alloc(8); out.writeDoubleLE(value); return out; }
function v3d(value) { return value.toArray().map(f64); }

const rig = buildRig();
const parts = buildParts(rig, 'high');
const hullMeshes = Object.entries(parts.buckets).map(([name, bucket]) => packMesh(name,
  bucket.P, bucket.N, bucket.UV, bucket.SI, bucket.SW, bucket.M, bucket.I));
const sourceModel = await readFile(resolve(root, 'splat-ref/src/boss/bossModel.js'), 'utf8');
const hitSource = sourceModel.match(/const HIT = \[([\s\S]*?)\];/);
if (!hitSource) throw new Error('Source BossModel hit shapes not found');
const hitSpecs = [...hitSource[1].matchAll(/\['(\w+)',\s*([\d.]+),\s*(true|false)\]/g)].map((row) => ({ name: row[1], radius: +row[2], weak: row[3] === 'true' }));
if (hitSpecs.length !== 10) throw new Error(`Expected 10 source boss hit shapes, found ${hitSpecs.length}`);
const hitSockets = hitSpecs.map((spec) => {
  let socket, boneName;
  if (spec.name === 'shellF' || spec.name === 'shellR') {
    boneName = 'shell';
    const offset = new THREE.Vector3(0, spec.name === 'shellF' ? .05 : .1, spec.name === 'shellF' ? 1.5 : -1.55)
      .applyMatrix4(new THREE.Matrix4().makeRotationX(CONT.pitch)).add(CONT.pos).sub(rig.rest.shell);
    socket = { offset };
  } else if (spec.name === 'body') {
    boneName = 'head'; socket = { offset: new THREE.Vector3(0, 2.2, 2.3).sub(rig.rest.head) };
  } else {
    [boneName, socket] = parts.sockets[spec.name];
  }
  const offset = socket.offset ?? socket.clone().sub(rig.rest[boneName]);
  const bone = rig.by[boneName].userData.i;
  return { name: spec.name, bone, offset: [offset.x, offset.y, offset.z], radius: spec.radius, weak: spec.weak, active: !spec.weak || spec.name.startsWith('eye') };
});
const poseSockets = Object.entries(parts.sockets).map(([name, [boneName, socket]]) => {
  // BossModel overrides this entry in its constructor; mirror its public getSocket semantics.
  const sourceBone = name === 'body' ? 'head' : boneName;
  const sourcePoint = name === 'body' ? new THREE.Vector3(0, 2.2, 2.3) : socket;
  const offset = sourcePoint.offset ?? sourcePoint.clone().sub(rig.rest[sourceBone]);
  return { name, bone: rig.by[sourceBone].userData.i, offset: [offset.x, offset.y, offset.z] };
});
if (poseSockets.length !== 20) throw new Error(`Expected 20 source boss pose sockets, found ${poseSockets.length}`);
const hull = rigData('hullbreaker', rig, hullMeshes, hitSockets, poseSockets);

const crab = buildCrablet('high');
const geo = crab.geo;
const attr = (name) => [...geo.attributes[name].array];
const crabMesh = packMesh('crablet', attr('position'), attr('normal'), attr('uv'), attr('skinIndex'), attr('skinWeight'), attr('aM'), [...geo.index.array]);
const crablet = rigData('crablet', crab, [crabMesh]);

const models = [hull, crablet];
const binary = Buffer.concat([text('IBOS'), u32(3), u32(models.length), ...models.map((model) => model.data)]);
const poseRig = Buffer.concat([
  text('IBPR'), u32(2), u32(rig.bones.length), ...rig.bones.flatMap((bone) => [string(bone.name), ...v3d(bone.position)]),
  ...v3d(BODY.pivot), ...v3d(CONT.pos), f64(CONT.pitch), f64(CONT.hl), f64(CONT.hw), f64(CONT.hh),
  ...MC.toArray().map(f64), ...v3d(HEAD.nozzle), ...v3d(HEAD.nozzleTip), u32(LEGS.length),
  ...LEGS.flatMap((leg) => [...v3d(leg.hip), ...v3d(leg.cox), ...v3d(leg.knee), ...v3d(leg.ankle), ...v3d(leg.foot), ...v3d(leg.out), f64(leg.ld), f64(leg.l1), f64(leg.l2), f64(leg.side)]),
  u32(ARMS.length), ...ARMS.flatMap((arm) => [...v3d(arm.S), ...v3d(arm.E), ...v3d(arm.W), ...v3d(arm.D), ...v3d(arm.U), ...v3d(arm.pole), ...v3d(arm.hinge), f64(arm.lu), f64(arm.lf), u8(arm.crusher)]),
  u32(rig.chains.length), ...rig.chains.flatMap((chain) => [
    string(chain.name), string(chain.parent), ...v3d(rig.rest[chain.parent]), u16(chain.pts.length), ...chain.pts.flatMap(v3d), u16(chain.bones.length), ...chain.bones.map(string),
    u8(!!chain.plane), ...(chain.plane || [0, 0, 0, 0]).map(f64), f64(chain.damp ?? 0.98), f64(chain.stiff || 0), f64(chain.grav ?? 1), u8(!!chain.flag), u8(!!chain.kelp),
  ]),
]);
const manifest = Buffer.from(`${JSON.stringify({
  version: 4,
  source: Object.fromEntries(await Promise.all(['bossModelGeo.js', 'bossModel.js'].map(async (file) => [
    file, hash(await readFile(resolve(root, 'splat-ref/src/boss', file))),
  ]))),
  binary: hash(binary),
  poseRig: hash(poseRig),
  models: models.map(({ name, bones, meshes, sockets, poseSockets }) => ({
    name, bones: bones.map((bone) => bone.name),
    meshes: meshes.map(({ name, count, triangles }) => ({ name, vertices: count, triangles })),
    sockets: sockets.map(({ name, bone, offset, radius, weak, active }) => ({ name, bone, offset, radius, weak, active })),
    poseSockets: poseSockets.map(({ name, bone, offset }) => ({ name, bone, offset })),
  })),
}, null, 2)}\n`);

await mkdir(output, { recursive: true });
const binaryPath = resolve(output, 'model.bin'), posePath = resolve(output, 'pose-rig.bin'), manifestPath = resolve(output, 'manifest.json');
if (check) {
  const [currentBinary, currentPose, currentManifest] = await Promise.all([readFile(binaryPath), readFile(posePath), readFile(manifestPath)]);
  if (!currentBinary.equals(binary) || !currentPose.equals(poseRig) || !currentManifest.equals(manifest)) throw new Error('Boss model assets differ from locked source; run tools/export-boss-model.mjs');
} else {
  await Promise.all([writeFile(binaryPath, binary), writeFile(posePath, poseRig), writeFile(manifestPath, manifest)]);
}
console.log(`Boss model ${models.map((model) => `${model.name}:${model.meshes.reduce((sum, mesh) => sum + mesh.triangles, 0)}t`).join(' ')} ${hash(binary)}`);
