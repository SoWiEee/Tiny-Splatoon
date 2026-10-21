import {readFile,writeFile,mkdir,copyFile,rename,realpath} from 'node:fs/promises';
import {resolve,relative,isAbsolute,extname} from 'node:path';
import {createHash} from 'node:crypto';
import {fileURLToPath} from 'node:url';

// Convert the reference songs manifest to the native index; recordings remain optional.
export async function importMusic(directory,output){
  directory=await realpath(resolve(directory));output=resolve(output);
  const manifest=JSON.parse(await readFile(resolve(directory,'manifest.json'),'utf8'));
  const rows=[];
  for(const track of ['battle','battle_final'])for(const entry of manifest.tracks?.[track]??[]){
    if(!entry||typeof entry.url!=='string')throw Error('Invalid source song entry');
    const url=decodeURIComponent(entry.url),gain=entry.gainDb===undefined?0:entry.gainDb;
    if(!url.startsWith('songs/')||/[\t\r\n\\]/.test(url)||typeof gain!=='number'||!Number.isFinite(gain)||gain< -60||gain>24)throw Error('Invalid source song entry');
    const source=resolve(directory,url.slice(6)),rel=relative(directory,source);
    if(isAbsolute(rel)||rel==='..'||rel.startsWith('..\\')||rel.startsWith('../'))throw Error('Song path escapes source directory');
    const actualSource=await realpath(source),actualRel=relative(directory,actualSource);
    if(isAbsolute(actualRel)||actualRel==='..'||actualRel.startsWith('..\\')||actualRel.startsWith('../'))throw Error('Song path escapes source directory');
    const extension=extname(actualSource).toLowerCase();
    if(!['.wav','.flac','.mp3'].includes(extension))throw Error(`Unsupported native recording: ${extension}; convert to WAV, FLAC or MP3`);
    const bytes=await readFile(actualSource),name=createHash('sha256').update(bytes).digest('hex')+extension;
    await mkdir(resolve(output,'songs'),{recursive:true});await copyFile(source,resolve(output,'songs',name));
    rows.push(`${track}\t${gain}\tsongs/${name}`);
  }
  await mkdir(output,{recursive:true});const target=resolve(output,'custom-music.tsv');
  await writeFile(target+'.tmp',rows.join('\n')+'\n');await rename(target+'.tmp',target);
  return rows.length;
}
if(process.argv[1]&&resolve(process.argv[1])===fileURLToPath(import.meta.url)){
  if(process.argv.length!==3)throw Error('Usage: node tools/import-music.mjs <reference songs directory>');
  console.log(`Imported ${await importMusic(process.argv[2],'assets/audio')} optional recordings`);
}
