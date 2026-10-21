import { createHash } from 'node:crypto';
import { mkdir, readFile, writeFile } from 'node:fs/promises';
import { register } from 'node:module';
import { resolve } from 'node:path';

const root = resolve(import.meta.dirname, '..');
const directory = resolve(root, 'assets/reference/environment');
const check = process.argv.includes('--check');
if (process.argv.slice(2).some(arg => arg !== '--check')) throw new Error('Usage: node tools/export-environment-source.mjs [--check]');
register(new URL('./reference-loader.mjs', import.meta.url));
const THREE = await import('three');
const { Environment } = await import('../splat-ref/src/world/environment.js');
const { buildSourceMaterialShader } = await import('./export-character-shaders.mjs');
const source = await readFile(resolve(root, 'splat-ref/src/world/environment.js'), 'utf8');
const sourceHash = createHash('sha256').update(source).digest('hex');

function expression(name) {
  const marker = `const ${name} = /* glsl */\``;
  const start = source.indexOf(marker);
  if (start < 0) throw new Error(`Missing source shader ${name}`);
  const end = source.indexOf('`;\n', start + marker.length);
  if (end < 0) throw new Error(`Unterminated source shader ${name}`);
  return source.slice(start + marker.length - 1, end + 1);
}
const names = ['GLSL_NOISE', 'GLSL_SKY_COMMON', 'CLOUD_BAKE_FRAG', 'SKY_VERT', 'SKY_FRAG',
  'GLSL_DECK', 'GLSL_SWELL', 'GLSL_SEA_MARINA', 'SEA_VERT', 'SEA_FRAG'];
const evaluated = {};
for (const name of names) evaluated[name] = Function(...Object.keys(evaluated), 'MAX_RECTS', 'MAX_WET', 'WATER_Y',
  `return ${expression(name)}`)(...Object.values(evaluated), 48, 24, -1.6);

function nativeShader(name, value) {
  let shader = '#version 450 core\n';
  if (name === 'SKY_VERT') shader += 'layout(location=0) in vec3 position; uniform mat4 projectionMatrix; uniform mat4 viewMatrix; out vec3 vDir;\n';
  if (name === 'SKY_FRAG') shader += 'in vec3 vDir; layout(location=0) out vec4 fragColor; uniform vec3 cameraPosition;\n';
  shader += value.replaceAll(/#include <[^>]+>\s*/g, '').replaceAll('varying vec3 vDir;', '')
    .replaceAll('texture2D(', 'texture(').replaceAll('gl_FragColor', 'fragColor');
  return shader;
}
const shaderFiles = {
  'sky.vert': nativeShader('SKY_VERT', evaluated.SKY_VERT),
  'sky.frag': nativeShader('SKY_FRAG', evaluated.SKY_FRAG),
};
const sea=buildSourceMaterialShader(new THREE.ShaderMaterial({vertexShader:evaluated.SEA_VERT,fragmentShader:evaluated.SEA_FRAG,lights:true}),{toneMapping:false,linearOutput:true,shadows:true});
shaderFiles['sea.vert']=sea.vertex;shaderFiles['sea.frag']=sea.fragment;

function rgb(hex, multiplier = 1) { return new THREE.Color(hex).multiplyScalar(multiplier).toArray(); }
const themes = {};
for (const [name, t] of Object.entries(Environment.THEMES)) {
  const az = t.sunAz * Math.PI / 180, el = t.sunEl * Math.PI / 180;
  themes[name] = {
    sunDirection: [Math.cos(el) * Math.cos(az), Math.sin(el), Math.cos(el) * Math.sin(az)],
    sunColor: rgb(t.sunColor), sunIntensity: t.sunIntensity,
    hemisphereSky: rgb(t.hemiSky, t.hemiIntensity),
    hemisphereGround: rgb(t.hemiGround, t.hemiIntensity * (t.hemiGroundK ?? 1)),
    environmentIntensity: t.envK ?? 0.66,
    fogColor: new THREE.Color(t.horizon).lerp(new THREE.Color(t.skyMid), 0.15).toArray(), fog: t.fog,
    uZenith: rgb(t.zenith), uSkyMid: rgb(t.skyMid), uHorizon: rgb(t.horizon), uGround: rgb(t.ground),
    uHorizonGlow: rgb(t.horizonGlow, t.horizonGlowK), uGlowColor: rgb(t.glowColor), uGlowParams: t.glow,
    uHaze: [...t.haze, 0], uNight: t.night,
    uSunDisk: rgb(t.sunDisk, t.sunDiskK), uSunCos: Math.cos(t.sunRadius * Math.PI / 180),
    uCloudLit: rgb(t.cloudLit, t.cloudLitK), uCloudShade: rgb(t.cloudShade), uCloudParams: t.cloud,
    uSeaDeep: rgb(t.seaDeep), uSeaShallow: rgb(t.seaShallow), uSeaCrest: rgb(t.seaCrest), uFoamColor: rgb(t.foam),
    uSunLight: rgb(t.sunColor, (t.skySun ?? t.sunIntensity) / Math.PI),
    uSeaAmbient: new THREE.Color(t.skyMid).lerp(new THREE.Color(t.horizon), 0.5).multiplyScalar(t.seaAmbientK).toArray(),
    uSunSpec: t.sunSpec, uWaveStrength: t.waveStrength,
    uChannelCol: t.marina?.channel ? rgb(t.marina.channel) : rgb(t.seaDeep, 0.65),
    uShadeCol: t.marina?.shade ? rgb(t.marina.shade) : rgb(t.seaDeep, 0.12),
    uMarinaK: [t.marina?.calm ?? 0.55, t.marina?.lap ?? 1, 0.35, 8],
    uStripK: [t.marina?.caustic ?? 1.5,t.marina?.wet ?? .5,0,0],
    cloudCoverage: t.cloudCov ?? 0.46, cloudSeed: t.cloudSeed ?? 3,
    shafts: !!t.shafts,
    skyShafts: Number(!!t.shafts),
  };
}
const files = { ...shaderFiles, 'themes.json': JSON.stringify({ sourceHash, themes }, null, 2) + '\n' };
const seaCapture={U:{uWaveTex:{value:null}},root:new THREE.Group()};Environment.prototype._buildSea.call(seaCapture);
for(const [name,g,triangles]of [['sky',new THREE.SphereGeometry(1,48,24),true],['sea',seaCapture.sea.geometry,false]]){
  const p=g.attributes.position,ix=Array.from(g.index.array),positions=triangles?ix.flatMap(i=>[p.getX(i),p.getY(i),p.getZ(i)]):Array.from(p.array);
  const header=Buffer.alloc(8);header.writeUInt32LE(positions.length/3);header.writeUInt32LE(triangles?0:ix.length,4);
  const data=Buffer.alloc(positions.length*4+(triangles?0:ix.length*4));positions.forEach((v,i)=>data.writeFloatLE(v,i*4));if(!triangles)ix.forEach((v,i)=>data.writeUInt32LE(v,positions.length*4+i*4));
  files[name+'.geometry.bin']=Buffer.concat([header,data]);
}
for (const [theme, values] of Object.entries(themes)) {
  files[`theme-${theme}.txt`] = Object.entries(values).filter(([, value]) => typeof value === 'number' || Array.isArray(value))
    .map(([key, value]) => `${key} ${(Array.isArray(value) ? value : [value]).join(' ')}`).join('\n') + '\n';
}
await mkdir(directory, { recursive: true });
for (const [name, contents] of Object.entries(files)) {
  const path = resolve(directory, name);
  if (check) {
    if(Buffer.isBuffer(contents)){if(!contents.equals(await readFile(path)))throw new Error(`Stale environment geometry: ${name}`);}
    else if ((await readFile(path, 'utf8')).replaceAll('\r\n', '\n') !== contents) throw new Error(`Stale environment source asset: ${name}`);
  } else await writeFile(path, contents);
}
console.log(`${check ? 'Checked' : 'Exported'} ${Object.keys(files).length} environment source files.`);
