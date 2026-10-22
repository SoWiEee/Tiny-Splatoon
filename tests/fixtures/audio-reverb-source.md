# Native reverb source fixtures

The fixtures were generated from the unchanged `splat-ref/src/audio/audio.js` and `splat-ref/src/audio/music.js` using headless Edge and `OfflineAudioContext`:

```powershell
node tools/export-native-reverb-fixture.mjs
```

Running the command without flags explicitly regenerates both binary fixtures. Use `node tools/export-native-reverb-fixture.mjs --check` to render and compare the current source without writing fixture files. Check mode requires exact metadata, normalization within `1e-8`, exact raw impulse and input samples, and a maximum per-sample wet-render difference of `3e-6` to allow small offline-render variation.

The plaza fixture uses a 1.5 s impulse, 3.4 decay, 12 ms pre-delay, seed 3, and 0.6 return gain. The music fixture uses a 2.2 s impulse, 2.4 decay, seed 5, bright 0.7, dark 0.08, and 0.5 return gain. The deterministic 1,024-frame input is rendered through a default-normalized stereo `ConvolverNode`; fixture headers record its measured 48 kHz normalization: 0.01141840140 for plaza and 0.01169976630 for music. The music chain's separate 0.25 FX reverb send is applied by the native riser helper.

Each file has a 32-byte little-endian `TSRVBF02` header followed by stereo float32 raw impulse samples, mono float32 input samples, and stereo float32 source wet output. The source inputs and current generated fixtures have these SHA-256 hashes:

| File | SHA-256 |
| --- | --- |
| `splat-ref/src/audio/audio.js` | `402A7DEDC9A7CEA7AEFBDFF540DD62FB02C22A966B71CDEB83A83FE7BDB98E9B` |
| `splat-ref/src/audio/music.js` | `E11A7E1C0F66B30B1B7C3F63DB6098C17A21B106417B1D3832B87D199C9FDDD4` |
| `tests/fixtures/audio-plaza-reverb-source.bin` | `41A0975537A2D26DFCCF36C3D8A3E38AF62852266356CBBA421305C479D3AB3A` |
| `tests/fixtures/audio-music-reverb-source.bin` | `78298AA794852919FAC22773D939275326F9F4323DC4B240A40BF6F907131DE6` |
