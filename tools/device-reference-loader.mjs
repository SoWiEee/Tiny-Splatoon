import {readFile} from 'node:fs/promises';
const exportsByFile={
  'shaker.js':'makeMesh','waddle.js':'makeMesh','torpedo.js':'buildRig','tracer.js':'buildHead','boomerang.js':'makeMesh','brolly.js':'buildLaunchedMesh',
};
export async function load(url,context,nextLoad){
  const result=await nextLoad(url,context);
  const name=url.split('/').pop();
  if(url.includes('/splat-ref/src/game/kits/')&&exportsByFile[name])return {...result,source:(result.source??await readFile(new URL(url),'utf8'))+`\nexport {${exportsByFile[name]} as sourceWorldModel};\n`+(['waddle.js','torpedo.js'].includes(name)?'export {makeRing as sourceRing};\n':'')+(name==='tracer.js'?'export {RIB_VS,RIB_FS};\n':'')};
  if(url.endsWith('/splat-ref/src/game/specials.js'))return {...result,source:(result.source??await readFile(new URL(url),'utf8'))+'\nexport {propMesh,partMesh,IMPL}; export async function sourcePropsReady(){PROPS=await import("./special-props.js");}\n'};
  if(url.endsWith('/splat-ref/src/game/subs.js'))return {...result,source:(result.source??await readFile(new URL(url),'utf8'))+'\nexport {CURTAIN_VS,CURTAIN_FS};\n'};
  return result;
}
