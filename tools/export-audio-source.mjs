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
const sourceNames = [
  'ui_hover', 'ui_click', 'ui_back', 'ui_confirm', 'ui_toggle', 'ui_slider', 'ui_error',
  'shoot_shooter', 'hit_marker', 'hurt', 'squid_in', 'squid_out', 'jump', 'land',
  'ready', 'go_horn', 'final_count', 'times_up', 'victory_fanfare', 'defeat_jingle',
];
const checking = process.argv.includes('--check');
if (process.argv.slice(2).some((arg) => arg !== '--check')) throw new Error('Usage: node tools/export-audio-source.mjs [--check]');
const selected = sourceNames;

const mime = new Map([
  ['.html', 'text/html; charset=utf-8'], ['.js', 'text/javascript; charset=utf-8'],
  ['.mjs', 'text/javascript; charset=utf-8'], ['.json', 'application/json'],
  ['.css', 'text/css; charset=utf-8'], ['.svg', 'image/svg+xml'],
]);
const server = createServer((req, res) => {
  let pathname;
  try { pathname = decodeURIComponent(new URL(req.url, 'http://localhost').pathname); }
  catch { res.writeHead(400).end(); return; }
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
  if (aa.frames !== bb.frames || aa.fmt.channels !== bb.fmt.channels || aa.fmt.sampleRate !== bb.fmt.sampleRate || aa.data.size !== bb.data.size) return { equivalent: false, maxDelta: Infinity, changed: 0 };
  let changed = 0, maxDelta = 0;
  for (let i = 0; i < aa.data.size; i += 2) {
    const delta = Math.abs(a.readInt16LE(aa.data.offset + i) - b.readInt16LE(bb.data.offset + i));
    if (delta) changed++;
    maxDelta = Math.max(maxDelta, delta);
  }
  return { equivalent: maxDelta <= 8, maxDelta, changed };
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

  const entries = [];
  for (const name of selected) {
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
    });
    if (checking) {
    } else {
      await mkdir(outputRoot, { recursive: true });
      if (!existsSync(assetPath) || !(await readFile(assetPath)).equals(wav)) await writeFile(assetPath, wav);
    }
  }

  const manifest = {
    schema: 1,
    source: 'splat-ref/src/audio/audio.js AudioEngine via Microsoft Edge OfflineAudioContext',
    render: { sampleRate: 48000, channels: 2, bitsPerSample: 16, seed: 1234, sourceMasterChain: true, trimAfterActiveSignalSeconds: 0.12, checkTolerancePcm16Lsb: 8 },
    scope: 'UI cues and representative match events only; remaining source SFX, music playback and Boss director are not ported.',
    clips: entries,
  };
  const manifestText = JSON.stringify(manifest, null, 2) + '\n';
  const manifestPath = path.join(outputRoot, 'manifest.json');
  if (checking) {
    if ((await readFile(manifestPath, 'utf8')).replace(/\r\n/g, '\n') !== manifestText) throw new Error('Stale source audio manifest');
    const expected = new Set([...entries.map((entry) => entry.file), 'manifest.json']);
    const actual = new Set(await readdir(outputRoot));
    if (actual.size !== expected.size || [...expected].some((file) => !actual.has(file))) throw new Error('Unexpected file in assets/audio');
  } else {
    await writeFile(manifestPath, manifestText);
    const expected = new Set([...entries.map((entry) => entry.file), 'manifest.json']);
    for (const file of await readdir(outputRoot)) if (!expected.has(file)) throw new Error(`Unexpected file in assets/audio; review it manually: ${file}`);
  }
  console.log(`${checking ? 'Checked' : 'Exported'} ${entries.length} source-rendered audio clips (${entries.reduce((sum, entry) => sum + entry.bytes, 0)} bytes).`);
} finally {
  if (browser) await browser.close();
  await new Promise((resolve) => server.close(resolve));
  const resolvedTemp = path.resolve(temp);
  if (path.dirname(resolvedTemp) !== path.join(root, 'build') || !path.basename(resolvedTemp).startsWith('audio-source-'))
    throw new Error('Refusing to clean an unexpected audio scratch directory');
  await rm(resolvedTemp, { recursive: true, force: true });
}
