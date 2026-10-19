import { register } from 'node:module';
import { mkdir, writeFile, readFile } from 'node:fs/promises';
import { execFileSync } from 'node:child_process';
import { createHash } from 'node:crypto';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

register(new URL('./reference-loader.mjs', import.meta.url));
await import('../splat-ref/src/game/kits/index.js');
const THREE = await import('three');
const { Character } = await import('../splat-ref/src/game/character.js');
const MATS = await import('../splat-ref/src/game/character-mats.js');
const { makeCoilMaterial, makeLampMaterial } = await import('../splat-ref/src/game/character-weapons.js');
const { makeFabric } = await import('../splat-ref/src/game/kits/brolly-model.js');

function plain(value) {
  if (typeof value === 'number' || typeof value === 'boolean') return value;
  if (value?.toArray) return value.toArray();
  if (Array.isArray(value) || ArrayBuffer.isView(value)) return Array.from(value, plain);
  return null;
}

function expand(source, stack = []) {
  return source.replace(/#include\s+<([\w\d_]+)>/g, (_, name) => {
    if (!(name in THREE.ShaderChunk)) throw new Error(`Missing source shader chunk ${name}`);
    if (stack.includes(name)) throw new Error(`Recursive shader chunk ${name}`);
    return `\n// source chunk: ${name}\n${expand(THREE.ShaderChunk[name], [...stack, name])}\n`;
  });
}

function counts(source, lighting) {
  return source.replace(/\b(NUM_[A-Z_]+|UNION_CLIPPING_PLANES)\b/g, name => String(lighting[name] ?? 0));
}

function unroll(source) {
  return source.replace(/#pragma unroll_loop_start\s+for\s*\(\s*int i\s*=\s*(\d+)\s*;\s*i\s*<\s*(\d+)\s*;\s*i\s*\+\+\s*\)\s*\{([\s\S]+?)\}\s*#pragma unroll_loop_end/g,
    (_, start, end, body) => {
      let text = '';
      for (let i = Number(start); i < Number(end); ++i)
        text += body.replace(/\[\s*i\s*\]/g, `[${i}]`).replace(/UNROLLED_LOOP_INDEX/g, String(i));
      return text;
    });
}

function defines(material, options) {
  const names = ['HIGH_PRECISION'];
  if (material.isMeshPhysicalMaterial) names.push('PHYSICAL');
  if (material.vertexColors) names.push('USE_COLOR');
  if (material.clearcoat > 0) names.push('USE_CLEARCOAT');
  if (material.sheen > 0) names.push('USE_SHEEN');
  if (material.side === THREE.DoubleSide) names.push('DOUBLE_SIDED');
  if (material.side === THREE.BackSide) names.push('FLIP_SIDED');
  if (material.premultipliedAlpha) names.push('PREMULTIPLIED_ALPHA');
  if (!material.transparent) names.push('OPAQUE');
  if (options.skinned) names.push('USE_SKINNING');
  if (options.toneMapping !== false && material.toneMapped) names.push('TONE_MAPPING');
  if (options.environment) names.push('USE_ENVMAP', 'ENVMAP_TYPE_CUBE_UV');
  return names.map(name => `#define ${name}`).join('\n') + '\n' +
    Object.entries(material.defines ?? {}).map(([name, v]) => `#define ${name} ${v}`).join('\n') + '\n';
}

/** Expand the real material hooks and physical shader chunks. Only the API prefix changes for desktop GL. */
export function buildCharacterShader(material, options = {}) {
  if(material.isShaderMaterial)return buildCustomShader(material,options);
  if (!material.isMeshStandardMaterial) throw new Error(`Unsupported source character material ${material.type}`);
  const lib = material.isMeshPhysicalMaterial ? THREE.ShaderLib.physical : THREE.ShaderLib.standard;
  const shader = { vertexShader: lib.vertexShader, fragmentShader: lib.fragmentShader, uniforms: THREE.UniformsUtils.clone(lib.uniforms) };
  material.onBeforeCompile(shader, null);
  const u = shader.uniforms;
  const put = (name, v) => { if (v !== undefined) u[name] = { value: v }; };
  put('diffuse', material.color); put('emissive', material.emissive.clone().multiplyScalar(material.emissiveIntensity));
  for (const k of ['roughness', 'metalness', 'opacity', 'ior', 'clearcoat', 'clearcoatRoughness', 'specularIntensity', 'specularColor', 'sheenRoughness', 'envMapIntensity']) put(k, material[k]);
  if (material.sheenColor) put('sheenColor', material.sheenColor.clone().multiplyScalar(material.sheen));
  put('toneMappingExposure', 1.0);
  const lighting = { NUM_DIR_LIGHTS: 1, NUM_HEMI_LIGHTS: 1, ...options.lighting };
  const featureDefines = defines(material, options);
  const matrices = 'uniform mat4 modelMatrix; uniform mat4 modelViewMatrix; uniform mat4 projectionMatrix; uniform mat4 viewMatrix; uniform mat3 normalMatrix; uniform vec3 cameraPosition; uniform bool isOrthographic;\n';
  const commonPrefix = `#version 450 core\n${featureDefines}#define texture2D texture\n#define textureCube texture\n#define texture2DLodEXT textureLod\n#define textureCubeLodEXT textureLod\n#define texture2DGradEXT textureGrad\n${matrices}`;
  const vertexPrefix = commonPrefix + '#define attribute in\n#define varying out\nin vec3 position; in vec3 normal; in vec2 uv;\n#ifdef USE_COLOR\nin vec3 color;\n#endif\n#ifdef USE_SKINNING\nin vec4 skinIndex; in vec4 skinWeight;\n#endif\n';
  const fragmentPrefix = commonPrefix + '#define varying in\nlayout(location=0) out vec4 iwFragColor;\n#define gl_FragColor iwFragColor\n#define gl_FragDepthEXT gl_FragDepth\n';
  let fragment = shader.fragmentShader.replace('#include <common>', `#include <common>\n#include <tonemapping_pars_fragment>\n#include <colorspace_pars_fragment>\nvec3 toneMapping(vec3 color) { return NeutralToneMapping(color); }\nvec4 linearToOutputTexel(vec4 color) { return ${options.linearOutput ? 'LinearTransferOETF' : 'sRGBTransferOETF'}(color); }`);
  // CubeUV atlas dimensions are source PMREM parameters supplied by the native environment exporter.
  let environmentDefines = '';
  if (options.environment) {
    const { texelWidth, texelHeight, maxMip } = options.environment;
    if (![texelWidth, texelHeight, maxMip].every(Number.isFinite)) throw new Error('CubeUV environment requires finite texelWidth, texelHeight, maxMip');
    environmentDefines = `#define CUBEUV_TEXEL_WIDTH ${texelWidth.toPrecision(12)}\n#define CUBEUV_TEXEL_HEIGHT ${texelHeight.toPrecision(12)}\n#define CUBEUV_MAX_MIP ${maxMip.toFixed(1)}\n`;
  }
  const vertex = vertexPrefix + environmentDefines + unroll(counts(expand(shader.vertexShader), lighting));
  fragment = fragmentPrefix + environmentDefines + unroll(counts(expand(fragment), lighting));
  if (/unroll_loop_start|#include\s*</.test(vertex + fragment)) throw new Error('Shader preprocessing left an unresolved directive');
  const uniforms = Object.fromEntries(Object.entries(u).map(([k, item]) => [k, plain(item.value)]).filter(([, v]) => v !== null));
  return {
    vertex, fragment, uniforms,
    sourceKey: material.customProgramCacheKey(),
    renderState: { transparent: material.transparent, opacity: material.opacity, depthWrite: material.depthWrite, depthTest: material.depthTest,
      depthFunc: material.depthFunc, side: material.side, blending: material.blending, premultipliedAlpha: material.premultipliedAlpha },
    skinned: !!options.skinned, lighting,
    digest: createHash('sha256').update(vertex).update(fragment).digest('hex'),
  };
}

function buildCustomShader(material,options){
  const featureDefines=defines(material,options);
  const common=`#version 450 core\n${featureDefines}#define texture2D texture\nuniform mat4 modelMatrix,modelViewMatrix,projectionMatrix,viewMatrix;uniform mat3 normalMatrix;uniform vec3 cameraPosition;\n`;
  const vertex=common+'#define varying out\n#define attribute in\nin vec3 position,normal;in vec2 uv;\n'+expand(material.vertexShader);
  const fragment=common+'#define varying in\nlayout(location=0)out vec4 iwFragColor;\n#define gl_FragColor iwFragColor\n'+expand(THREE.ShaderChunk.common)+'\n'+expand(THREE.ShaderChunk.tonemapping_pars_fragment)+'\n'+expand(THREE.ShaderChunk.colorspace_pars_fragment)+'\nvec3 toneMapping(vec3 color){return NeutralToneMapping(color);}\nvec4 linearToOutputTexel(vec4 color){return sRGBTransferOETF(color);}\n'+expand(material.fragmentShader);
  return {vertex,fragment,uniforms:Object.fromEntries(Object.entries(material.uniforms).map(([key,u])=>[key,plain(u.value)])),sourceKey:material.customProgramCacheKey(),
    renderState:{transparent:material.transparent,opacity:material.opacity,depthWrite:material.depthWrite,depthTest:material.depthTest,depthFunc:material.depthFunc,side:material.side,blending:material.blending,premultipliedAlpha:material.premultipliedAlpha},
    skinned:false,lighting:{},digest:createHash('sha256').update(vertex).update(fragment).digest('hex')};
}

export async function exportCharacterShaders(output,check=false) {
  await mkdir(output, { recursive: true });
  async function artifact(name,content){const path=resolve(output,name);if(check){if(await readFile(path,'utf8')!==content)throw new Error(`Stale source character shader: ${name}`);}else await writeFile(path,content);}
  const character = new Character({ name: 'pose-oracle', style: { hair: 0, skin: 0, outfit: 0, eyes: 0 } });
  const entries = [];
  try {
    const materials = { ...character.mats, glass: MATS.getGlassMaterial(), plastic: MATS.getPlasticMaterial(), ink: MATS.getInkMaterial(character.color), coil: makeCoilMaterial(), lamp: makeLampMaterial() };
    materials.brollyInk=makeFabric(character.color,true);materials.brollyCream=makeFabric(new THREE.Color(1,1,1),false);
    for(const key of ['brollyInk','brollyCream']){materials[key+'_local']=materials[key].clone();materials[key+'_local'].transparent=true;}
    character.setWeapon('blade');character._animWeapon(0,{},character.weapon);
    materials.bladeEdge=character.weapon.bl.em;materials.bladeAura=character.weapon.bl.am;
    character.setWeapon('mitts');character._animWeapon(0,{},character.weapon);
    materials.sponge=character.weapon.sponge;
    for (const [key, material] of Object.entries(materials)) {
      const skinned = ['skin', 'cloth', 'hair', 'dark', 'eye'].includes(key);
      const variants = [{ key, skinned }];
      if (key === 'dark' || key === 'eye') variants.push({ key: `${key}_plain`, skinned: false });
      for (const variant of variants) {
        const data = buildCharacterShader(material, { skinned: variant.skinned });
        await artifact(`${variant.key}.vert`, data.vertex);
        await artifact(`${variant.key}.frag`, data.fragment);
        entries.push({ key: variant.key, ...data, vertex: `${variant.key}.vert`, fragment: `${variant.key}.frag` });
      }
    }
  } finally { character.dispose(); }
  await artifact('manifest.json', JSON.stringify({ version: 1, generator: 'source material.onBeforeCompile + source ShaderChunk expansion', entries }, null, 2) + '\n');
  const license=(await readFile(new URL('../LICENSE',import.meta.url),'utf8')).replace(/\r\n?/g,'\n').replace('Copyright (c) 2025 SoWiEee','Copyright (c) 2010-2026 Three.js Authors');
  await artifact('LICENSE.three.txt',license);
  console.log(`${check?'Verified':'Expanded'} ${entries.length} source character shader pairs -> ${output}`);
  return entries;
}

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href){
  const args=process.argv.slice(2),check=args.includes('--check'),paths=args.filter(v=>v!=='--check');
  if(paths.length>1||paths.some(v=>v.startsWith('--')))throw new Error('Usage: node tools/export-character-shaders.mjs [output-directory] [--check]');
  execFileSync(process.execPath,['tools/export-reference.mjs','--check'],{cwd:resolve(import.meta.dirname,'..'),stdio:'pipe'});
  await exportCharacterShaders(resolve(paths[0]??'assets/reference/characters/shaders'),check);
}
