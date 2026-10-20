import { spawn } from 'node:child_process';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const child = spawn(process.execPath, [path.join(root, 'tools', 'export-audio-source.mjs'), '--check'], {
  cwd: root, windowsHide: true, stdio: 'inherit',
});
const code = await new Promise((resolve, reject) => {
  child.on('error', reject);
  child.on('close', resolve);
});
if (code !== 0) throw new Error(`Source audio render/check failed with exit ${code}`);
