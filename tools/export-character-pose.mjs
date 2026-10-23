import { register } from 'node:module';
import { readFile, writeFile } from 'node:fs/promises';
import { createHash } from 'node:crypto';
import { resolve } from 'node:path';
import { pathToFileURL } from 'node:url';

const root = resolve(import.meta.dirname, '..');
register(new URL('./reference-loader.mjs', import.meta.url));

// Register the actual kit modules before constructing the reference Character.
await import('../splat-ref/src/game/kits/index.js');
const THREE = await import('three');
const { Character } = await import('../splat-ref/src/game/character.js');
const { G } = await import('../splat-ref/src/core/ctx.js');
const { WEAPON_ORDER } = await import('../splat-ref/src/config.js');
const { BONE_NAMES, BONE_PARENT, getRestPositions, getBoneInverses } = await import('../splat-ref/src/game/character-geo.js');

const sourcePaths = [
  'splat-ref/src/game/character.js', 'splat-ref/src/game/character-geo.js',
  'splat-ref/src/game/character-mats.js', 'splat-ref/src/game/character-face.js',
  'splat-ref/src/game/character-weapons.js', 'splat-ref/src/game/character-style.js',
  'splat-ref/src/game/character-lod.js', 'splat-ref/src/config.js',
];

async function sourceHashes() {
  const baseline = await readFile(resolve(root, 'docs/reference-baseline.sha256'), 'utf8');
  const hashes = {};
  for (const path of sourcePaths) {
    const hash = createHash('sha256').update(await readFile(resolve(root, path))).digest('hex');
    const entry = baseline.split(/\r?\n/).find(line => line.endsWith(`  ${path}`));
    if (!entry || entry.slice(0, 64).toLowerCase() !== hash) throw new Error(`Reference baseline mismatch: ${path}`);
    hashes[path] = hash;
  }
  return hashes;
}

function value(v) {
  if (typeof v === 'number' || typeof v === 'boolean' || typeof v === 'string') return v;
  if (v == null) return null;
  if (typeof v.toArray === 'function') return v.toArray();
  if (ArrayBuffer.isView(v) || Array.isArray(v)) return Array.from(v, value);
  return Object.fromEntries(Object.entries(v).map(([k, x]) => [k, value(x)]));
}

function transform(node) {
  return { position: node.position.toArray(), quaternion: node.quaternion.toArray(), scale: node.scale.toArray(), visible: node.visible,
    ...(node.matrixAutoUpdate ? {} : { authoredMatrix: node.matrix.toArray() }) };
}

const tracked = ['t', 'yaw', 'prevYaw', 'yawRate', 'kvx', 'kvz', 'kax', 'kaz', 'hs', 'gv', 'gs', 'gvx', 'gvz',
  'tread', 'tvx', 'tvz', 'kgx', 'kgz', 'mdx', 'mdz', 'vyS', 'gnd', 'airT', 'grounded', 'phase', 'moving', 'gaitW',
  'runW', 'duty', 'cad', 'liftH', 'wSub', 'wAim', 'wRoll', 'wAir', 'wDance', 'wTwo', 'wGlow', 'wLow', 'wTired', 'wGoo',
  'charge', 'fullT', 'inkS', 'kidScale', 'sqScale', 'kidPop', 'formT', 'form', 'lastShot', 'lastRelease', 'rv', 'ra', 'rp'];

function capture(c, frame) {
  c.root.updateMatrixWorld(true);
  c.skeleton.update();
  const invKid = c.kid.matrixWorld.clone().invert();
  const parts = {};
  function partTree(node, path, descend = true) {
    if (!node) return;
    parts[path] = transform(node);
    if (node.material) {
      const m = node.material;
      parts[path].material = { color: m.color?.toArray(), emissive: m.emissive?.toArray(), emissiveIntensity: m.emissiveIntensity,opacity:m.opacity,transparent:m.transparent,
        uniforms:Object.fromEntries(Object.entries(m.userData?.u||m.uniforms||{}).map(([name,u])=>[name,value(u.value)])) };
    }
    if (descend) node.children.forEach((child, i) => partTree(child, `${path}/${i}`));
  }
  if (c.weapon) {
    for (const [side, weapon] of [['right', c.weapon], ['left', c.weapon.left]]) {
      if (!weapon) continue;
      for (const key of ['pivot', 'off', 'body', 'ink', 'bodyFar', 'inkFar', 'glow', 'drum', 'spin', 'muzzle'])
        partTree(weapon[key], `weapon/${side}/${key}`, key === 'off');
      for (const [key, node] of Object.entries(weapon.parts)) partTree(node, `weapon/${side}/parts/${key}`);
    }
    if (c.weapon.pivotL) partTree(c.weapon.pivotL, 'weapon/mirror');
  }
  partTree(c.tank.group, 'tank/group', false);
  partTree(c.tank.fill, 'tank/fill');
  if (c.bomb?.group) partTree(c.bomb.group, 'sub/group');
  partTree(c.squidRoot, 'squid');
  const muzzle = new THREE.Vector3(), muzzleLeft = new THREE.Vector3(), head = new THREE.Vector3();
  c.getMuzzle(muzzle); c.getMuzzleHand(muzzleLeft, 1); c.getHeadPosition(head);
  return {
    frame, state: Object.fromEntries(tracked.map(k => [k, value(c[k])])),
    root: transform(c.root), model: transform(c.model), kid: transform(c.kid), squid: transform(c.squidRoot),
    bones: c.boneList.map(transform),
    skinMatrices: c.boneList.map((b, i) => invKid.clone().multiply(b.matrixWorld).multiply(c.skeleton.boneInverses[i]).toArray()),
    channels: Array.from(c.P), springs: Array.from(c.sp), hair: Array.from(c.hx), hairVelocity: Array.from(c.hv),
    feet: c.feet.map(f => ({ planted: f.planted, sw: f.sw, mode: f.mode, su: f.su, pw: f.pw.toArray(), cw: f.cw.toArray(), cn: f.cn.toArray(), yaw: f.yaw })),
    uniforms: Object.fromEntries(Object.entries(c.u).map(([k, u]) => [k, value(u.value)])),
    fillEmissive: c.mats.fill.emissive.toArray(), coilEmissiveIntensity: c.mats.glow.emissiveIntensity,
    muzzle: muzzle.toArray(), muzzleLeft: muzzleLeft.toArray(), head: head.toArray(), parts,
  };
}

function planePhysics(slope = 0) {
  return {
    raycast(origin, direction, distance, out) {
      const den = direction.y - slope * direction.x;
      const t = Math.abs(den) < 1e-12 ? -1 : (slope * origin.x - origin.y) / den;
      out.hit = t >= 0 && t <= distance;
      out.dist = out.hit ? t : distance;
      if (out.hit) { out.point.copy(origin).addScaledVector(direction, t); out.normal.set(-slope, 1, 0).normalize(); }
      return out;
    },
  };
}

function scenarioDefinitions() {
  const scenarios = [
    { name: 'idle', frames: 180 },
    { name: 'run', frames: 100, motion: [0, 0, 4.8], state: { speed: 4.8, localMove: { x: 0, z: 1 } } },
    { name: 'strafe', frames: 100, motion: [-3.7, 0, 0], state: { speed: 3.7, localMove: { x: 1, z: 0 } } },
    { name: 'backpedal', frames: 100, motion: [0, 0, -2.3], state: { speed: 2.3, localMove: { x: 0, z: -1 } } },
    { name: 'treadmill', frames: 100, state: { speed: 4.8, localMove: { x: 0, z: 1 } } },
    { name: 'turn', frames: 100, yawRate: 1.5 },
    { name: 'aim', frames: 90, state: { firing: true, aimPitch: 0.45 } },
    { name: 'ramp', frames: 100, motion: [2, 0.4, 1], slope: 0.2, state: { speed: Math.hypot(2, 1), localMove: { x: -2, z: 1 } } },
    { name: 'jump_land', frames: 110, jump: true, motion: [0, 0, 2.5], state: { speed: 2.5, localMove: { x: 0, z: 1 } } },
    { name: 'form_cycle', frames: 150, forms: true },
    { name: 'hurt_invuln', frames: 90, hurt: 0.75, state: { hp: 25, invuln: true, ink: 0.14, lowInk: true, inEnemyInk: true } },
    { name: 'special', frames: 100, state: { special: 1 }, events: [{ frame: 10, name: 'special_leap' }, { frame: 50, name: 'special_slam' }] },
    { name: 'throw', frames: 85, state: { subAim: true }, events: [{ frame: 40, name: 'throw' }] },
    ...['victory', 'defeat', 'menu_idle', 'locker_idle', 'lobby_pose'].map(dance => ({ name: `dance_${dance}`, frames: 150, dance })),
    ...WEAPON_ORDER.map(weapon => ({ name: `weapon_${weapon}`, frames: 100, weapon, weaponAction: true, state: { firing: true, aimPitch: -0.2, charge: 0.85 } })),
  ];
  // Short and long frame intervals exercise the source's clamp and spring substeps.
  scenarios.push({ name: 'idle_30hz', frames: 60, dt: 1 / 30 }, { name: 'idle_120hz', frames: 240, dt: 1 / 120 });
  return scenarios;
}

function inputFor(scenario, frame) {
  const dt = scenario.dt ?? 1 / 60;
  const t = frame * dt;
  const position = (scenario.motion ?? [0, 0, 0]).map(v => v * t);
  const state = { grounded: true, form: 'kid', localMove: { x: 0, z: 0 }, ink: 1, ...scenario.state };
  if (scenario.jump && frame >= 20 && frame < 65) {
    const jt = (frame - 20) * dt;
    position[1] = Math.max(0, 5.4 * jt - 7.2 * jt * jt);
    state.grounded = false; state.vy = 5.4 - 14.4 * jt;
  }
  if (scenario.forms) {
    state.form = frame < 25 ? 'kid' : frame < 55 ? 'squid' : frame < 85 ? 'swim' : frame < 115 ? 'climb' : 'kid';
    state.wallNormal = new THREE.Vector3(0, 0, 1);
  }
  const events = (scenario.events ?? []).filter(e => e.frame === frame);
  if (scenario.jump && frame === 20) events.push({ name: 'jump' });
  if (scenario.jump && frame === 65) events.push({ name: 'land', arg: 9 });
  if (scenario.hurt && frame === 8) events.push({ name: 'hit', arg: { x: 0.6, z: 0.8, amount: 0.9 } });
  if (scenario.weaponAction && frame % 20 === 8) {
    const event = { roller: 'flick', brush: 'flick', bucket: 'slosh', blade: 'blade_slash', bow: 'charge_release', charger: 'charge_release', spinner: 'stream', twins: 'shoot', mitts: 'shoot' }[scenario.weapon] ?? 'shoot';
    events.push({ name: event, arg: scenario.weapon === 'twins' ? { hand: Math.floor(frame / 20) % 2 } : 1 });
  }
  return { dt, position, yaw: t * (scenario.yawRate ?? 0), state, events };
}

export async function exportCharacterPose() {
  const hashes = await sourceHashes();
  const style = { hair: 0, skin: 0, outfit: 0, eyes: 0 };
  const rest = getRestPositions(style), inverses = getBoneInverses(style);
  const result = {
    version: 1, generator: 'actual Character.update; no source patches or animation replacement', hashes,
    boneNames: BONE_NAMES, parents: BONE_NAMES.map(n => BONE_NAMES.indexOf(BONE_PARENT[n])),
    rest: BONE_NAMES.map(n => rest[n].toArray()), inverseBind: inverses.map(m => m.toArray()),
    matrixConvention: 'column-major; skin = inverse(kid.matrixWorld) * bone.matrixWorld * inverseBind',
    scenarios: [],
  };
  const saved = { scene: G.scene, physics: G.physics, actors: G.actors, mode: G.mode, camera: G.camera, local: G.local };
  try {
    G.actors = []; G.mode = 'match'; G.camera = null; G.local = null;
    for (const scenario of scenarioDefinitions()) {
      G.scene = new THREE.Scene(); G.physics = planePhysics(scenario.slope ?? 0);
      const character = new Character({ name: 'pose-oracle', style, weapon: scenario.weapon ?? 'shooter' });
      character.setSub(scenario.subId ?? 'bomb');
      character.setLod('game'); G.scene.add(character.root);
      if (scenario.dance) character.setDance(scenario.dance);
      if (scenario.hurt) character.setHurt(scenario.hurt, '#2f5bff');
      const inputs = [], samples = [];
      try {
        for (let frame = 1; frame <= scenario.frames; frame++) {
          const input = inputFor(scenario, frame); inputs.push(value(input));
          character.root.position.fromArray(input.position); character.root.rotation.y = input.yaw;
          for (const event of input.events) character.trigger(event.name, event.arg);
          character.update(input.dt, input.state);
          if (frame === 1 || frame % 15 === 0 || frame === scenario.frames) samples.push(capture(character, frame));
        }
      } finally { character.dispose(); }
      result.scenarios.push({ definition: scenario, inputs, samples });
    }
  } finally { Object.assign(G, saved); }
  return result;
}

export { capture as captureCharacterPose };

if (import.meta.url === pathToFileURL(process.argv[1] ?? '').href) {
  const output = resolve(process.argv[2] ?? resolve(root, 'tests/fixtures/character_pose_fixtures.json'));
  const data = await exportCharacterPose();
  const text = JSON.stringify(data) + '\n';
  await writeFile(output, text);
  console.log(`Character oracle: ${data.scenarios.length} scenarios, ${data.scenarios.reduce((n, s) => n + s.samples.length, 0)} samples, ${Buffer.byteLength(text)} bytes -> ${output}`);
}
