import { spawn } from 'node:child_process';
import { createSocket } from 'node:dgram';
import { readFile } from 'node:fs/promises';
import { setTimeout as delay } from 'node:timers/promises';

const [serverExe, menuTestExe, roomTestExe] = process.argv.slice(2);
if (!serverExe || !menuTestExe || !roomTestExe) throw new Error('Expected server, menu adapter, and room test executables');

function launch(exe, args) {
  const child = spawn(exe, args, { windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
  let output = '';
  child.stdout.on('data', chunk => { output += chunk; });
  child.stderr.on('data', chunk => { output += chunk; });
  const done = new Promise((resolve, reject) => {
    child.once('error', reject);
    child.once('close', code => resolve(code));
  });
  return { child, done, output: () => output };
}

async function freeUdpPort() {
  const socket = createSocket('udp4');
  await new Promise((resolve, reject) => {
    socket.once('error', reject);
    socket.bind(0, '127.0.0.1', resolve);
  });
  const { port } = socket.address();
  await new Promise((resolve, reject) => socket.close(error => error ? reject(error) : resolve()));
  return port;
}

async function directChildren(pid) {
  try {
    return (await readFile(`/proc/${pid}/task/${pid}/children`, 'utf8')).trim().split(/\s+/).filter(Boolean).map(Number);
  } catch { return []; }
}

async function waitForDirectChildExit(parentPid, childPid) {
  const deadline = Date.now() + 5000;
  while (Date.now() < deadline) {
    if (!(await directChildren(parentPid)).includes(childPid)) return;
    await delay(10, undefined, { ref: false });
  }
  throw new Error(`Local room service process ${childPid} was not reaped`);
}

function stop(pid) {
  try { process.kill(pid, 'SIGTERM'); } catch (error) { if (error.code !== 'ESRCH') throw error; }
}

async function testLocalServiceLaunch() {
  const address = `127.0.0.1:${await freeUdpPort()}`;
  const child = spawn(menuTestExe, ['--local-service', address], { windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'] });
  const markerText = 'ONLINE_LOCAL_SERVICE_FLOW_COMPLETE';
  let output = '', serverPid, serverReaped = false, finished = false, resolveMarker;
  const marker = new Promise(resolve => { resolveMarker = resolve; });
  const collect = chunk => {
    output += chunk;
    if (output.includes(markerText)) resolveMarker();
  };
  child.stdout.on('data', collect); child.stderr.on('data', collect);
  const done = new Promise(resolve => {
    child.once('error', error => { finished = true; resolve({ error }); });
    child.once('close', code => { finished = true; resolve({ code }); });
  });
  try {
    await Promise.race([
      marker,
      done.then(result => { throw new Error(`Local-service menu flow exited early (${result.code ?? result.error}):\n${output}`); }),
      delay(45000, undefined, { ref: false }).then(() => { throw new Error(`Local-service menu flow timed out:\n${output}`); }),
    ]);
    if (!serverPid) serverPid = (await directChildren(child.pid))[0];
    if (!serverPid) throw new Error(`Local room service was not a child process:\n${output}`);
    stop(serverPid);
    await waitForDirectChildExit(child.pid, serverPid);
    serverReaped = true;
    child.stdin.end('cleanup\n');
    const result = await Promise.race([
      done,
      delay(5000, undefined, { ref: false }).then(() => ({ timeout: true })),
    ]);
    if (result.timeout) throw new Error(`Local-service harness did not exit after cleanup:\n${output}`);
    if (result.error || result.code !== 0) throw new Error(`Local-service menu test failed (${result.code ?? result.error}):\n${output}`);
    process.stdout.write(output);
  } finally {
    if (serverPid && !serverReaped) stop(serverPid);
    else if (!serverPid) for (const pid of await directChildren(child.pid)) stop(pid);
    if (!finished) child.stdin.end('cleanup\n');
    if (!finished) {
      await Promise.race([done, delay(2000, undefined, { ref: false })]);
      if (!finished) child.kill();
    }
  }
}

let server;
let port;
try {
  for (let attempt = 0; attempt < 16; ++attempt) {
    port = 30000 + (process.pid * 17 + attempt) % 10000;
    const candidate = launch(serverExe, ['--bind', '127.0.0.1', '--port', String(port), '--run-for', '60']);
    const state = await Promise.race([
      candidate.done.then(code => ({ code })),
      (async () => {
        for (let i = 0; i < 160; ++i) {
          if (candidate.output().includes('Room server listening on port ')) return { ready: true };
          await delay(50, undefined, { ref: false });
        }
        return { timeout: true };
      })(),
    ]);
    if (state.ready) { server = candidate; break; }
    if (state.timeout) { candidate.child.kill(); await candidate.done; throw new Error(`Room service startup timed out:\n${candidate.output()}`); }
    await candidate.done;
    if (attempt === 15) throw new Error(`Room service could not bind a test port:\n${candidate.output()}`);
  }
  const address = `127.0.0.1:${port}`;
  for (const [exe, args] of [[menuTestExe, [address]], [roomTestExe, ['3', '--server', address]]]) {
    const test = launch(exe, args);
    const code = await Promise.race([test.done, delay(45000, undefined, { ref: false }).then(() => 'timeout')]);
    if (code === 'timeout') { test.child.kill(); await test.done; }
    if (code !== 0) throw new Error(`Cross-process test failed (${code}):\n${test.output()}\nServer:\n${server.output()}`);
    process.stdout.write(test.output());
  }
  if (process.platform === 'linux') await testLocalServiceLaunch();
  console.log('Standalone room service and client processes passed');
} finally {
  if (server) { server.child.kill(); await server.done; }
}
