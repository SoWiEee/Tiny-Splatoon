// Match setup uses the actual source actors/brains; graphics UUID allocation is excluded from the gameplay RNG.
import { register } from 'node:module';
import { readFileSync, writeFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
register(new URL('./reference-loader.mjs', import.meta.url));
const THREE = await import('three');
const { OFFLINE_MAPS } = await import('../splat-ref/src/config.js');
const { MAP_LAYOUTS } = await import('../splat-ref/src/world/maps.js');
const { layoutFor, inMode } = await import('../splat-ref/src/world/variants.js');
const { dressingFor } = await import('../splat-ref/src/world/dressing.js');
const { PropKit } = await import('../splat-ref/src/world/props.js');
const { Level } = await import('../splat-ref/src/world/level.js');
const { Physics } = await import('../splat-ref/src/game/physics.js');
const { Match } = await import('../splat-ref/src/game/match.js');
const { G, rng } = await import('../splat-ref/src/core/ctx.js');
await import('../splat-ref/src/game/kits/index.js');
const map = OFFLINE_MAPS.find(m => m.id === 'halyard');
const props = new PropKit(null, { quality: 'high', headless: true }), colliders = [];
try {
  for (const item of dressingFor(map.layout || map.id))
    if (inMode(item, 'turf')) colliders.push(...props.add(item.type, item).colliders);
} finally { props.dispose(); }
G.level = new Level(layoutFor(MAP_LAYOUTS[map.layout || map.id], 'turf'), colliders);
G.physics = new Physics(G.level);
G.teamColors = [new THREE.Color('#ff8030'), new THREE.Color('#3080ff')];
G.scene = { add() {}, remove() {} };
class HeadlessCharacter {
  constructor({ style }) {
    this.style = style;
    this.root = { position: new THREE.Vector3(), rotation: { y: 0 }, visible: true };
  }
  setVisible(value) { this.root.visible = value; }
  setSub() {} setWeapon() {} setHurt() {} trigger() {} dispose() {}
}
const style = { hair: 0, skin: 0, outfit: 0, eyes: 0, hat: 0, brows: 0 };
const cases = [];
for (const seed of [1, 7, 2026]) for (const mode of ['turf', 'practice', 'autopilot'])
  cases.push({ seed, mode, weapon: 'shooter', playerName: 'You', style });
cases.push({ seed: 93, mode: 'turf', weapon: 'bow', sub: 'mine', special: 'storm', playerName: 'MintIce',
  style: { hair: 6, skin: 5, outfit: 8, eyes: 2, hat: 3, brows: 1 } });
const previousRandom = Math.random;
const rows = [], lifecycles = [];
try {
  for (const c of cases) {
    Math.random = rng(c.seed);
    const match = new Match({ ...c, practice: c.mode === 'practice', autopilot: c.mode === 'autopilot',
      difficulty: 'normal', CharacterClass: HeadlessCharacter });
    try {
      match.setup(); match.start();
      rows.push({ ...c, phase: match.state, bots: match.actors.filter(a => a.bot).length,
        actors: match.actors.map(a => ({ name: a.name, team: a.team, slot: a.slot, local: a.isLocal, bot: !!a.bot,
          main: a.weaponId, sub: a.subId, special: a.specialId, style: a.character.style,
          pos: a.pos.toArray(), yaw: a.yaw, invuln: a.invuln })),
        nextRandom: [Math.random(), Math.random(), Math.random()] });
    } finally { match.dispose(); }
  }
  // Isolate the source match clock/judge from combat: actors remain registered but inert/dead.
  for (const { seed, paintTeam } of [
    ...[1, 7, 2026, 4].map(seed => ({ seed, paintTeam: -1 })),
    { seed: 1, paintTeam: 0 }, { seed: 1, paintTeam: 1 },
  ]) {
    Math.random = rng(seed);
    const match = new Match({ weapon: 'shooter', difficulty: 'normal', style, CharacterClass: HeadlessCharacter });
    G.paint = { coverage() { return [paintTeam === 0 ? 1 : 0, paintTeam === 1 ? 1 : 0]; } };
    try {
      match.setup(); match.start();
      for (const actor of match.actors) { actor.bot = null; actor.alive = false; actor.update = () => {}; }
      const transitions = [{ frame: 0, phase: match.state, time: match.time }];
      let frame = 0;
      while (match.state !== 'judge' && frame < 3000) {
        ++frame;
        const previous = match.state;
        match.paused = frame >= 100 && frame < 110;
        match.update(0.1);
        if (previous !== match.state || frame === 100 || frame === 109 || frame === 110)
          transitions.push({ frame, phase: match.state, time: match.time });
      }
      if (match.state !== 'judge') throw new Error('Source clock failed to reach judge');
      match.setState('results');
      transitions.push({ frame, phase: match.state, time: match.time });
      lifecycles.push({ seed, paintTeam, transitions, winner: match.result.winner });
    } finally { match.dispose(); }
  }
} finally { Math.random = previousRandom; }
const paths = ['src/config.js', 'src/game/match.js', 'src/game/actor.js', 'src/game/bots.js', 'src/game/character-style.js'];
const sources = paths.map(path => ({ path, sha256: createHash('sha256').update(readFileSync(new URL('../splat-ref/' + path, import.meta.url))).digest('hex') }));
const q = JSON.stringify, vec = a => `{${a.join(',')}}`, sty = s => vec(['hair','skin','outfit','eyes','hat','brows'].map(k => s[k]));
const header = ['// Generated by tools/export-match-setup-fixtures.mjs. Do not edit.', '#pragma once',
  '#include <vector>', '#include <string>', '#include "../src/game/MatchSimulation.hpp"',
  'namespace restoration::test {',
  'struct SetupActor { std::string name,main,sub,special; CharacterStyle style; int team,slot; bool local,bot; glm::dvec3 pos; double yaw,invuln; };',
  'struct SetupCase { unsigned seed; bool practice,autopilot; std::string main,sub,special,name; CharacterStyle style; int bots; std::vector<SetupActor> actors; double nextRandom[3]; };',
  'inline const std::vector<SetupCase> matchSetupFixtures{',
  ...rows.map(r => `{${r.seed},${r.mode === 'practice'},${r.mode === 'autopilot'},${q(r.weapon)},${q(r.sub || '')},${q(r.special || '')},${q(r.playerName)},${sty(r.style)},${r.bots},{\n` +
    r.actors.map(a => ` {${q(a.name)},${q(a.main)},${q(a.sub)},${q(a.special)},${sty(a.style)},${a.team},${a.slot},${a.local},${a.bot},${vec(a.pos)},${a.yaw},${a.invuln}},`).join('\n') + `\n},${vec(r.nextRandom)}},`),
  '};',
  'struct PhaseSample { int frame; MatchPhase phase; double time; };',
  'struct LifecycleCase { unsigned seed; int paintTeam,winner; std::vector<PhaseSample> samples; };',
  'inline const std::vector<LifecycleCase> matchLifecycleFixtures{',
  ...lifecycles.map(r => `{${r.seed},${r.paintTeam},${r.winner},{` + r.transitions.map(s => `{${s.frame},MatchPhase::${s.phase[0].toUpperCase()+s.phase.slice(1)},${s.time}}`).join(',') + '}},'),
  '};', '}', ''].join('\n');
for (const [path, data] of [['../tests/fixtures/match_setup_fixtures.hpp', header], ['../tests/fixtures/match_setup_fixtures.json', JSON.stringify({ sources, rows, lifecycles }, null, 2) + '\n']]) {
  const target = new URL(path, import.meta.url);
  if (process.argv.includes('--check')) {
    if (readFileSync(target, 'utf8') !== data) throw new Error(`Source setup fixture differs: ${path}`);
  } else writeFileSync(target, data);
}
console.log(`Locked ${rows.length} actual-source match setups and ${lifecycles.length} clock/judge traces (graphics UUID RNG excluded)`);
