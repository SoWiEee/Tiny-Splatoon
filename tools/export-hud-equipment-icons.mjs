import { mkdir, readFile, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { referenceBrowser } from './reference-browser-session.mjs';

const root = path.resolve(import.meta.dirname, '..');
const check = process.argv.includes('--check');
if (process.argv.slice(2).some(arg => arg !== '--check')) throw new Error('Usage: node tools/export-hud-equipment-icons.mjs [--check]');
const browser = await referenceBrowser(root, 'hud-equipment-icons');
let failure;
try {
  const icons = await browser.evaluate(`(async () => {
    await import('/src/game/kits/index.js');
    const [{ SUB_ICONS, specialIcon }, { SUBS, SUB_ORDER, SPECIAL_ORDER }] = await Promise.all([
      import('/src/ui/ui-icons.js'), import('/src/config.js')
    ]);
    const rasterize = async (name, markup) => {
      const parsed = new DOMParser().parseFromString(markup, 'text/html').querySelector('svg');
      if (!parsed) throw new Error('Missing source SVG: ' + name);
      const svg = new XMLSerializer().serializeToString(parsed)
        .replace('<svg ', '<svg width="128" height="128" ')
        .replaceAll('currentColor', '#ffffff');
      const url = URL.createObjectURL(new Blob([svg], { type: 'image/svg+xml' }));
      try {
        const image = new Image(); image.src = url;
        try { await image.decode(); } catch { throw new Error('Cannot decode source SVG: ' + name); }
        const canvas = document.createElement('canvas'); canvas.width = canvas.height = 128;
        canvas.getContext('2d').drawImage(image, 0, 0, 128, 128);
        return canvas.toDataURL('image/png').split(',')[1];
      } finally { URL.revokeObjectURL(url); }
    };
    const result = { sub: {}, special: {} };
    for (const kind of SUB_ORDER) {
      const markup = SUB_ICONS[kind] || SUB_ICONS[SUBS[kind]?.kind] || SUB_ICONS.bomb;
      result.sub[kind] = await rasterize('sub/' + kind, markup);
    }
    for (const id of SPECIAL_ORDER) result.special[id] = await rasterize('special/' + id, specialIcon(id));
    return result;
  })()`);
  if (Object.keys(icons.sub).length !== 15 || Object.keys(icons.special).length !== 19) throw new Error('Unexpected source HUD icon counts');
  for (const [category, categoryIcons] of Object.entries(icons)) {
    const directory = path.join(root, 'assets/ui/equipment'), prefix = category === 'sub' ? 'sub-' : 'special-';
    if (!check) await mkdir(directory, { recursive: true });
    for (const [id, base64] of Object.entries(categoryIcons)) {
      const file = path.join(directory, prefix + id + '.png'), bytes = Buffer.from(base64, 'base64');
      if (check) {
        if (!bytes.equals(await readFile(file))) throw new Error('HUD equipment icon differs from source: ' + category + '/' + id);
      } else await writeFile(file, bytes);
    }
  }
  console.log((check ? 'Checked ' : 'Exported ') + Object.keys(icons.sub).length + ' sub and ' + Object.keys(icons.special).length + ' special icons');
} catch (error) { failure=error; console.error('HUD equipment icon export failed:',error.message); throw error; }
finally { try { await browser.close(); } catch (error) { if (!failure) console.warn('Could not remove the temporary Edge profile:',error.message); } }
