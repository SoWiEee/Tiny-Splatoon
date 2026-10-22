import { mkdir, readFile, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { referenceBrowser } from './reference-browser-session.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const args = process.argv.slice(2);
if (args.length > 1 || (args.length === 1 && args[0] !== '--check'))
  throw new Error('Usage: node tools/export-native-reverb-fixture.mjs [--check]');
const check = args[0] === '--check';
const wetTolerance = 3e-6;
const normalizationTolerance = 1e-8;
const browser = await referenceBrowser(root, 'audio-native-reverb-fixture');

try {
  const exported = await browser.evaluate(`
    (async () => {
      const { makeImpulse } = await import('/src/audio/music.js');
      const sampleRate = 48000;
      const inputFrames = 1024;
      const input = new Float32Array(inputFrames);
      input[0] = 1;
      input[31] = -0.5;
      input[127] = 0.25;
      input[511] = -0.125;
      input[1023] = 0.0625;
      const configs = [
        { name: 'plaza', seconds: 1.5, decay: 3.4, options: { pre: 0.012, seed: 3 }, returnGain: 0.6, normalization: 0.01141840140 },
        { name: 'music', seconds: 2.2, decay: 2.4, options: { seed: 5, bright: 0.7, dark: 0.08 }, returnGain: 0.5, normalization: 0.01169976630 },
      ];
      const encode = (channels) => {
        const frames = channels[0].length;
        const bytes = new Uint8Array(frames * channels.length * 4);
        const view = new DataView(bytes.buffer);
        for (let frame = 0; frame < frames; frame++) {
          for (let channel = 0; channel < channels.length; channel++)
            view.setFloat32((frame * channels.length + channel) * 4, channels[channel][frame], true);
        }
        let binary = '';
        const chunk = 0x8000;
        for (let i = 0; i < bytes.length; i += chunk)
          binary += String.fromCharCode(...bytes.subarray(i, Math.min(bytes.length, i + chunk)));
        return btoa(binary);
      };
      const results = [];
      for (const config of configs) {
        const impulseFrames = Math.floor(sampleRate * config.seconds);
        const outputFrames = impulseFrames + inputFrames - 1;
        const context = new OfflineAudioContext(2, outputFrames, sampleRate);
        const impulse = makeImpulse(context, config.seconds, config.decay, config.options);
        const convolver = context.createConvolver();
        if (convolver.normalize !== true) throw new Error('Expected source ConvolverNode default normalization');
        convolver.buffer = impulse;
        const inputBuffer = context.createBuffer(1, inputFrames, sampleRate);
        inputBuffer.copyToChannel(input, 0);
        const source = context.createBufferSource();
        source.buffer = inputBuffer;
        source.connect(convolver);
        const returned = context.createGain();
        returned.gain.value = config.returnGain;
        convolver.connect(returned);
        returned.connect(context.destination);
        source.start(0);
        const rendered = await context.startRendering();
        const raw = encode([impulse.getChannelData(0), impulse.getChannelData(1)]);
        const wet = encode([rendered.getChannelData(0), rendered.getChannelData(1)]);
        results.push({ config, sampleRate, impulseFrames, inputFrames, outputFrames, raw, wet });
      }
      return results;
    })()
  `);

  if (!Array.isArray(exported) || exported.length !== 2) throw new Error('Source browser did not return both reverb fixtures');
  if (!check) await mkdir(path.join(root, 'tests', 'fixtures'), { recursive: true });
  for (const entry of exported) {
    const { config, sampleRate, impulseFrames, inputFrames, outputFrames } = entry;
    if (sampleRate !== 48000 || inputFrames !== 1024 || outputFrames !== impulseFrames + inputFrames - 1)
      throw new Error(`Unexpected source ${config.name} fixture dimensions`);
    const raw = Buffer.from(entry.raw, 'base64');
    const wet = Buffer.from(entry.wet, 'base64');
    if (raw.length !== impulseFrames * 2 * 4 || wet.length !== outputFrames * 2 * 4)
      throw new Error(`Unexpected source ${config.name} float count`);

    // The first response sample after the two-millisecond ramp is due solely
    // to input[0]. This directly measures the browser convolver's normalization
    // and return-gain scalar without reproducing its normalization algorithm.
    const preFrames = Math.floor((config.options.pre ?? 0.01) * sampleRate);
    let normalization = 0;
    for (let frame = preFrames + 1; frame < Math.min(impulseFrames, preFrames + 128); frame++) {
      const denominator = raw.readFloatLE((frame * 2) * 4);
      const numerator = wet.readFloatLE((frame * 2) * 4);
      if (Math.abs(denominator) > 1e-8) { normalization = numerator / denominator / config.returnGain; break; }
    }
    if (!(normalization > 0) || !Number.isFinite(normalization)) throw new Error(`Unable to measure ${config.name} normalization`);
    if (check && Math.abs(normalization - config.normalization) > normalizationTolerance)
      throw new Error(`${config.name} source normalization changed; run an explicit export to refresh the fixture`);

    const input = Buffer.alloc(inputFrames * 4);
    [[0, 1], [31, -0.5], [127, 0.25], [511, -0.125], [1023, 0.0625]]
      .forEach(([frame, value]) => input.writeFloatLE(value, frame * 4));
    const header = Buffer.alloc(32);
    header.write('TSRVBF02', 0, 'ascii');
    header.writeUInt32LE(sampleRate, 8);
    header.writeUInt32LE(impulseFrames, 12);
    header.writeUInt32LE(2, 16);
    header.writeUInt32LE(inputFrames, 20);
    header.writeUInt32LE(outputFrames, 24);
    header.writeFloatLE(normalization, 28);
    const filename = `audio-${config.name}-reverb-source.bin`;
    const output = path.join(root, 'tests', 'fixtures', filename);
    if (check) {
      const existing = await readFile(output);
      const expectedLength = header.length + raw.length + input.length + wet.length;
      if (existing.length !== expectedLength || existing.toString('ascii', 0, 8) !== 'TSRVBF02' ||
          existing.readUInt32LE(8) !== sampleRate ||
          existing.readUInt32LE(12) !== impulseFrames || existing.readUInt32LE(16) !== 2 ||
          existing.readUInt32LE(20) !== inputFrames || existing.readUInt32LE(24) !== outputFrames ||
          Math.abs(existing.readFloatLE(28) - config.normalization) > normalizationTolerance)
        throw new Error(`${config.name} fixture metadata or known normalization changed`);
      const rawOffset = header.length, inputOffset = rawOffset + raw.length, wetOffset = inputOffset + input.length;
      if (!existing.subarray(rawOffset, inputOffset).equals(raw))
        throw new Error(`${config.name} raw source impulse changed`);
      if (!existing.subarray(inputOffset, wetOffset).equals(input))
        throw new Error(`${config.name} deterministic input changed`);
      for (let offset = 0; offset < wet.length; offset += 4) {
        const actual = existing.readFloatLE(wetOffset + offset), expected = wet.readFloatLE(offset);
        if (!Number.isFinite(actual) || Math.abs(actual - expected) > wetTolerance)
          throw new Error(`${config.name} browser wet render differs at sample ${offset / 4}`);
      }
      process.stdout.write(`Verified ${output}\n`);
    } else {
      await writeFile(output, Buffer.concat([header, raw, input, wet]));
      process.stdout.write(`Wrote ${output}\n`);
    }
    process.stdout.write(`Source ConvolverNode normalization ${normalization.toPrecision(10)}; frames impulse=${impulseFrames}, input=${inputFrames}, rendered=${outputFrames}\n`);
  }
} finally {
  await browser.close();
}
