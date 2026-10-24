import { register } from 'node:module';
import { createHash } from 'node:crypto';
import { deflateSync } from 'node:zlib';
import { mkdir, readFile, readdir, rm, writeFile } from 'node:fs/promises';
import { resolve } from 'node:path';
import { referenceBrowser } from './reference-browser-session.mjs';

const root = resolve(import.meta.dirname, '..');
const output = resolve(root, 'assets/reference/lobby');
const check = process.argv.includes('--check');
if (process.argv.slice(2).some(value => value !== '--check')) throw new Error('Usage: node tools/export-lobby-assets.mjs [--check]');
register(new URL('./reference-loader.mjs', import.meta.url));
const THREE = await import('three');
const { buildSourceMaterialShader } = await import('./export-character-shaders.mjs');

// The source builder only needs canvas APIs to construct its atlases. A real Edge canvas is used below to export them.
function gradient() { return { addColorStop() {} }; }
const context = new Proxy({ measureText: text => ({ width: String(text).length * 12 }), createLinearGradient: gradient, createRadialGradient: gradient }, {
  get(target, key) { return key in target ? target[key] : () => {}; },
  set(target, key, value) { target[key] = value; return true; },
});
globalThis.document = { createElement: () => ({ width: 0, height: 0, getContext: () => context }) };

const textureMeta = JSON.parse(await readFile(resolve(root, 'assets/reference/stage-textures/metadata.json'), 'utf8'));
if (textureMeta.schema !== 1 || textureMeta.texlib?.size !== 512 || !textureMeta.texlib?.layers || !textureMeta.texlib?.meta)
  throw new Error('Source lobby requires the captured source texture library');
const dummyArray = () => new THREE.DataArrayTexture(new Uint8Array(4), 1, 1, 1);
const dummyTexlib = {
  size: textureMeta.texlib.size, layers: textureMeta.texlib.layers, meta: textureMeta.texlib.meta,
  albedo: dummyArray(), normal: dummyArray(), orm: dummyArray(), stats: { size: textureMeta.texlib.size },
};
const { LobbySet, ALLEY } = await import('../splat-ref/src/game/lobbySet.js');
const source = new LobbySet(null, { quality: 'high', texlib: dummyTexlib });
await source.ready;
source.root.updateMatrixWorld(true);

class Writer {
  chunks = [];
  raw(value) { this.chunks.push(Buffer.from(value)); }
  u32(value) { if (!Number.isInteger(value) || value < 0 || value > 0xffffffff) throw new Error(`Invalid lobby asset integer ${value}`); const b = Buffer.alloc(4); b.writeUInt32LE(value); this.raw(b); }
  string(value) { const b = Buffer.from(value, 'utf8'); this.u32(b.length); this.raw(b); }
  floats(values) { const b = Buffer.alloc(values.length * 4); values.forEach((value, i) => { if (!Number.isFinite(value)) throw new Error(`Non-finite source lobby asset value ${value} at ${i} in [${values}]`); b.writeFloatLE(value, i * 4); }); this.raw(b); }
  done() { return Buffer.concat(this.chunks); }
}

const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const flatten = value => {
  if (typeof value === 'number' || typeof value === 'boolean') return [Number(value)];
  if (value?.toArray) return value.toArray().flatMap(flatten);
  if (Array.isArray(value) || ArrayBuffer.isView(value)) return Array.from(value).flatMap(flatten);
  return [];
};
function materialUniforms(material) {
  if (material.isShaderMaterial) return material.uniforms;
  const lib = material.isMeshPhysicalMaterial ? THREE.ShaderLib.physical
    : material.isMeshStandardMaterial ? THREE.ShaderLib.standard
      : material.isMeshBasicMaterial ? THREE.ShaderLib.basic : null;
  if (!lib || typeof material.onBeforeCompile !== 'function') throw new Error(`Unsupported source lobby material ${material.type}/${material.name}`);
  const shader = { vertexShader: lib.vertexShader, fragmentShader: lib.fragmentShader, uniforms: THREE.UniformsUtils.clone(lib.uniforms) };
  material.onBeforeCompile(shader, null);
  return shader.uniforms;
}
function textureRows(material, meshIndex) {
  const result = [];
  for (const [name, item] of Object.entries(materialUniforms(material)).sort(([a], [b]) => a.localeCompare(b))) {
    const texture = item?.value;
    if (texture?.isTexture) result.push({ uniform: name, key: `mesh-${meshIndex}-${name}`, dynamic: !!texture.isRenderTargetTexture,
      sampler: { wrapS: texture.wrapS, wrapT: texture.wrapT, minFilter: texture.minFilter, magFilter: texture.magFilter, generateMipmaps: !!texture.generateMipmaps } });
  }
  return result;
}
function geometryData(geometry) {
  const position = geometry?.attributes?.position;
  if (!geometry?.isBufferGeometry || !position) throw new Error('Source lobby mesh has no positions');
  const attributes = Object.entries(geometry.attributes).filter(([, attribute]) => !attribute.isInstancedBufferAttribute).sort(([a], [b]) => a.localeCompare(b));
  const rows = [];
  for (const [name, attribute] of attributes) {
    if (attribute.count !== position.count || attribute.itemSize < 1 || attribute.itemSize > 4) throw new Error(`Unsupported source lobby attribute ${name}`);
    const values = Array.from({ length: attribute.count * attribute.itemSize }, (_, i) => attribute.getComponent(Math.floor(i / attribute.itemSize), i % attribute.itemSize));
    rows.push({ name, components: attribute.itemSize, count: attribute.count, values });
  }
  const indices = Array.from(geometry.index?.array ?? Array.from({ length: position.count }, (_, i) => i));
  if (indices.length % 3 || indices.some(index => !Number.isInteger(index) || index < 0 || index >= position.count)) throw new Error('Invalid source lobby triangle indices');
  return { attributes: rows, vertices: position.count, indices };
}
function renderState(material, casts) {
  const state = buildSourceMaterialShader(material, { toneMapping: false, linearOutput: true, materialMaps: true }).renderState;
  return [Number(state.transparent), Number(state.depthWrite), Number(state.depthTest), state.side, state.blending, Number(casts),
    state.depthFunc, material.blendSrc, material.blendDst, material.blendSrcAlpha ?? material.blendSrc, material.blendDstAlpha ?? material.blendDst,
    material.blendEquation, material.blendEquationAlpha ?? material.blendEquation, Number(!!material.premultipliedAlpha)];
}
const meshes = [];
const omitted = [];
const localSpots = [
  { prefix: 'uLobbyKey', field: 'keySpot', shadow: true },
  { prefix: 'uSpill', field: 'spillSpot', cookie: true },
  { prefix: 'uRim', field: 'rimSpot' },
  { prefix: 'uSweep', field: 'sweep' },
];
const localPoints = [
  { prefix: 'uNeonA', field: 'neonA', dynamic: 'teamA' },
  { prefix: 'uNeonB', field: 'neonB', dynamic: 'teamB' },
  { prefix: 'uBulbs', field: 'bulbs' },
  { prefix: 'uVend', field: 'vend' },
  { prefix: 'uStreet', field: 'street' },
];
const lightUniforms = [
  ...localSpots.flatMap(({ prefix }) => [
    `uniform vec3 ${prefix}Position; uniform vec3 ${prefix}Target; uniform vec3 ${prefix}Color;`,
    `uniform float ${prefix}Intensity; uniform float ${prefix}Distance; uniform float ${prefix}Decay; uniform float ${prefix}ConeCos; uniform float ${prefix}PenumbraCos;`,
  ]),
  ...localPoints.map(({ prefix }) => `uniform vec3 ${prefix}Position; uniform vec3 ${prefix}Color; uniform float ${prefix}Intensity; uniform float ${prefix}Distance; uniform float ${prefix}Decay;`),
  'uniform sampler2D uSpillCookie; uniform mat4 uSpillMatrix;',
  'uniform sampler2DShadow uLobbyKeyShadow; uniform mat4 uLobbyKeyShadowMatrix; uniform float uLobbyKeyShadowIntensity; uniform float uLobbyKeyShadowBias; uniform float uLobbyKeyShadowNormalBias; uniform float uLobbyKeyShadowRadius; uniform vec2 uLobbyKeyShadowMapSize;',
].join('\n');
const lightTerms = [
  ...localSpots.map(({ prefix, cookie, shadow }) => `{
vec3 lsVector = ${prefix}Position - vLsW;
float lsDistance = length(lsVector);
float lsCone = getSpotAttenuation(${prefix}ConeCos, ${prefix}PenumbraCos, dot(normalize(lsVector), normalize(${prefix}Position - ${prefix}Target)));
${cookie ? `vec4 lsCookiePosition = uSpillMatrix * vec4(vLsW, 1.0);
vec3 lsCookieCoord = lsCookiePosition.xyz / lsCookiePosition.w;
float lsCookieInside = all(lessThan(abs(lsCookieCoord * 2.0 - 1.0), vec3(1.0))) ? 1.0 : 0.0;
float lsCookie = mix(1.0, texture(uSpillCookie, lsCookieCoord.xy).r, lsCookieInside);` : 'float lsCookie = 1.0;'}
${shadow ? `float lsShadow = 1.0;
if (uLobbyKeyShadowIntensity > 0.0) {
vec3 lsShadowNormal = normalize(transpose(mat3(viewMatrix)) * geometryNormal);
vec4 lsShadowPosition4 = uLobbyKeyShadowMatrix * vec4(vLsW + lsShadowNormal * uLobbyKeyShadowNormalBias, 1.0);
vec3 lsShadowPosition = lsShadowPosition4.xyz / lsShadowPosition4.w;
lsShadowPosition.z += uLobbyKeyShadowBias;
bool lsShadowInside = lsShadowPosition.x >= 0.0 && lsShadowPosition.x <= 1.0 && lsShadowPosition.y >= 0.0 && lsShadowPosition.y <= 1.0 && lsShadowPosition.z <= 1.0;
if (lsShadowInside) {
vec2 lsShadowStep = (1.0 / uLobbyKeyShadowMapSize) * max(uLobbyKeyShadowRadius * 0.55, 0.6);
float lsShadowSamples = 0.0;
for (int lsX = -1; lsX <= 1; ++lsX) for (int lsY = -1; lsY <= 1; ++lsY) lsShadowSamples += texture(uLobbyKeyShadow, vec3(lsShadowPosition.xy + vec2(float(lsX), float(lsY)) * lsShadowStep, lsShadowPosition.z));
 lsShadow = mix(1.0, lsShadowSamples * (1.0 / 9.0), uLobbyKeyShadowIntensity);
 }
}` : 'float lsShadow = 1.0;'}
IncidentLight lsLight;
lsLight.direction = normalize((viewMatrix * vec4(${prefix}Position, 1.0)).xyz - geometryPosition);
lsLight.color = ${prefix}Color * ${prefix}Intensity * lsCone * lsCookie * lsShadow * getDistanceAttenuation(lsDistance, ${prefix}Distance, ${prefix}Decay);
lsLight.visible = lsCone > 0.0 && ${prefix}Intensity > 0.0;
if (lsLight.visible) RE_Direct(lsLight, geometryPosition, geometryNormal, geometryViewDir, geometryClearcoatNormal, material, reflectedLight);
}`),
  ...localPoints.map(({ prefix }) => `{
vec3 lsVector = ${prefix}Position - vLsW;
float lsDistance = length(lsVector);
IncidentLight lsLight;
lsLight.direction = normalize((viewMatrix * vec4(${prefix}Position, 1.0)).xyz - geometryPosition);
lsLight.color = ${prefix}Color * ${prefix}Intensity * getDistanceAttenuation(lsDistance, ${prefix}Distance, ${prefix}Decay);
lsLight.visible = ${prefix}Intensity > 0.0;
if (lsLight.visible) RE_Direct(lsLight, geometryPosition, geometryNormal, geometryViewDir, geometryClearcoatNormal, material, reflectedLight);
}`),
].join('\n');
if (lightTerms.split('{').length !== lightTerms.split('}').length) throw new Error('Unbalanced source lobby local-light shader terms');
source.root.traverse(object => {
  if (!object.geometry?.attributes?.position) return;
  if (object.isInstancedMesh && object.count === 0 && object.name !== 'lobbySet:drips') throw new Error(`Unsupported empty source lobby instance pool: ${object.name}`);
  if (Array.isArray(object.material)) throw new Error(`Unsupported source lobby material array: ${object.name}`);
  if (!object.visible) { omitted.push({ name: object.name, reason: 'source object is hidden at high quality' }); return; }
  const material = object.material;
  const shader = buildSourceMaterialShader(material, { toneMapping: false, linearOutput: true, materialMaps: true });
  const textureBindings = textureRows(material, meshes.length);
  const uniformText = Object.entries(shader.uniforms).map(([name, value]) => [name, flatten(value)])
    .filter(([, values]) => values.length && values.every(Number.isFinite))
    .sort(([a], [b]) => a.localeCompare(b)).map(([name, values]) => `${name} ${values.length} ${values.join(' ')}`).join('\n') + '\n';
  const sourceTextures = textureBindings.map(binding => `${binding.uniform}:${binding.key}`).join('\n');
  let vertex = shader.vertex, fragment = shader.fragment;
  const directLightingOutput = 'vec3 totalDiffuse = reflectedLight.directDiffuse + reflectedLight.indirectDiffuse;';
  if (material.isMeshStandardMaterial || material.isMeshPhysicalMaterial) {
    if (!fragment.includes(directLightingOutput) || !fragment.includes('varying vec3 vLsW;')) throw new Error(`Unsupported source lobby PBR lighting insertion point: ${object.name}`);
    fragment = fragment.replace('#version 450 core\n', `#version 450 core
${lightUniforms}
`);
    fragment = fragment.replace(directLightingOutput, `${lightTerms}\n${directLightingOutput}`);
    if (!fragment.includes(lightTerms)) throw new Error(`Failed to inject source lobby local lights into ${object.name}`);
  }
  if (object.isInstancedMesh) {
    const defines = `#define USE_INSTANCING\n${object.instanceColor ? '#define USE_INSTANCING_COLOR\n' : ''}`;
    vertex = vertex.replace('#version 450 core\n', `#version 450 core\n${defines}in mat4 instanceMatrix;\n${object.instanceColor ? 'in vec3 instanceColor;\n' : ''}`);
    fragment = fragment.replace('#version 450 core\n', `#version 450 core\n${defines}`);
  }
  const key = hash(Buffer.from(`${vertex}\0${fragment}\0${uniformText}\0${sourceTextures}`)).slice(0, 20);
  const data = geometryData(object.geometry);
  const instances = [];
  const count = object.isInstancedMesh ? object.count : 1;
  const instanceCapacity = object.isInstancedMesh ? object.instanceMatrix.count : 1;
  const dynamicInstances = !!object.isInstancedMesh && count === 0;
  if (count > instanceCapacity) throw new Error(`Source lobby instance count exceeds capacity: ${object.name}`);
  if (dynamicInstances && object.name !== 'lobbySet:drips') throw new Error(`Unsupported dynamic source lobby instance pool: ${object.name}`);
  for (let i = 0; i < count; i++) {
    const matrix = object.matrixWorld.clone();
    const color = new THREE.Color(1, 1, 1);
    if (object.isInstancedMesh) {
      const instance = new THREE.Matrix4(); object.getMatrixAt(i, instance); matrix.multiply(instance);
      if (object.instanceColor) color.fromBufferAttribute(object.instanceColor, i);
    }
    const phaseAttribute = object.geometry.attributes.aPhase;
    const phase = phaseAttribute ? phaseAttribute.getX(Math.min(i, phaseAttribute.count - 1)) : 0;
    instances.push({ matrix: matrix.toArray(), color: color.toArray(), phase });
  }
  const entry = {
    index: meshes.length, name: object.name || `mesh-${meshes.length}`, shader: key,
    attributes: data.attributes.map(({ name, components, count: n }) => ({ name, components, count: n })),
    indices: data.indices.length, instances: instances.length, instanceCapacity, dynamicInstances, instanced: !!object.isInstancedMesh, textures: textureBindings,
    state: renderState(material, object.castShadow),
  };
  meshes.push({ entry, shader: { key, vertex, fragment, uniforms: uniformText, renderState: shader.renderState }, data, instances });
});
if (!meshes.length) throw new Error('Source lobby scene has no static meshes');
// Source-authored layout/choreography is serialized beside the render geometry.
const vector = value => value.toArray();
const camera = value => ({ position: vector(value.pos), target: vector(value.target), fov: value.fov, near: value.near, far: value.far });
const spot = value => ({ position: vector(value.pos), yaw: value.yaw });
let bulbOffset = 0;
const bulbStrings = source._strings.map(string => {
  const result = { a: vector(string.a), b: vector(string.b), sag: string.sag, count: string.n, first: bulbOffset };
  bulbOffset += string.n;
  return result;
});
const layout = {
  schema: 1, source: 'splat-ref/src/game/lobbySet.js', units: 'metres, y-up',
  alley: ALLEY,
  lobbyCamera: camera(source.camera), hubCamera: camera(source.hubCamera), hubSpot: spot(source.hubSpot),
  lineupSpots: source.spots.map(spot), arrivalPaths: source.spots.map((_, i) => source.lanes(i).map(vector)),
  exitPaths: source.spots.map((_, i) => source.exitPath(i).map(vector)),
  // These values mirror the immutable LobbySet.update drip emitter.
  dripEmitter: { capacity: source._drips.length, sources: source._dripSrc.map(vector), initialDelay: source._nextDrip, gravity: 9.8, intervalMin: 1.4, intervalJitter: 1.8 },
  // These values mirror LobbySet.update's captured-object motion. The spot sweep's native lighting pass is not ported.
  motions: {
    bulbSway: {
      strings: bulbStrings, count: bulbOffset, glowCount: source._glowList.filter(glow => glow.tag).length,
      headlightGlows: [source._headlightGlow, source._headlightGlow + 1], headlightColor: new THREE.Color('#fff1d8').toArray(), streetZ: ALLEY.street.z0,
      initialDelay: 3, duration: 3.2, intervalMin: 7, intervalJitter: 7, halfTravel: 26, separation: 1.5,
    },
    moth: { center: vector(source._mothC) },
  },
};
if (layout.lineupSpots.length !== 8 || layout.arrivalPaths.length !== 8 || layout.exitPaths.length !== 8) throw new Error('Source lobby layout must expose eight lineup routes');
if (layout.dripEmitter.capacity !== 6 || layout.dripEmitter.sources.length !== 3 || meshes.filter(mesh => mesh.entry.name === 'lobbySet:drips').length !== 1 || meshes.find(mesh => mesh.entry.name === 'lobbySet:drips').entry.instanceCapacity !== layout.dripEmitter.capacity)
  throw new Error('Source lobby drip emitter geometry/layout mismatch');
if (layout.motions.bulbSway.count !== meshes.find(mesh => mesh.entry.name === 'lobbySet:bulbs')?.entry.instances
    || layout.motions.bulbSway.glowCount !== layout.motions.bulbSway.count
    || layout.motions.bulbSway.headlightGlows.some(index => index >= meshes.find(mesh => mesh.entry.name === 'lobbySet:glows')?.entry.instances)
    || meshes.filter(mesh => mesh.entry.name === 'lobbySet:moth').length !== 1)
  throw new Error('Source lobby motion metadata/geometry mismatch');

const artifacts = new Map();
function artifact(name, bytes) {
  bytes = Buffer.from(bytes);
  if (artifacts.has(name) && !artifacts.get(name).equals(bytes)) throw new Error(`Lobby export collision: ${name}`);
  artifacts.set(name, bytes);
}
for (const mesh of meshes) {
  artifact(`${mesh.shader.key}.vert`, mesh.shader.vertex);
  artifact(`${mesh.shader.key}.frag`, mesh.shader.fragment);
  artifact(`${mesh.shader.key}.uniforms`, mesh.shader.uniforms);
  artifact(`${mesh.shader.key}.textures`, JSON.stringify(mesh.entry.textures, null, 2) + '\n');
}
const sceneWriter = new Writer();
sceneWriter.raw('TSLBG001'); sceneWriter.u32(meshes.length);
for (const mesh of meshes) {
  const { entry, data, instances } = mesh;
  sceneWriter.string(entry.name); sceneWriter.string(entry.shader);
  sceneWriter.u32(data.attributes.length);
  for (const attribute of data.attributes) { sceneWriter.string(attribute.name); sceneWriter.u32(attribute.components); sceneWriter.u32(attribute.count); sceneWriter.floats(attribute.values); }
  sceneWriter.u32(data.indices.length); data.indices.forEach(index => sceneWriter.u32(index));
  sceneWriter.u32(instances.length);
  for (const instance of instances) { sceneWriter.floats(instance.matrix); sceneWriter.floats(instance.color); sceneWriter.floats([instance.phase]); }
  sceneWriter.floats(entry.state); sceneWriter.u32(Number(entry.instanced)); sceneWriter.u32(entry.instanceCapacity);
}
const rawScene = sceneWriter.done(), outer = Buffer.alloc(12);
outer.write('TSLBZ001'); outer.writeUInt32LE(rawScene.length, 8);
artifact('scene.bin.z', Buffer.concat([outer, deflateSync(rawScene, { level: 9 })]));

const sourceFiles = [
  'splat-ref/src/game/lobbySet.js', 'splat-ref/src/game/lobbySet-geo.js',
  'splat-ref/src/game/lobbySet-mats.js', 'splat-ref/src/game/lobbySet-tex.js',
  'splat-ref/src/world/texlib.js', 'splat-ref/assets/fonts/TitanOne-latin.woff2', 'splat-ref/assets/fonts/Rubik-latin.woff2',
];
const sourceHashes = {};
for (const file of sourceFiles) sourceHashes[file] = hash(await readFile(resolve(root, file)));
artifact('layout.json', JSON.stringify(layout, null, 2) + '\n');
source.dispose();

// Capture source canvas atlases, texture arrays, and the alley PMREM in Edge. The game binary consumes only this bundle.
const browser = await referenceBrowser(root, 'lobby-source-export');
const evaluate = (expression, label) => {
  let timer;
  return Promise.race([
    browser.evaluate(expression),
    new Promise((_, reject) => { timer = setTimeout(() => reject(new Error(`Source lobby capture timed out: ${label}`)), 180000); }),
  ]).finally(() => clearTimeout(timer));
};
let captured;
try {
  const requests = meshes.flatMap(mesh => mesh.entry.textures.map(t => ({ index: mesh.entry.index, uniform: t.uniform, key: t.key, dynamic: t.dynamic })));
  captured = { textureRefs: {}, textureData: {}, atlas: {} };
  captured.renderer = await evaluate(`(async()=>{
    const THREE=await import('three');
    await document.fonts.load('120px "LSTitan"');await document.fonts.load('800 100px LSRubik');await document.fonts.ready;
    const renderer=new THREE.WebGLRenderer({antialias:false,preserveDrawingBuffer:true});renderer.setSize(64,64);renderer.outputColorSpace=THREE.LinearSRGBColorSpace;
    const originalInit=renderer.initRenderTarget.bind(renderer);renderer.initRenderTarget=target=>{if(target.isWebGLArrayRenderTarget)window.__lobbyTextureTarget=target;return originalInit(target);};
    const {createTextureLibrary}=await import('/src/world/texlib.js');
    const lib=await createTextureLibrary(renderer,{size:${textureMeta.texlib.size}});
    if(lib.names.length!==${Object.keys(textureMeta.texlib.layers).length})throw new Error('Source lobby texture-library layer count differs from the captured source metadata');
    const {LobbySet}=await import('/src/game/lobbySet.js');
    const set=new LobbySet(renderer,{quality:'high',texlib:lib});await set.ready;set.root.updateMatrixWorld(true);
    const encode=bytes=>{const u=new Uint8Array(bytes.buffer,bytes.byteOffset,bytes.byteLength);let out='';for(let i=0;i<u.length;i+=32768)out+=String.fromCharCode(...u.subarray(i,i+32768));return btoa(out);};
    const uniformMap=material=>{
      if(material.isShaderMaterial)return material.uniforms;
      const sl=material.isMeshPhysicalMaterial?THREE.ShaderLib.physical:material.isMeshStandardMaterial?THREE.ShaderLib.standard:material.isMeshBasicMaterial?THREE.ShaderLib.basic:null;
      if(!sl||typeof material.onBeforeCompile!=='function')throw new Error('Unsupported source lobby material '+material.type+'/'+material.name);
      const shader={vertexShader:sl.vertexShader,fragmentShader:sl.fragmentShader,uniforms:THREE.UniformsUtils.clone(sl.uniforms)};material.onBeforeCompile(shader,renderer);return shader.uniforms;
    };
    const imageData=texture=>{
      const image=texture.image;
      if(image?.getContext){const c=image,raw=c.getContext('2d').getImageData(0,0,c.width,c.height).data;return {width:c.width,height:c.height,layers:1,bits:8,srgb:texture.colorSpace===THREE.SRGBColorSpace,flipY:true,pixels:encode(raw)};}
      if(image?.data&&ArrayBuffer.isView(image.data)){return {width:image.width,height:image.height,layers:image.depth||1,bits:image.data instanceof Uint16Array?16:8,srgb:texture.colorSpace===THREE.SRGBColorSpace,flipY:false,pixels:encode(image.data)};}
      throw new Error('Unsupported source lobby texture image: '+texture.type+' '+texture.name);
    };
    const objects=[];set.root.traverse(o=>{if(o.geometry?.attributes?.position&&o.visible)objects.push(o);});
    if(objects.length!==${meshes.length})throw new Error('Source lobby geometry count differs from offline mesh export');
    const rendererName=(()=>{const gl=renderer.getContext(),ext=gl.getExtension('WEBGL_debug_renderer_info');return gl.getParameter(ext?ext.UNMASKED_RENDERER_WEBGL:gl.RENDERER);})();
    window.__lobbyCapture={THREE,renderer,lib,set,objects,uniformMap,imageData,encode,textureObjects:new Map(),textureData:new Map()};
    return rendererName;
  })()`, 'source scene setup');
  console.log(`Source lobby GPU setup ready: ${captured.renderer}`);

  captured.textureRefs = await evaluate(`(()=>{
    const c=window.__lobbyCapture,requests=${JSON.stringify(requests)},refs={};
    for(const request of requests){
      const object=c.objects[request.index],texture=c.uniformMap(object.material)[request.uniform]?.value;
      if(!texture?.isTexture)throw new Error('Missing source lobby texture '+request.key);
      let dataKey;
      if(request.dynamic){refs[request.key]={dynamic:true};continue;}
      else if(texture===c.lib.albedo)dataKey='texlib-albedo';
      else if(texture===c.lib.normal)dataKey='texlib-normal';
      else if(texture===c.lib.orm)dataKey='texlib-orm';
      else {dataKey='image-'+texture.uuid;c.textureObjects.set(dataKey,texture);}
      refs[request.key]={dataKey,dynamic:false};
    }
    return refs;
  })()`, 'material texture map');
  captured.atlas = await evaluate(`(()=>{
    const c=window.__lobbyCapture,refs={};
    for(const [name,texture] of Object.entries(c.set.tex)){
      if(!texture?.isTexture)throw new Error('Unsupported source lobby atlas '+name);
      refs[name]='image-'+texture.uuid;c.textureObjects.set(refs[name],texture);
    }
    const cookie=c.set.lights.spill?.map;
    if(!cookie?.isTexture)throw new Error('Missing source lobby door spill cookie');
    refs.spillCookie='image-'+cookie.uuid;c.textureObjects.set(refs.spillCookie,cookie);
    return refs;
  })()`, 'canvas atlas map');
  const imageKeys = new Set([...Object.values(captured.textureRefs).filter(ref => !ref.dynamic).map(ref => ref.dataKey), ...Object.values(captured.atlas)]);
  for (const dataKey of imageKeys) {
    captured.textureData[dataKey] = await evaluate(`(()=>{
      const c=window.__lobbyCapture,key=${JSON.stringify(dataKey)},cached=c.textureData.get(key);
      if(cached)return cached;
      let image;
      if(key==='dynamic-reflection-fallback')image={width:1,height:1,layers:1,bits:8,srgb:false,flipY:false,pixels:c.encode(new Uint8Array(4)),file:key};
      else if(key.startsWith('texlib-')){
        const channelName=key.slice(7),channel=({albedo:0,normal:1,orm:2})[channelName],target=window.__lobbyTextureTarget;
        if(channel===undefined||!target)throw new Error('Missing source texture-library target: '+channelName);
        const bytes=new Uint8Array(c.lib.size*c.lib.size*4*c.lib.names.length);
        for(let layer=0;layer<c.lib.names.length;layer++){
          c.renderer.setRenderTarget(target,layer);
          const pixels=new Uint8Array(c.lib.size*c.lib.size*4);
          c.renderer.readRenderTargetPixels(target,0,0,c.lib.size,c.lib.size,pixels,undefined,channel);
          bytes.set(pixels,layer*c.lib.size*c.lib.size*4);
        }
        if(bytes.length!==c.lib.size*c.lib.size*4*c.lib.names.length)throw new Error('Invalid source texture-library capture '+channelName);
        image={width:c.lib.size,height:c.lib.size,layers:c.lib.names.length,bits:8,srgb:channel===0,flipY:false,pixels:c.encode(bytes),file:key};
      }else{
        const texture=c.textureObjects.get(key);if(!texture)throw new Error('Missing source lobby texture object '+key);
        image=c.imageData(texture);
      }
      c.textureData.set(key,image);return image;
    })()`, `texture ${dataKey}`);
    console.log(`Captured source lobby texture ${dataKey}`);
  }
  captured.pmrem = await evaluate(`(()=>{
    const c=window.__lobbyCapture,THREE=c.THREE;
    // CharacterShader uses the 256-face CubeUV constants shared with the other source scene bundles.
    // LobbySet's interactive preview bakes a smaller 128-face PMREM, so rebake its same authored proxy at 256 here.
    const env=c.set._pmrem.fromScene(c.set._envScene,0.04,0.1,100,{size:256,position:new THREE.Vector3(0.8,1.4,-1.2)});
    if(!env||env.width!==768||env.height!==1024)throw new Error('Unexpected source lobby PMREM target '+env?.width+'x'+env?.height);
    c.renderer.setRenderTarget(null);const pmrem=new Uint16Array(env.width*env.height*4);c.renderer.readRenderTargetPixels(env,0,0,env.width,env.height,pmrem);env.dispose();
    return {width:env.width,height:env.height,bits:16,pixels:c.encode(pmrem)};
  })()`, '256-face PMREM');
  captured.lighting = await evaluate(`(()=>{
    const c=window.__lobbyCapture,L=c.set.lights,hemi=L.hemi,key=L.key,spill=L.spill;
    const spot=light=>({position:light.position.toArray(),target:light.target.position.toArray(),color:light.color.toArray(),intensity:light.intensity,distance:light.distance,angle:light.angle,penumbra:light.penumbra,decay:light.decay,visible:light.visible,castShadow:!!light.castShadow});
    const point=light=>({position:light.position.toArray(),color:light.color.toArray(),intensity:light.intensity,distance:light.distance,decay:light.decay,visible:light.visible});
    key.shadow.updateMatrices(key);
    const keySpot=spot(key);keySpot.shadow={size:key.shadow.mapSize.x,aspect:key.shadow.mapSize.x/key.shadow.mapSize.y,near:key.shadow.camera.near,far:key.shadow.camera.far,bias:key.shadow.bias,normalBias:key.shadow.normalBias,radius:key.shadow.radius};
    spill.shadow.updateMatrices(spill);
    const spillSpot=spot(spill);spillSpot.cookieKey='atlas-spillCookie';spillSpot.cookieMatrix=spill.shadow.matrix.toArray();
    return {ambient:[0.08,0.08,0.1],sunDirection:key.target.position.clone().sub(key.position).normalize().toArray(),sunColor:[0,0,0],keySpot,spillSpot,rimSpot:spot(L.rim),sweep:spot(L.sweep),points:{neonA:point(L.neonA),neonB:point(L.neonB),bulbs:point(L.bulbs),vend:point(L.vend),street:point(L.street)},skyDirection:hemi.position.toArray(),skyColor:hemi.color.toArray().map(v=>v*hemi.intensity),groundColor:hemi.groundColor.toArray().map(v=>v*hemi.intensity),environmentIntensity:c.set.environmentIntensity};
  })()`, 'source lighting');
  if (!captured.lighting.spillSpot?.cookieKey || captured.lighting.spillSpot.cookieMatrix?.length !== 16
    || !captured.lighting.keySpot.castShadow || captured.lighting.keySpot.shadow?.size !== 2048
    || !['keySpot','spillSpot','rimSpot','sweep'].every(name => captured.lighting[name]?.position?.length === 3)
    || !['neonA','neonB','bulbs','vend','street'].every(name => captured.lighting.points?.[name]?.position?.length === 3))
    throw new Error('Source lobby local-light export is incomplete');
  await evaluate(`(()=>{const c=window.__lobbyCapture;c.set.dispose();c.renderer.dispose();delete window.__lobbyCapture;return true;})()`, 'source resource cleanup');
} finally {
  try { await browser.close(); }
  catch (error) {
    // Chromium can release its last profile lock just after its process exits on Windows.
    if (error?.code !== 'EBUSY') throw error;
    console.warn('Source browser profile is still closing; captured texture data is retained.');
  }
}
console.log(`Source lobby texture capture GPU: ${captured.renderer}`);

function texBytes(image) {
  const bytes = Buffer.from(image.pixels, 'base64');
  const expected = image.width * image.height * image.layers * 4 * (image.bits / 8);
  if (image.bits !== 8 || !Number.isInteger(image.width) || !Number.isInteger(image.height) || !Number.isInteger(image.layers) || bytes.length !== expected)
    throw new Error('Invalid source lobby texture capture');
  if (!image.flipY) return bytes;
  const row = image.width * 4, stride = row * image.layers, flipped = Buffer.alloc(bytes.length);
  for (let layer = 0; layer < image.layers; layer++) for (let y = 0; y < image.height; y++)
    bytes.copy(flipped, layer * stride + y * row, layer * stride + (image.height - 1 - y) * row, layer * stride + (image.height - y) * row);
  return flipped;
}
const textureFiles = {};
function textureArtifact(key, image) {
  const pixelBytes = texBytes(image);
  const header = Buffer.alloc(24); header.write('TSTEX001'); header.writeUInt32LE(image.width, 8); header.writeUInt32LE(image.height, 12); header.writeUInt32LE(image.layers, 16); header.writeUInt32LE(Number(image.srgb), 20);
  const raw = Buffer.concat([header, pixelBytes]);
  const digest = hash(raw); const file = `${digest}.tex.z`;
  artifact(file, deflateSync(raw, { level: 9 }));
  textureFiles[key] = { file, width: image.width, height: image.height, layers: image.layers, bits: image.bits, srgb: !!image.srgb, sha256: digest };
  return file;
}
for (const [key, reference] of Object.entries(captured.textureRefs)) {
  if (reference.dynamic) continue;
  const image = captured.textureData[reference.dataKey];
  if (!image) throw new Error(`Missing captured source lobby texture data ${reference.dataKey}`);
  if (image.file && textureFiles[image.file]) { textureFiles[key] = textureFiles[image.file]; continue; }
  textureArtifact(image.file || key, image);
  textureFiles[key] = textureFiles[image.file || key];
}
for (const [name, dataKey] of Object.entries(captured.atlas)) {
  const image = captured.textureData[dataKey];
  if (!image) throw new Error(`Missing captured source lobby atlas ${name}`);
  textureArtifact(`atlas-${name}`, image);
}
const pmremPixels = Buffer.from(captured.pmrem.pixels, 'base64');
if (pmremPixels.length !== 768 * 1024 * 4 * 2) throw new Error('Invalid source lobby PMREM bytes');
const pmremHeader = Buffer.alloc(20); pmremHeader.write('TSENV001'); pmremHeader.writeUInt32LE(768, 8); pmremHeader.writeUInt32LE(1024, 12); pmremHeader.writeUInt32LE(16, 16);
const pmremRaw = Buffer.concat([pmremHeader, pmremPixels]);
artifact('lobby-pmrem.rgba16f.z', deflateSync(pmremRaw, { level: 9 }));
const textureBindings = meshes.map(mesh => ({ name: mesh.entry.name, shader: mesh.entry.shader,
  textures: mesh.entry.textures.map(texture => ({ uniform: texture.uniform, key: texture.key, dynamic: texture.dynamic, file: texture.dynamic ? null : textureFiles[texture.key]?.file, sampler: texture.sampler })) }));
for (const row of textureBindings) for (const binding of row.textures) if (!binding.dynamic && !binding.file) throw new Error(`Missing exported source lobby texture ${row.name}/${binding.uniform}`);
artifact('scene-index.json', JSON.stringify({ schema: 1, geometry: 'scene.bin.z', meshes: meshes.map(mesh => mesh.entry), textureBindings,
  textures: textureFiles, pmrem: { file: 'lobby-pmrem.rgba16f.z', width: 768, height: 1024, bits: 16, sha256: hash(pmremRaw) },
  lighting: captured.lighting, omitted, sourceHashes }, null, 2) + '\n');
artifact('manifest.json', JSON.stringify({ schema: 1, sourceHashes, meshes: meshes.length, textures: Object.keys(textureFiles).length,
  shaderKeys: [...new Set(meshes.map(mesh => mesh.entry.shader))].sort(), omitted }, null, 2) + '\n');

await mkdir(output, { recursive: true });
for (const [name, bytes] of artifacts) {
  const path = resolve(output, name);
  if (check) {
    let current; try { current = await readFile(path); } catch { throw new Error(`Missing generated source lobby asset ${name}`); }
    if (!current.equals(bytes)) throw new Error(`Stale source lobby asset ${name}`);
  } else await writeFile(path, bytes);
}
const files = await readdir(output);
for (const name of files) {
  if (artifacts.has(name)) continue;
  if (check) throw new Error(`Unexpected source lobby asset ${name}`);
  if (/^(?:[0-9a-f]{20}\.(?:vert|frag|uniforms|textures)|[0-9a-f]{64}\.tex\.z)$/.test(name)) await rm(resolve(output, name));
}
console.log(`${check ? 'Checked' : 'Exported'} source lobby: ${meshes.length} meshes, ${Object.keys(textureFiles).length} textures, ${new Set(meshes.map(mesh => mesh.entry.shader)).size} material programs`);
