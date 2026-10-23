import { mkdir, readFile, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { referenceBrowser } from './reference-browser-session.mjs';

const root = path.resolve(import.meta.dirname, '..');
const check = process.argv.includes('--check');
if (process.argv.slice(2).some(arg => arg !== '--check')) throw new Error('Usage: node tools/export-hud-icons.mjs [--check]');
const browser = await referenceBrowser(root, 'hud-icons');
try {
  const icons = await browser.evaluate(`(async () => {
    await import('/src/game/kits/index.js');
    const { WEAPON_ICONS, SQUID } = await import('/src/ui/ui-icons.js');
    const result = {};
    for (const [kind, markup] of Object.entries({...WEAPON_ICONS, squid: SQUID})) {
      const parsed = new DOMParser().parseFromString(markup, 'text/html').querySelector('svg');
      const svg = new XMLSerializer().serializeToString(parsed).replace('<svg ', '<svg width="128" height="128" ').replaceAll('currentColor', '#ffffff');
      const url = URL.createObjectURL(new Blob([svg], {type:'image/svg+xml'}));
      try {
        const image = new Image(); image.src = url; try { await image.decode(); } catch { throw new Error(kind + ': ' + svg); }
        const canvas = document.createElement('canvas'); canvas.width = canvas.height = 128;
        canvas.getContext('2d').drawImage(image, 0, 0, 128, 128);
        result[kind] = canvas.toDataURL('image/png').split(',')[1];
      } finally { URL.revokeObjectURL(url); }
    }
    return result;
  })()`);
  const directory = path.join(root, 'assets/ui/weapons');
  if (!check) await mkdir(directory, { recursive:true });
  for (const [kind, base64] of Object.entries(icons)) {
    const file = path.join(directory, kind + '.png'), bytes = Buffer.from(base64, 'base64');
    if (check) {
      if (!bytes.equals(await readFile(file))) throw new Error('HUD icon differs from source: ' + kind);
    } else await writeFile(file, bytes);
  }
  console.log((check ? 'Checked ' : 'Exported ') + Object.keys(icons).length + ' source weapon icons');
} finally { await browser.close(); }
