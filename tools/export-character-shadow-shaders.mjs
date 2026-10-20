import { resolve } from 'node:path';
import { exportCharacterShaders } from './export-character-shaders.mjs';
const args=process.argv.slice(2),check=args.includes('--check'),paths=args.filter(a=>a!=='--check');
if(paths.length>1||paths.some(a=>a.startsWith('--')))throw Error('Usage: node tools/export-character-shadow-shaders.mjs [output] [--check]');
await exportCharacterShaders(resolve(paths[0]||'assets/reference/characters/shaders-shadow'),check,{shadows:true,toneMapping:false,linearOutput:true});
