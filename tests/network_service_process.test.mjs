import { spawn } from 'node:child_process';
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
  console.log('Standalone room service and client processes passed');
} finally {
  if (server) { server.child.kill(); await server.done; }
}
