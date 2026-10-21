import { createServer } from 'node:http';
import { spawn } from 'node:child_process';
import { createHash } from 'node:crypto';
import { createReadStream } from 'node:fs';
import { existsSync } from 'node:fs';
import { mkdir, mkdtemp, readFile, readdir, rm, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const sourceRoot = path.join(root, 'splat-ref');
const outputRoot = path.join(root, 'assets', 'audio');
const checking = process.argv.includes('--check');
const comparisonSelfTest = process.argv.includes('--test-music-comparison');
if (process.argv.slice(2).some((arg) => arg !== '--check' && arg !== '--test-music-comparison')) throw new Error('Usage: node tools/export-audio-source.mjs [--check]');
if (checking && comparisonSelfTest) throw new Error('Choose either --check or --test-music-comparison');
let selected = [];

const musicComparisonLimits = { maxPcm16Lsb: 1024, nrmseFullScale: 0.003, minCorrelation: 0.9999 };
// Three fresh source menu renders include a repeatable alternate waveform; see the calibration report.
const menuComparisonLimits={maxPcm16Lsb:1024,nrmseFullScale:0.004,minCorrelation:0.9997};

const mime = new Map([
  ['.html', 'text/html; charset=utf-8'], ['.js', 'text/javascript; charset=utf-8'],
  ['.mjs', 'text/javascript; charset=utf-8'], ['.json', 'application/json'],
  ['.css', 'text/css; charset=utf-8'], ['.svg', 'image/svg+xml'],
]);
const server = createServer((req, res) => {
  let pathname;
  try { pathname = decodeURIComponent(new URL(req.url, 'http://localhost').pathname); }
  catch { res.writeHead(400).end(); return; }
  const render = /^\/__audio-render\/(music-[a-z0-9_-]+\.wav)$/.exec(pathname);
  if (req.method === 'POST' && render) {
    const chunks = []; let size = 0;
    req.on('data', (chunk) => { size += chunk.length; if (size > 128 * 1024 * 1024) { req.destroy(); return; } chunks.push(chunk); });
    req.on('end', async () => {
      try { await writeFile(path.join(temp, render[1]), Buffer.concat(chunks)); res.writeHead(204).end(); }
      catch { res.writeHead(500).end(); }
    });
    return;
  }
  const file = path.resolve(sourceRoot, `.${pathname}`);
  if (file !== sourceRoot && !file.startsWith(sourceRoot + path.sep)) { res.writeHead(403).end(); return; }
  if (!['GET', 'HEAD'].includes(req.method)) { res.writeHead(405).end(); return; }
  const stream = createReadStream(file);
  stream.on('error', () => { if (!res.headersSent) res.writeHead(404); res.end(); });
  stream.on('open', () => { res.writeHead(200, { 'Content-Type': mime.get(path.extname(file)) || 'application/octet-stream', 'Cache-Control': 'no-cache' }); if (req.method === 'HEAD') { stream.destroy(); res.end(); } else stream.pipe(res); });
});

const edgeCandidates = [process.env.EDGE_PATH,
  process.env['PROGRAMFILES(X86)'] && path.join(process.env['PROGRAMFILES(X86)'], 'Microsoft', 'Edge', 'Application', 'msedge.exe'),
  process.env.PROGRAMFILES && path.join(process.env.PROGRAMFILES, 'Microsoft', 'Edge', 'Application', 'msedge.exe'),
  process.env.LOCALAPPDATA && path.join(process.env.LOCALAPPDATA, 'Microsoft', 'Edge', 'Application', 'msedge.exe')].filter(Boolean);
const edgePath = edgeCandidates.find(existsSync);
if (!edgePath) throw new Error(`Microsoft Edge was not found. Set EDGE_PATH to msedge.exe. Tried: ${edgeCandidates.join(', ')}`);

function waitForExit(child) {
  return new Promise((resolve) => child.exitCode !== null ? resolve() : child.once('exit', resolve));
}
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

async function launchEdge(profile) {
  const child = spawn(edgePath, ['--headless=new', '--disable-gpu', '--no-first-run', '--no-default-browser-check',
    '--disable-extensions', '--autoplay-policy=no-user-gesture-required', '--remote-debugging-port=0',
    `--user-data-dir=${profile}`, 'about:blank'], { windowsHide: true, stdio: 'ignore' });
  const activePortFile = path.join(profile, 'DevToolsActivePort');
  let port;
  for (let i = 0; i < 100; i++) {
    if (child.exitCode !== null) throw new Error(`Edge exited before DevTools started (${child.exitCode})`);
    try { port = Number((await readFile(activePortFile, 'utf8')).split(/\r?\n/)[0]); if (port > 0) break; } catch {}
    await sleep(100);
  }
  if (!port) { child.kill(); throw new Error('Timed out waiting for Edge DevToolsActivePort'); }
  const version = await (await fetch(`http://127.0.0.1:${port}/json/version`)).json();
  const socket = new WebSocket(version.webSocketDebuggerUrl);
  await new Promise((resolve, reject) => { socket.addEventListener('open', resolve, { once: true }); socket.addEventListener('error', reject, { once: true }); });
  let id = 0;
  const pending = new Map();
  socket.addEventListener('message', (event) => {
    let message;
    try { message = JSON.parse(event.data); } catch { return; }
    if (!message.id) return;
    const waiter = pending.get(message.id);
    if (!waiter) return;
    pending.delete(message.id);
    if (message.error) waiter.reject(new Error(message.error.message));
    else waiter.resolve(message.result || {});
  });
  const send = (method, params = {}, sessionId) => new Promise((resolve, reject) => {
    const requestId = ++id;
    pending.set(requestId, { resolve, reject });
    const request = { id: requestId, method, params };
    if (sessionId) request.sessionId = sessionId;
    socket.send(JSON.stringify(request));
  });
  return {
    child, socket, send,
    async close() {
      try { await send('Browser.close'); } catch {}
      socket.close();
      await Promise.race([waitForExit(child), sleep(2000)]);
      if (child.exitCode === null) child.kill();
    },
  };
}

async function evaluate(send, expression, sessionId) {
  const response = await send('Runtime.evaluate', { expression, awaitPromise: true, returnByValue: true, userGesture: true }, sessionId);
  if (response.exceptionDetails) throw new Error(response.exceptionDetails.exception?.description || response.exceptionDetails.text || 'Browser evaluation failed');
  return response.result?.value;
}

function readSourceWav(bytes) {
  if (bytes.toString('ascii', 0, 4) !== 'RIFF' || bytes.toString('ascii', 8, 12) !== 'WAVE') throw new Error('Source harness emitted a non-WAV file');
  let fmt, data;
  for (let p = 12; p + 8 <= bytes.length;) {
    const tag = bytes.toString('ascii', p, p + 4), size = bytes.readUInt32LE(p + 4), body = p + 8;
    if (body + size > bytes.length) throw new Error('Truncated source WAV chunk');
    if (tag === 'fmt ') fmt = { format: bytes.readUInt16LE(body), channels: bytes.readUInt16LE(body + 2), sampleRate: bytes.readUInt32LE(body + 4), bits: bytes.readUInt16LE(body + 14) };
    if (tag === 'data') data = { offset: body, size };
    p = body + size + (size & 1);
  }
  if (!fmt || !data || fmt.format !== 1 || fmt.bits !== 16 || !fmt.channels || !fmt.sampleRate || data.size % (fmt.channels * 2)) throw new Error('Expected PCM16 source render');
  return { fmt, data, frames: data.size / (fmt.channels * 2) };
}

function packPcm16(source, frames) {
  const { fmt, data } = readSourceWav(source);
  const dataBytes = frames * fmt.channels * 2;
  const out = Buffer.alloc(44 + dataBytes);
  out.write('RIFF', 0); out.writeUInt32LE(36 + dataBytes, 4); out.write('WAVE', 8);
  out.write('fmt ', 12); out.writeUInt32LE(16, 16); out.writeUInt16LE(1, 20);
  out.writeUInt16LE(fmt.channels, 22); out.writeUInt32LE(fmt.sampleRate, 24);
  out.writeUInt32LE(fmt.sampleRate * fmt.channels * 2, 28); out.writeUInt16LE(fmt.channels * 2, 32); out.writeUInt16LE(16, 34);
  out.write('data', 36); out.writeUInt32LE(dataBytes, 40);
  source.copy(out, 44, data.offset, data.offset + dataBytes);
  return out;
}

function comparePcm16(a, b) {
  const aa = readSourceWav(a), bb = readSourceWav(b);
  if (aa.frames !== bb.frames || aa.fmt.channels !== bb.fmt.channels || aa.fmt.sampleRate !== bb.fmt.sampleRate || aa.data.size !== bb.data.size)
    return { equivalent: false, maxDelta: Infinity, changed: 0, nrmseFullScale: Infinity, correlation: -1 };
  let changed = 0, maxDelta = 0, sumSquaredDelta = 0, sumA = 0, sumB = 0, sumAA = 0, sumBB = 0, sumAB = 0;
  for (let i = 0; i < aa.data.size; i += 2) {
    const x = a.readInt16LE(aa.data.offset + i), y = b.readInt16LE(bb.data.offset + i), delta = x - y;
    if (delta) changed++;
    maxDelta = Math.max(maxDelta, Math.abs(delta));
    sumSquaredDelta += delta * delta;
    sumA += x; sumB += y; sumAA += x * x; sumBB += y * y; sumAB += x * y;
  }
  const count = aa.data.size / 2;
  const covariance = sumAB - sumA * sumB / count;
  const varianceA = sumAA - sumA * sumA / count, varianceB = sumBB - sumB * sumB / count;
  const correlation = varianceA > 0 && varianceB > 0 ? covariance / Math.sqrt(varianceA * varianceB) : (varianceA === varianceB ? 1 : -1);
  return { equivalent: maxDelta <= 8, maxDelta, changed,
    nrmseFullScale: Math.sqrt(sumSquaredDelta / count) / 32768, correlation };
}

function compareMusicPcm16(a, b,track='') {
  const result = comparePcm16(a, b);
  const limits=track==='menu'?menuComparisonLimits:musicComparisonLimits;
  result.equivalent = result.maxDelta <= limits.maxPcm16Lsb &&
    result.nrmseFullScale <= limits.nrmseFullScale &&
    result.correlation >= limits.minCorrelation;
  return result;
}

function musicComparisonSummary(result) {
  return `${result.changed} changed samples, max ${result.maxDelta} PCM16 LSB, ` +
    `full-scale NRMSE ${result.nrmseFullScale.toPrecision(4)}, correlation ${result.correlation.toPrecision(12)}`;
}

function manifestComparisonText(text) {
  const manifest = typeof text === 'string' ? JSON.parse(text) : structuredClone(text);
  for (const clip of manifest.clips ?? []) {
    if (!clip.loop) continue;
    // Edge's compressor can shift these loop measurements slightly between equivalent renders.
    delete clip.loop.sourceActiveSeconds;
    delete clip.loop.sourcePeak;
    delete clip.loop.sourceRms;
  }
  return JSON.stringify(manifest, null, 2) + '\n';
}

function pcm16Wav(samples) {
  const bytes = Buffer.alloc(44 + samples.length * 2);
  bytes.write('RIFF', 0); bytes.writeUInt32LE(36 + samples.length * 2, 4); bytes.write('WAVE', 8);
  bytes.write('fmt ', 12); bytes.writeUInt32LE(16, 16); bytes.writeUInt16LE(1, 20); bytes.writeUInt16LE(2, 22);
  bytes.writeUInt32LE(48000, 24); bytes.writeUInt32LE(192000, 28); bytes.writeUInt16LE(4, 32); bytes.writeUInt16LE(16, 34);
  bytes.write('data', 36); bytes.writeUInt32LE(samples.length * 2, 40);
  for (let i = 0; i < samples.length; i++) bytes.writeInt16LE(samples[i], 44 + i * 2);
  return bytes;
}

if (comparisonSelfTest) {
  const reference = [], shifted = [];
  for (let frame = 0; frame < 2048; frame++) {
    reference.push(Math.round(Math.sin(frame * 0.11) * 15000), Math.round(Math.sin(frame * 0.11 + 0.7) * 12000));
  }
  for (let frame = 0; frame < 2048; frame++) {
    const previous = (frame + 2047) % 2048;
    shifted.push(reference[previous * 2], reference[previous * 2 + 1]);
  }
  const stable = compareMusicPcm16(pcm16Wav(reference), pcm16Wav(reference));
  const negativeControl = compareMusicPcm16(pcm16Wav(reference), pcm16Wav(shifted));
  if (!stable.equivalent || negativeControl.equivalent ||compareMusicPcm16(pcm16Wav(reference),pcm16Wav(shifted),'menu').equivalent)
    throw new Error(`Music comparison control failed: stable=${JSON.stringify(stable)}, shifted=${JSON.stringify(negativeControl)}`);
  const baseManifest = { schema: 3, clips: [{ file: 'loop.wav', sha256: 'kept', frames: 100,
    loop: { file: 'loop-bed.wav', sha256: 'loop-kept', frames: 90, sourceActiveSeconds: 3.1, sourcePeak: 0.4, sourceRms: 0.08 } }] };
  const measuredLoop = structuredClone(baseManifest);
  Object.assign(measuredLoop.clips[0].loop, { sourceActiveSeconds: 3.099, sourcePeak: 0.401, sourceRms: 0.081 });
  const changedHash = structuredClone(baseManifest); changedHash.clips[0].loop.sha256 = 'changed';
  const changedFrames = structuredClone(baseManifest); changedFrames.clips[0].loop.frames++;
  if (manifestComparisonText(baseManifest) !== manifestComparisonText(measuredLoop) ||
      manifestComparisonText(baseManifest) === manifestComparisonText(changedHash) ||
      manifestComparisonText(baseManifest) === manifestComparisonText(changedFrames))
    throw new Error('Manifest comparison control failed: it must ignore only loop measurement fields');
  console.log(`Music comparison controls passed; one-frame shift rejected (${musicComparisonSummary(negativeControl)}). Manifest control passed; loop hashes and frame counts remain exact.`);
  process.exit(0);
}

async function listFiles(directory, prefix = '') {
  const files = [];
  for (const entry of await readdir(directory, { withFileTypes: true })) {
    const relative = path.posix.join(prefix, entry.name);
    if (entry.isDirectory()) files.push(...await listFiles(path.join(directory, entry.name), relative));
    else if (entry.isFile()) files.push(relative);
  }
  return files.sort();
}

function run(command, args) {
  return new Promise((resolve, reject) => {
    const child = spawn(command, args, { windowsHide: true, stdio: ['ignore', 'ignore', 'pipe'] });
    let stderr = '';
    child.stderr.setEncoding('utf8'); child.stderr.on('data', (part) => { stderr += part; });
    child.once('error', reject);
    child.once('close', (code) => code === 0 ? resolve() : reject(new Error(`${command} failed (${code}): ${stderr}`)));
  });
}

async function decodedFlac(flac, target) {
  await run(process.env.FFMPEG_PATH || 'ffmpeg', ['-y', '-v', 'error', '-i', flac, '-f', 'wav', '-c:a', 'pcm_s16le', target]);
  return readFile(target);
}

async function encodeFlac(wav, flac) {
  await run(process.env.FFMPEG_PATH || 'ffmpeg', ['-y', '-v', 'error', '-i', wav, '-compression_level', '8', '-c:a', 'flac', '-sample_fmt', 's16', flac]);
}

const temp = await mkdtemp(path.join(root, 'build', 'audio-source-'));
let browser;
try {
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(0, '127.0.0.1', resolve);
  });
  const address = server.address();
  const url = `http://127.0.0.1:${address.port}/tools/audio-lab.html`;
  browser = await launchEdge(path.join(temp, 'edge-profile'));
  const target = await browser.send('Target.createTarget', { url });
  const attached = await browser.send('Target.attachToTarget', { targetId: target.targetId, flatten: true });
  const sessionId = attached.sessionId;
  await browser.send('Page.enable', {}, sessionId);
  await browser.send('Runtime.enable', {}, sessionId);
  await browser.send('Page.navigate', { url }, sessionId);
  let pageReady = false;
  for (let i = 0; i < 100; i++) {
    try { pageReady = await evaluate(browser.send, 'document.readyState === "complete"', sessionId); } catch {}
    if (pageReady) break;
    await sleep(100);
  }
  if (!pageReady) throw new Error('Audio source page did not finish loading');

  selected = await evaluate(browser.send, `
    (async () => {
      const { SFX } = await import('/src/audio/audio.js');
      await import('/src/game/kits/index.js');
      return Object.entries(SFX).sort(([a], [b]) => a.localeCompare(b)).map(([name, def]) => ({
        name, maxVoices: def.max ?? 6, pitchJitter: def.jitter ?? 0.06,
        minGapSeconds: def.minGap ?? 0.018, loopable: !!def.loop,
      }));
    })()
  `, sessionId);
  if (!Array.isArray(selected) || selected.length < 200 || new Set(selected.map((entry) => entry.name)).size !== selected.length)
    throw new Error(`Expected the complete source SFX registry, got ${selected?.length ?? 'no list'} names`);

  const entries = [];
  for (const sourceMeta of selected) {
    const { name } = sourceMeta;
    const rendered = await evaluate(browser.send, `
      (async () => {
        const { AudioEngine, SFX } = await import('/src/audio/audio.js');
        const sampleRate = 48000, seconds = 5.3;
        const context = new OfflineAudioContext(2, Math.floor(sampleRate * seconds), sampleRate);
        const engine = new AudioEngine({ context, seed: 1234, music: false });
        engine.init(); engine.setVolumes({ master: 1, music: 1, sfx: 1 });
        engine.master.gain.value = 1; engine.sfxBus.gain.value = 1; engine.musicBus.gain.value = 1;
        if (!SFX[${JSON.stringify(name)}]) throw new Error('Missing source SFX ' + ${JSON.stringify(name)});
        engine.play(${JSON.stringify(name)}, { at: 0.3 });
        const buffer = await context.startRendering();
        const left = buffer.getChannelData(0), right = buffer.getChannelData(1);
        let peak = 0, sum = 0, activeStart = -1, activeEnd = -1, nan = 0, clipped = 0;
        for (let i = 0; i < buffer.length; i++) {
          const l = left[i], r = right[i];
          if (!Number.isFinite(l) || !Number.isFinite(r)) { nan++; continue; }
          const a = Math.max(Math.abs(l), Math.abs(r)); peak = Math.max(peak, a);
          if (a > 0.99) clipped++;
          if (a > 0.001) { if (activeStart < 0) activeStart = i; activeEnd = i; }
        }
        if (activeStart >= 0) for (let i = activeStart; i <= activeEnd; i++) sum += (left[i] * left[i] + right[i] * right[i]) * 0.5;
        const frames = buffer.length, bytes = new Uint8Array(44 + frames * 4), dv = new DataView(bytes.buffer);
        const text = (offset, value) => { for (let i = 0; i < value.length; i++) dv.setUint8(offset + i, value.charCodeAt(i)); };
        text(0, 'RIFF'); dv.setUint32(4, 36 + frames * 4, true); text(8, 'WAVE'); text(12, 'fmt ');
        dv.setUint32(16, 16, true); dv.setUint16(20, 1, true); dv.setUint16(22, 2, true);
        dv.setUint32(24, sampleRate, true); dv.setUint32(28, sampleRate * 4, true); dv.setUint16(32, 4, true); dv.setUint16(34, 16, true);
        text(36, 'data'); dv.setUint32(40, frames * 4, true);
        let o = 44;
        for (let i = 0; i < frames; i++) for (const channel of [left, right]) {
          dv.setInt16(o, Math.max(-1, Math.min(1, channel[i])) * 32767, true); o += 2;
        }
        let binary = ''; for (let i = 0; i < bytes.length; i += 0x8000) binary += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
        return { wav: btoa(binary), sampleRate, channels: 2, bitsPerSample: 16, frames,
          activeSeconds: activeEnd > 0 ? activeEnd / sampleRate : 0, peak, rms: Math.sqrt(sum / Math.max(1, activeEnd - activeStart + 1)), nan, clipped };
      })()
    `, sessionId);
    if (!rendered?.wav) throw new Error(`Source render returned no PCM for ${name}`);
    if (rendered.nan || rendered.clipped || rendered.peak < 0.01 || rendered.activeSeconds <= 0) throw new Error(`Invalid source render ${name}: ${JSON.stringify({ peak: rendered.peak, activeSeconds: rendered.activeSeconds, nan: rendered.nan, clipped: rendered.clipped })}`);
    const original = Buffer.from(rendered.wav, 'base64');
    const parsed = readSourceWav(original);
    const activeEnd = Math.max(1, Math.min(parsed.frames, Math.ceil((rendered.activeSeconds + 0.12) * parsed.fmt.sampleRate)));
    let wav = packPcm16(original, activeEnd);
    const file = `${name}.wav`;
    let sha256;
    const assetPath = path.join(outputRoot, file);
    if (checking || existsSync(assetPath)) {
      let existing;
      try { existing = await readFile(assetPath); } catch (error) { if (checking) throw error; }
      if (existing) {
        const comparison = comparePcm16(existing, wav);
        if (checking && !comparison.equivalent) throw new Error(`Stale source audio asset: ${file} (${comparison.changed} changed samples, max ${comparison.maxDelta} PCM16 LSB)`);
        if (comparison.equivalent) wav = existing;
        else if (checking) throw new Error(`Stale source audio asset: ${file}`);
      }
    }
    // Measure the accepted PCM so harmless browser mixer variance cannot change the manifest.
    const accepted = readSourceWav(wav);
    let peak = 0, sum = 0, first = -1, last = -1;
    for (let frame = 0; frame < accepted.frames; frame++) {
      let amplitude = 0;
      for (let channel = 0; channel < accepted.fmt.channels; channel++) {
        const sample = wav.readInt16LE(accepted.data.offset + (frame * accepted.fmt.channels + channel) * 2) / 32767;
        amplitude = Math.max(amplitude, Math.abs(sample));
      }
      peak = Math.max(peak, amplitude);
      if (amplitude > 0.001) { if (first < 0) first = frame; last = frame; }
    }
    for (let frame = Math.max(0, first); frame <= last; frame++)
      for (let channel = 0; channel < accepted.fmt.channels; channel++) {
        const sample = wav.readInt16LE(accepted.data.offset + (frame * accepted.fmt.channels + channel) * 2) / 32767;
        sum += sample * sample / accepted.fmt.channels;
      }
    sha256 = createHash('sha256').update(wav).digest('hex');
    entries.push({
      name, file, sourceName: name, sampleRate: parsed.fmt.sampleRate, channels: parsed.fmt.channels,
      bitsPerSample: parsed.fmt.bits, frames: activeEnd, durationSeconds: activeEnd / parsed.fmt.sampleRate,
      sourceActiveSeconds: Math.max(0, last) / accepted.fmt.sampleRate, sourcePeak: +peak.toFixed(3), sourceRms: +Math.sqrt(sum / Math.max(1, last - first + 1)).toFixed(3),
      sourceNaNCount: rendered.nan, sourceClippedSamples: rendered.clipped, sha256,
      bytes: wav.length,
      sourceMaxVoices: sourceMeta.maxVoices, sourcePitchJitter: sourceMeta.pitchJitter,
      sourceMinGapSeconds: sourceMeta.minGapSeconds, loopable: sourceMeta.loopable,
    });
    if(checking&&entries.length%25===0)console.log(`Source SFX checked: ${entries.length}/${selected.length}`);
    if (checking) {
    } else {
      await mkdir(outputRoot, { recursive: true });
      if (!existsSync(assetPath) || !(await readFile(assetPath)).equals(wav)) await writeFile(assetPath, wav);
    }

    const loop = await evaluate(browser.send, `
      (async () => {
        const { AudioEngine, SFX } = await import('/src/audio/audio.js');
        if (!SFX[${JSON.stringify(name)}]?.loop) return null;
        const sampleRate = 48000, seconds = 3.2;
        const context = new OfflineAudioContext(2, Math.floor(sampleRate * seconds), sampleRate);
        const engine = new AudioEngine({ context, seed: 1234, music: false });
        engine.init(); engine.setVolumes({ master: 1, music: 1, sfx: 1 });
        engine.master.gain.value = 1; engine.sfxBus.gain.value = 1; engine.musicBus.gain.value = 1;
        engine.loop(${JSON.stringify(name)});
        const buffer = await context.startRendering();
        const left = buffer.getChannelData(0), right = buffer.getChannelData(1);
        let peak = 0, sum = 0, activeStart = -1, activeEnd = -1, nan = 0, clipped = 0;
        for (let i = 0; i < buffer.length; i++) {
          const l = left[i], r = right[i];
          if (!Number.isFinite(l) || !Number.isFinite(r)) { nan++; continue; }
          const a = Math.max(Math.abs(l), Math.abs(r)); peak = Math.max(peak, a);
          if (a > 0.99) clipped++;
          if (a > 0.001) { if (activeStart < 0) activeStart = i; activeEnd = i; }
        }
        if (activeStart >= 0) for (let i = activeStart; i <= activeEnd; i++) sum += (left[i] * left[i] + right[i] * right[i]) * 0.5;
        const frames = buffer.length, bytes = new Uint8Array(44 + frames * 4), dv = new DataView(bytes.buffer);
        const text = (offset, value) => { for (let i = 0; i < value.length; i++) dv.setUint8(offset + i, value.charCodeAt(i)); };
        text(0, 'RIFF'); dv.setUint32(4, 36 + frames * 4, true); text(8, 'WAVE'); text(12, 'fmt ');
        dv.setUint32(16, 16, true); dv.setUint16(20, 1, true); dv.setUint16(22, 2, true);
        dv.setUint32(24, sampleRate, true); dv.setUint32(28, sampleRate * 4, true); dv.setUint16(32, 4, true); dv.setUint16(34, 16, true);
        text(36, 'data'); dv.setUint32(40, frames * 4, true);
        let o = 44;
        for (let i = 0; i < frames; i++) for (const channel of [left, right]) {
          dv.setInt16(o, Math.max(-1, Math.min(1, channel[i])) * 32767, true); o += 2;
        }
        let binary = ''; for (let i = 0; i < bytes.length; i += 0x8000) binary += String.fromCharCode(...bytes.subarray(i, i + 0x8000));
        return { wav: btoa(binary), sampleRate, channels: 2, bitsPerSample: 16, frames,
          activeSeconds: activeEnd > 0 ? activeEnd / sampleRate : 0, peak,
          rms: Math.sqrt(sum / Math.max(1, activeEnd - activeStart + 1)), nan, clipped };
      })()
    `, sessionId);
    if (loop) {
      if (loop.nan || loop.clipped || loop.peak < 0.01 || loop.activeSeconds <= 0.2)
        throw new Error(`Invalid source loop render ${name}: ${JSON.stringify({ peak: loop.peak, activeSeconds: loop.activeSeconds, nan: loop.nan, clipped: loop.clipped })}`);
      const raw = Buffer.from(loop.wav, 'base64'), parsedLoop = readSourceWav(raw);
      const fadeFrames = Math.ceil(0.08 * parsedLoop.fmt.sampleRate), stableStart = Math.ceil(0.07 * parsedLoop.fmt.sampleRate);
      const loopFrames = parsedLoop.frames - stableStart;
      const loopWav = packPcm16(raw, parsedLoop.frames);
      const lp = readSourceWav(loopWav);
      // Source loops fade in only when opened; trim that opening ramp and blend their bounded seam.
      for (let frame = 0; frame < fadeFrames; frame++) {
        const t = frame / Math.max(1, fadeFrames - 1), tailFrame = stableStart + loopFrames - fadeFrames + frame;
        const headFrame = stableStart + frame;
        for (let channel = 0; channel < 2; channel++) {
          const headOffset = lp.data.offset + (headFrame * 2 + channel) * 2;
          const tailOffset = lp.data.offset + (tailFrame * 2 + channel) * 2;
          const headSample = loopWav.readInt16LE(headOffset), tailSample = loopWav.readInt16LE(tailOffset);
          loopWav.writeInt16LE(Math.round(tailSample * (1 - t) + headSample * t), tailOffset);
        }
      }
      // Rebuild the header after the loop-in ramp is removed.
      let loopAsset = Buffer.alloc(44 + loopFrames * parsedLoop.fmt.channels * 2);
      loopAsset.write('RIFF', 0); loopAsset.writeUInt32LE(36 + loopAsset.length - 44, 4); loopAsset.write('WAVE', 8);
      loopAsset.write('fmt ', 12); loopAsset.writeUInt32LE(16, 16); loopAsset.writeUInt16LE(1, 20);
      loopAsset.writeUInt16LE(2, 22); loopAsset.writeUInt32LE(parsedLoop.fmt.sampleRate, 24);
      loopAsset.writeUInt32LE(parsedLoop.fmt.sampleRate * 4, 28); loopAsset.writeUInt16LE(4, 32); loopAsset.writeUInt16LE(16, 34);
      loopAsset.write('data', 36); loopAsset.writeUInt32LE(loopAsset.length - 44, 40);
      const srcStart = parsedLoop.data.offset + stableStart * parsedLoop.fmt.channels * 2;
      loopWav.copy(loopAsset, 44, srcStart, srcStart + loopAsset.length - 44);
      const loopFile = `loops/${name}.wav`, loopPath = path.join(outputRoot, 'loops', `${name}.wav`);
      await mkdir(path.dirname(loopPath), { recursive: true });
      if (checking || existsSync(loopPath)) {
        let existing;
        try { existing = await readFile(loopPath); } catch (error) { if (checking) throw error; }
        if (existing) {
          const comparison = comparePcm16(existing, loopAsset);
          if (checking && !comparison.equivalent) throw new Error(`Stale source loop asset: ${loopFile} (${comparison.changed} changed samples, max ${comparison.maxDelta} PCM16 LSB)`);
          if (comparison.equivalent) loopAsset = existing;
        }
      }
      if (!checking && (!existsSync(loopPath) || !(await readFile(loopPath)).equals(loopAsset))) await writeFile(loopPath, loopAsset);
      entries.at(-1).loop = {
        file: loopFile, sampleRate: parsedLoop.fmt.sampleRate, channels: parsedLoop.fmt.channels,
        bitsPerSample: parsedLoop.fmt.bits, frames: loopFrames, durationSeconds: loopFrames / parsedLoop.fmt.sampleRate,
        sourceActiveSeconds: loop.activeSeconds, sourcePeak: +loop.peak.toFixed(3), sourceRms: +loop.rms.toFixed(3),
        sourceNaNCount: loop.nan, sourceClippedSamples: loop.clipped,
        sha256: createHash('sha256').update(loopAsset).digest('hex'), bytes: loopAsset.length,
      };
    }
  }

  const tracks = await evaluate(browser.send, "import('/src/audio/music.js').then(({ SONGS }) => Object.keys(SONGS).sort())", sessionId);
  if (!Array.isArray(tracks) || tracks.length !== 9 || new Set(tracks).size !== tracks.length)
    throw new Error(`Expected all nine procedural source tracks, got ${tracks?.length ?? 'no list'}`);
  const musicEntries = [];
  for (const track of tracks) {
    const rendered = await evaluate(browser.send, `
      (async () => {
        const { AudioEngine } = await import('/src/audio/audio.js');
        const { MusicEngine, getSong } = await import('/src/audio/music.js');
        const sampleRate = 48000, music = new MusicEngine(), song = getSong(${JSON.stringify(track)});
        const loopBegin = Math.round((0.06 + song.loopFrom * 240 / song.bpm) * sampleRate);
        const loopEnd = Math.round((0.06 + song.bars.length * 240 / song.bpm) * sampleRate);
        const frames = loopEnd + sampleRate;
        const context = new OfflineAudioContext(2, frames, sampleRate);
        const engine = new AudioEngine({ context, seed: 1234 }); engine.music = music; engine.init();
        engine.setVolumes({ master: 1, music: 1, sfx: 1 });
        music.play(${JSON.stringify(track)}, { fade: 0, seed: 1234 });
        for (let t = 0; t < frames / sampleRate; t += 0.05) music.advance(t);
        const buffer = await context.startRendering(), left = buffer.getChannelData(0), right = buffer.getChannelData(1);
        let peak = 0, nan = 0, clipped = 0;
        for (let i = 0; i < frames; i++) {
          const l = left[i], r = right[i];
          if (!Number.isFinite(l) || !Number.isFinite(r)) { nan++; continue; }
          peak = Math.max(peak, Math.abs(l), Math.abs(r)); if (Math.max(Math.abs(l), Math.abs(r)) > 0.99) clipped++;
        }
        const bytes = new Uint8Array(44 + frames * 4), dv = new DataView(bytes.buffer);
        const text = (offset, value) => { for (let i = 0; i < value.length; i++) dv.setUint8(offset + i, value.charCodeAt(i)); };
        text(0, 'RIFF'); dv.setUint32(4, 36 + frames * 4, true); text(8, 'WAVE'); text(12, 'fmt ');
        dv.setUint32(16, 16, true); dv.setUint16(20, 1, true); dv.setUint16(22, 2, true);
        dv.setUint32(24, sampleRate, true); dv.setUint32(28, sampleRate * 4, true); dv.setUint16(32, 4, true); dv.setUint16(34, 16, true);
        text(36, 'data'); dv.setUint32(40, frames * 4, true);
        let o = 44;
        for (let i = 0; i < frames; i++) for (const channel of [left, right]) { dv.setInt16(o, Math.max(-1, Math.min(1, channel[i])) * 32767, true); o += 2; }
        const response = await fetch('/__audio-render/music-${track}.wav', { method: 'POST', body: bytes });
        if (!response.ok) throw new Error('Could not transfer offline music render');
        return { sampleRate, channels: 2, frames, loopBegin, loopEnd, bpm: song.bpm, peak, nan, clipped };
      })()
    `, sessionId);
    if (!rendered || rendered.nan || rendered.clipped || rendered.peak < 0.01 || rendered.loopBegin <= 0 || rendered.loopEnd <= rendered.loopBegin || rendered.loopEnd > rendered.frames)
      throw new Error(`Invalid source music render ${track}: ${JSON.stringify(rendered)}`);
    const wavPath = path.join(temp, `music-${track}.wav`), sourceWav = await readFile(wavPath);
    const parsed = readSourceWav(sourceWav);
    if (parsed.frames !== rendered.frames || parsed.fmt.sampleRate !== 48000 || parsed.fmt.channels !== 2)
      throw new Error(`Unexpected source music PCM format ${track}`);
    const relative = `music/${track}.flac`, flacPath = path.join(outputRoot, 'music', `${track}.flac`);
    const decodedPath = path.join(temp, `music-${track}-decoded.wav`);
    let flac;
    if (checking || existsSync(flacPath)) {
      try {
        const decoded = await decodedFlac(flacPath, decodedPath), comparison = compareMusicPcm16(decoded, sourceWav,track);
        if (!comparison.equivalent) {
          if (checking) throw new Error(`Stale source music asset: ${relative} (${musicComparisonSummary(comparison)})`);
        } else flac = await readFile(flacPath);
      } catch (error) { if (checking || error.message.startsWith('Stale source music asset:')) throw error; }
    }
    if (!flac) {
      if (checking) throw new Error(`Missing source music asset: ${relative}`);
      const freshPath = path.join(temp, `music-${track}-fresh.flac`);
      await encodeFlac(wavPath, freshPath); flac = await readFile(freshPath);
      const decoded = await decodedFlac(freshPath, decodedPath), comparison = comparePcm16(decoded, sourceWav);
      if (!comparison.equivalent) throw new Error(`FLAC changed source music PCM ${track} (${comparison.changed} samples, max ${comparison.maxDelta} LSB)`);
      await mkdir(path.dirname(flacPath), { recursive: true }); await writeFile(flacPath, flac);
    }
    const decodedForManifest = await decodedFlac(flacPath, decodedPath);
    const decodedPcm = readSourceWav(decodedForManifest);
    if (decodedPcm.fmt.sampleRate !== rendered.sampleRate || decodedPcm.fmt.channels !== rendered.channels || decodedPcm.frames !== rendered.frames)
      throw new Error(`Unexpected decoded source music format ${track}`);
    if (checking) console.log(`Source music ${track}: ${musicComparisonSummary(compareMusicPcm16(decodedForManifest, sourceWav,track))}`);
    const decodedPcmSha256 = createHash('sha256').update(decodedForManifest.subarray(
      decodedPcm.data.offset, decodedPcm.data.offset + decodedPcm.data.size)).digest('hex');
    musicEntries.push({ name: track, file: relative, sampleRate: 48000, channels: 2, bitsPerSample: 16,
      frames: rendered.frames, loopBeginFrame: rendered.loopBegin, loopEndFrame: rendered.loopEnd,
      loopSeconds: +(rendered.loopBegin / 48000).toFixed(6), durationSeconds: +(rendered.frames / 48000).toFixed(3),
      bpm: rendered.bpm, sha256: createHash('sha256').update(flac).digest('hex'), decodedPcmSha256, bytes: flac.length });
  }

  const catalogText = selected.map(({ name, maxVoices, pitchJitter, minGapSeconds, loopable }) =>
    `${name}\t${maxVoices}\t${pitchJitter}\t${minGapSeconds}\t${loopable ? 1 : 0}`).join('\n') + '\n';
  const musicIndexText = musicEntries.map((entry) =>
    `${entry.name}\t${entry.frames}\t${entry.loopBeginFrame}\t${entry.loopEndFrame}\t${entry.bpm}`).join('\n') + '\n';
  const manifest = {
    schema: 3,
    source: 'splat-ref/src/audio/{audio,music}.js via Microsoft Edge OfflineAudioContext',
    render: { sampleRate: 48000, channels: 2, bitsPerSample: 16, seed: 1234, sourceMasterChain: true, trimAfterActiveSignalSeconds: 0.12,
      checkTolerancePcm16Lsb: 8, musicCheck: { maxPcm16Lsb: musicComparisonLimits.maxPcm16Lsb,
        nrmseFullScaleMax: musicComparisonLimits.nrmseFullScale, correlationMin: musicComparisonLimits.minCorrelation,
        menu:{maxPcm16Lsb:menuComparisonLimits.maxPcm16Lsb,nrmseFullScaleMax:menuComparisonLimits.nrmseFullScale,correlationMin:menuComparisonLimits.minCorrelation} }, musicEncoding: 'lossless FLAC' },
    scope: 'All SFX registered by src/audio/audio.js and src/game/kits/index.js, all nine procedural soundtrack tracks, and source loop builders.',
    clips: entries,
    music: musicEntries,
  };
  const manifestText = JSON.stringify(manifest, null, 2) + '\n';
  const manifestPath = path.join(outputRoot, 'manifest.json');
  const catalogPath = path.join(outputRoot, 'catalog.tsv'), musicIndexPath = path.join(outputRoot, 'music.tsv');
  if (checking) {
    const actualManifest = (await readFile(manifestPath, 'utf8')).replace(/\r\n/g, '\n');
    if (manifestComparisonText(actualManifest) !== manifestComparisonText(manifestText)) throw new Error('Stale source audio manifest');
    if ((await readFile(catalogPath, 'utf8')).replace(/\r\n/g, '\n') !== catalogText) throw new Error('Stale source SFX registry');
    if ((await readFile(musicIndexPath, 'utf8')).replace(/\r\n/g, '\n') !== musicIndexText) throw new Error('Stale source music index');
    const expected = new Set(['manifest.json', 'catalog.tsv', 'music.tsv', ...entries.flatMap((entry) => [entry.file, ...(entry.loop ? [entry.loop.file] : [])]), ...musicEntries.map((entry) => entry.file)]);
    const actual = new Set((await listFiles(outputRoot)).filter(file=>file!=='custom-music.tsv'&&!file.startsWith('songs/')));
    if (actual.size !== expected.size || [...expected].some((file) => !actual.has(file))) throw new Error('Unexpected file in assets/audio');
  } else {
    await Promise.all([writeFile(manifestPath, manifestText), writeFile(catalogPath, catalogText), writeFile(musicIndexPath, musicIndexText)]);
    const expected = new Set(['manifest.json', 'catalog.tsv', 'music.tsv', ...entries.flatMap((entry) => [entry.file, ...(entry.loop ? [entry.loop.file] : [])]), ...musicEntries.map((entry) => entry.file)]);
    for (const file of await listFiles(outputRoot)) if (!expected.has(file)&&file!=='custom-music.tsv'&&!file.startsWith('songs/')) throw new Error(`Unexpected file in assets/audio; review it manually: ${file}`);
  }
  console.log(`${checking ? 'Checked' : 'Exported'} ${entries.length} source SFX and ${musicEntries.length} source music tracks (${entries.reduce((sum, entry) => sum + entry.bytes + (entry.loop?.bytes || 0), 0) + musicEntries.reduce((sum, entry) => sum + entry.bytes, 0)} bytes).`);
} finally {
  if (browser) await browser.close();
  await new Promise((resolve) => server.close(resolve));
  const resolvedTemp = path.resolve(temp);
  if (path.dirname(resolvedTemp) !== path.join(root, 'build') || !path.basename(resolvedTemp).startsWith('audio-source-'))
    throw new Error('Refusing to clean an unexpected audio scratch directory');
  await rm(resolvedTemp, { recursive: true, force: true });
}
