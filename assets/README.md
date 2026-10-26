# Asset bundles

- `models/` contains characters, Boss, devices, and projectiles. The character `catalog.json` maps records to SHA-256-named mesh blobs; keep each catalog with its blobs.
- `world/` contains stages, stage textures, environment data, lobby data, and Zones materials.
- `effects/` contains post-processing data and World FX particle shaders.
- `shaders/`, `audio/`, `fonts/`, and `ui/` hold shared runtime resources.

Bundle manifests and metadata retain the source hashes used to produce these files. The old `content.json` snapshot from `splat-ref` had no runtime or test consumer and was removed. The old `arsenal.json` snapshot was likewise unused; its source hash (`76a1bc70f855e58343a1bc1d7a41c6b72dcd2da55b3e4c47b5a5908756180999`) remains recorded in generated `src/game/ArsenalData.cpp`, which supplies the active native arsenal data.
