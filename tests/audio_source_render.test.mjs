import { createHash } from 'node:crypto';
import { spawn } from 'node:child_process';
import { readFile, readdir } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const audioRoot = path.join(root, 'assets', 'audio');
const manifest = JSON.parse(await readFile(path.join(audioRoot, 'manifest.json'), 'utf8'));
const fail = (message) => { throw new Error(`Audio source assets: ${message}`); };
const hash = (bytes) => createHash('sha256').update(bytes).digest('hex');

function parseWav(bytes, label) {
  if (bytes.toString('ascii', 0, 4) !== 'RIFF' || bytes.toString('ascii', 8, 12) !== 'WAVE') fail(`${label} is not RIFF/WAVE`);
  let format, channels, sampleRate, bits, dataOffset = -1, dataBytes = 0;
  for (let offset = 12; offset + 8 <= bytes.length;) {
    const size = bytes.readUInt32LE(offset + 4), payload = offset + 8;
    if (payload + size > bytes.length) fail(`${label} has a truncated chunk`);
    const tag = bytes.toString('ascii', offset, offset + 4);
    if (tag === 'fmt ' && size >= 16) {
      format = bytes.readUInt16LE(payload); channels = bytes.readUInt16LE(payload + 2);
      sampleRate = bytes.readUInt32LE(payload + 4); bits = bytes.readUInt16LE(payload + 14);
    }
    if (tag === 'data') { dataOffset = payload; dataBytes = size; }
    offset = payload + size + (size & 1);
  }
  if (format !== 1 || channels !== 2 || sampleRate !== 48000 || bits !== 16 || dataOffset < 0 || dataBytes % 4)
    fail(`${label} must be stereo 48 kHz PCM16`);
  return { channels, sampleRate, bits, dataOffset, dataBytes, frames: dataBytes / 4 };
}

async function filesUnder(directory, prefix = '') {
  const files = [];
  for (const entry of await readdir(directory, { withFileTypes: true })) {
    const relative = path.posix.join(prefix, entry.name);
    if (entry.isDirectory()) files.push(...await filesUnder(path.join(directory, entry.name), relative));
    else if (entry.isFile()) files.push(relative);
  }
  return files;
}

async function decodePcmSha256(file) {
  const child = spawn(process.env.FFMPEG_PATH || 'ffmpeg', [
    '-v', 'error', '-i', file, '-f', 's16le', '-acodec', 'pcm_s16le', 'pipe:1',
  ], { cwd: root, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
  const digest = createHash('sha256');
  let bytes = 0, stderr = '';
  child.stdout.on('data', (chunk) => { bytes += chunk.length; digest.update(chunk); });
  child.stderr.setEncoding('utf8'); child.stderr.on('data', (chunk) => { stderr += chunk; });
  const code = await new Promise((resolve, reject) => {
    child.once('error', reject);
    child.once('close', resolve);
  });
  if (code !== 0) fail(`ffmpeg could not decode ${path.relative(root, file)} (${code}): ${stderr.trim()}`);
  return { bytes, sha256: digest.digest('hex') };
}

if (manifest.schema !== 3 || manifest.render?.sampleRate !== 48000 || manifest.render?.channels !== 2 || manifest.render?.bitsPerSample !== 16)
  fail('manifest schema or PCM render contract changed');
if (manifest.render?.checkTolerancePcm16Lsb !== 8 ||
    manifest.render?.musicCheck?.maxPcm16Lsb !== 1024 ||
    manifest.render?.musicCheck?.nrmseFullScaleMax !== 0.003 ||
    manifest.render?.musicCheck?.correlationMin !== 0.9999)
  fail('SFX/loop or master-compressed music render comparison bounds changed');
if(manifest.render.musicCheck.menu?.maxPcm16Lsb!==1024||manifest.render.musicCheck.menu?.nrmseFullScaleMax!==.004||manifest.render.musicCheck.menu?.correlationMin!==.9997)
  fail('source-measured menu comparison bounds changed');
if (!Array.isArray(manifest.clips) || manifest.clips.length < 200 || !Array.isArray(manifest.music) || manifest.music.length !== 9)
  fail(`expected the complete SFX bank and nine source tracks; found ${manifest.clips?.length ?? 0} cues and ${manifest.music?.length ?? 0} tracks`);

const tracks = new Set(manifest.music.map((entry) => entry.name));
const requiredTracks = ['title', 'menu', 'battle', 'battle_final', 'results_win', 'results_lose', 'boss', 'boss_2', 'boss_3'];
if (requiredTracks.some((track) => !tracks.has(track)) || tracks.size !== requiredTracks.length)
  fail('source soundtrack must contain title, menu, battle, final-minute, both results, and all three boss phases');
const director = await readFile(path.join(root, 'src', 'audio', 'GameAudio.cpp'), 'utf8');
const musicCalls = [...director.matchAll(/PlayMusic\("([a-z0-9_]+)"/g)].map((match) => match[1]);
for (const track of musicCalls) if (!tracks.has(track)) fail(`GameAudio requests missing music track ${track}`);
for (const track of ['title', 'menu', 'battle', 'battle_final', 'results_win', 'results_lose', 'boss', 'boss_2', 'boss_3'])
  if (!director.includes(`"${track}"`)) fail(`GameAudio does not route the ${track} music state`);
if (!director.includes('return phase >= 3 ? "boss_3" : phase == 2 ? "boss_2" : "boss";'))
  fail('Boss music must select its three source intensity phases');

const actorRouting = director.slice(director.indexOf('void GameAudio::ActorTrigger'), director.indexOf('void GameAudio::Menu'));
for (const trigger of ['spawn', 'jump', 'special_leap', 'land', 'hit', 'shoot', 'stream', 'charge_release', 'throw', 'flick',
  'slosh', 'blade_slash', 'blade_heavy', 'dodge', 'roll'])
  if (!actorRouting.includes(`"${trigger}"`)) fail(`GameAudio is missing native actor trigger route ${trigger}`);
const worldRouting = director.slice(director.indexOf('void GameAudio::Update('));
for (const event of ['explosion', 'bulletImpact', 'beam', 'slamWave', 'stormStart', 'burst'])
  if (!worldRouting.includes(`"${event}"`)) fail(`GameAudio is missing world event disposition ${event}`);
const footstepRouting = director.slice(director.indexOf('void GameAudio::Footstep'), director.indexOf('void GameAudio::ActorTrigger'));
for (const cue of ['step_dry', 'step_ink', 'step_enemy'])
  if (!footstepRouting.includes(`"${cue}"`)) fail(`GameAudio is missing the ${cue} touchdown route`);
const actorLoopRouting = director.slice(director.indexOf('void GameAudio::UpdateActorAudio'), director.indexOf('void GameAudio::UpdateWorldAudio'));
for (const cue of ['"climb"', '"enemy_ink_sizzle"', 'state.submerged'])
  if (!actorLoopRouting.includes(cue)) fail(`GameAudio is missing source local-state loop ${cue}`);
const worldAmbienceRouting = director.slice(director.indexOf('void GameAudio::UpdateWorldAudio'), director.indexOf('void GameAudio::UpdateBoss'));
for (const cue of ['"harbor_ambience"', '"gull"', '"halyard_clink"', '"ferry_horn"'])
  if (!worldAmbienceRouting.includes(cue)) fail(`GameAudio is missing the ${cue} stage ambience route`);
if (!worldAmbienceRouting.includes('AudioRandom(audioRandomState_)')) fail('World ambience must use its independent audio random stream');
if (!director.includes('void GameAudio::Pause()') || !director.includes('resumeLoops_ = true'))
  fail('GameAudio must pause managed loops and recreate them on resume');

const catalogText = (await readFile(path.join(audioRoot, 'catalog.tsv'), 'utf8')).trimEnd();
const catalog = new Map(catalogText.split(/\r?\n/).map((line) => {
  const [name, maxVoices, pitchJitter, minGapSeconds, loopable] = line.split('\t');
  if (!name || !maxVoices || pitchJitter === undefined || minGapSeconds === undefined || !['0', '1'].includes(loopable)) fail(`invalid catalog row: ${line}`);
  return [name, { maxVoices: Number(maxVoices), pitchJitter: Number(pitchJitter), minGapSeconds: Number(minGapSeconds), loopable: loopable === '1' }];
}));
if (catalog.size !== manifest.clips.length) fail('catalog and manifest cue counts differ');
const clips = new Map(manifest.clips.map((clip) => [clip.name, clip]));
if (clips.size !== manifest.clips.length || [...catalog.keys()].some((name) => !clips.has(name))) fail('catalog and manifest cue names differ');

const requiredCues = [
  'shoot_shooter', 'shoot_blaster', 'shoot_charger', 'shoot_dualies', 'shoot_splatling', 'slosh_throw', 'roller_flick',
  'bow_loose', 'blade_swish', 'dualies_roll', 'swim', 'splatling_spin', 'bomb_throw', 'low_ink', 'refill_full',
  'squid_in', 'squid_out', 'respawn', 'splatted_self', 'ally_splatted', 'splat_enemy', 'special_ready', 'special_activate',
  'special_end', 'crab_move', 'crab_cannon', 'crab_gatling', 'jet_loop', 'kraken_on', 'kraken_jump', 'kraken_slam',
  'stamp_swing', 'stamp_slam', 'stamp_throw', 'storm_rain', 'storm_thunder', 'booyah_charge', 'booyah_throw', 'booyah_blast',
  'zip_fire', 'zip_pull', 'wail_charge', 'wail_blast', 'strike_launch', 'strike_whistle', 'strike_impact', 'zooka_fire',
  'boss_tele', 'boss_cannon_sweep', 'boss_gallop', 'boss_frenzy', 'boss_dizzy', 'boss_phase', 'boss_defeat', 'crablet_chitter',
  'ready', 'go_horn', 'one_minute', 'times_up', 'victory_fanfare', 'defeat_jingle',
  'step_dry', 'step_ink', 'step_enemy', 'climb', 'enemy_ink_sizzle', 'gull', 'harbor_ambience', 'halyard_clink', 'ferry_horn',
];
for (const name of requiredCues) if (!clips.has(name)) fail(`missing source cue required by playable actor, match, or Boss routing: ${name}`);

const comparisonTest = spawn(process.execPath, [path.join(root, 'tools', 'export-audio-source.mjs'), '--test-music-comparison'], {
  cwd: root, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'],
});
let comparisonOutput = '', comparisonError = '';
comparisonTest.stdout.setEncoding('utf8'); comparisonTest.stdout.on('data', (chunk) => { comparisonOutput += chunk; });
comparisonTest.stderr.setEncoding('utf8'); comparisonTest.stderr.on('data', (chunk) => { comparisonError += chunk; });
const comparisonExit = await new Promise((resolve, reject) => {
  comparisonTest.once('error', reject);
  comparisonTest.once('close', resolve);
});
if (comparisonExit !== 0 || !comparisonOutput.includes('one-frame shift rejected'))
  fail(`music comparison negative control failed (${comparisonExit}): ${comparisonError.trim()}`);

const mapperStart = director.indexOf('const char* SpecialWeaponSound');
const mapperEnd = director.indexOf('AudioPosition BossPosition');
if (mapperStart < 0 || mapperEnd <= mapperStart) fail('could not inspect actor and special cue routing');
const mapperBlock = director.slice(mapperStart, mapperEnd);
const cueReferences = new Set([
  ...[...director.matchAll(/(?:engine_\.)?Play\("([a-z0-9_]+)"/g)].map((match) => match[1]),
  ...[...director.matchAll(/return\s+"([a-z0-9_]+)"/g)].map((match) => match[1]).filter((name) => mapperBlock.includes(`return "${name}"`)),
  ...[...director.matchAll(/desired\s*=\s*"([a-z0-9_]+)"/g)].map((match) => match[1]),
  ...[...director.matchAll(/StartLoop\([^,]+,\s*"([a-z0-9_]+)"/g)].map((match) => match[1]),
]);
for (const name of cueReferences) if (!clips.has(name)) fail(`GameAudio cue ${name} has no exported source clip`);
for (const clip of manifest.clips) {
  const meta = catalog.get(clip.name);
  if (clip.sourceMaxVoices !== meta.maxVoices || clip.sourcePitchJitter !== meta.pitchJitter ||
      clip.sourceMinGapSeconds !== meta.minGapSeconds || clip.loopable !== meta.loopable)
    fail(`source voice/pitch/gap/loop metadata differs for ${clip.name}`);
  const wav = await readFile(path.join(audioRoot, clip.file));
  const parsed = parseWav(wav, clip.file);
  if (hash(wav) !== clip.sha256 || wav.length !== clip.bytes || parsed.frames !== clip.frames ||
      parsed.sampleRate !== clip.sampleRate || parsed.channels !== clip.channels || parsed.bits !== clip.bitsPerSample ||
      parsed.dataBytes !== clip.frames * clip.channels * 2)
    fail(`source-rendered WAV hash or PCM metadata changed for ${clip.name}`);
  if (meta.loopable !== !!clip.loop) fail(`loop metadata does not match exported loop asset for ${clip.name}`);
  if (clip.loop) {
    const loopWav = await readFile(path.join(audioRoot, clip.loop.file));
    const loopParsed = parseWav(loopWav, clip.loop.file);
    if (hash(loopWav) !== clip.loop.sha256 || loopWav.length !== clip.loop.bytes || loopParsed.frames !== clip.loop.frames)
      fail(`source-rendered loop asset changed for ${clip.name}`);
  }
}

const musicIndex = (await readFile(path.join(audioRoot, 'music.tsv'), 'utf8')).trimEnd().split(/\r?\n/);
if (musicIndex.length !== manifest.music.length) fail('music index and manifest track counts differ');
const expectedMusicFiles = new Set(['manifest.json', 'catalog.tsv', 'music.tsv']);
for (let i = 0; i < manifest.music.length; i++) {
  const track = manifest.music[i];
  const [name, frames, loopBegin, loopEnd, bpm] = musicIndex[i].split('\t');
  if (name !== track.name || Number(frames) !== track.frames || Number(loopBegin) !== track.loopBeginFrame ||
      Number(loopEnd) !== track.loopEndFrame || Number(bpm) !== track.bpm || !track.bpm)
    fail(`source loop index differs for ${track.name}`);
  if (!track.loopBeginFrame || track.loopEndFrame <= track.loopBeginFrame || track.loopEndFrame > track.frames)
    fail(`${track.name} must preserve a non-empty intro and valid source loop interval`);
  const file = path.join(audioRoot, track.file);
  const flac = await readFile(file);
  if (flac.length !== track.bytes || hash(flac) !== track.sha256) fail(`lossless FLAC file hash changed for ${track.name}`);
  const decoded = await decodePcmSha256(file);
  if (decoded.bytes !== track.frames * track.channels * 2 || decoded.sha256 !== track.decodedPcmSha256)
    fail(`decoded PCM hash, frame count, or format changed for ${track.name}`);
  expectedMusicFiles.add(track.file);
}

for (const clip of manifest.clips) {
  expectedMusicFiles.add(clip.file);
  if (clip.loop) expectedMusicFiles.add(clip.loop.file);
}
const actual = new Set((await filesUnder(audioRoot)).filter(file=>file!=='custom-music.tsv'&&!file.startsWith('songs/')));
if (actual.size !== expectedMusicFiles.size || [...expectedMusicFiles].some((file) => !actual.has(file)))
  fail('asset folder has an unexpected file or a missing source export');

console.log(`Audio source inventory passed: ${clips.size} SFX (${[...catalog.values()].filter((entry) => entry.loopable).length} loops), ${tracks.size} music tracks, and ${cueReferences.size} resolved GameAudio cue names.`);
console.log('Full source re-render acceptance remains explicit: node tools/export-audio-source.mjs --check');
