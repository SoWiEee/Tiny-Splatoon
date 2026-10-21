import test from 'node:test';
import assert from 'node:assert/strict';
import {mkdtemp,mkdir,writeFile,readFile,rm} from 'node:fs/promises';
import {tmpdir} from 'node:os';
import {join} from 'node:path';
import {importMusic} from '../tools/import-music.mjs';
test('optional source songs preserve gain and reject invalid gain or escaping paths',async()=>{
  const root=await mkdtemp(join(tmpdir(),'tiny-music-'));
  try{
    const source=join(root,'source'),out=join(root,'out');await mkdir(join(source,'in game'),{recursive:true});
    await writeFile(join(source,'in game','測試.wav'),Buffer.from('fixture'));
    const manifest=entry=>writeFile(join(source,'manifest.json'),JSON.stringify({tracks:{battle:[entry]}}));
    await manifest({url:'songs/in%20game/%E6%B8%AC%E8%A9%A6.wav',gainDb:-3.2});
    assert.equal(await importMusic(source,out),1);assert.match(await readFile(join(out,'custom-music.tsv'),'utf8'),/^battle\t-3.2\tsongs\/[a-f0-9]{64}\.wav\n$/);
    await manifest({url:'songs/../escape.wav'});await assert.rejects(importMusic(source,out),/escapes/);
    // JSON.stringify turns Infinity into null; it must not be mistaken for the default zero gain.
    await manifest({url:'songs/in%20game/測試.wav',gainDb:Infinity});
    await assert.rejects(importMusic(source,out),/Invalid source song entry/);
    await manifest({url:'songs/in%20game/測試.wav',gainDb:null});
    await assert.rejects(importMusic(source,out),/Invalid source song entry/);
    await manifest({url:'songs/in%20game/測試.wav',gainDb:'-3.2'});
    await assert.rejects(importMusic(source,out),/Invalid source song entry/);
  }finally{await rm(root,{recursive:true,force:true});}
});
