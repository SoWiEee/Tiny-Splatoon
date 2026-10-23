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
    const { bossEmblem } = await import('/src/ui/boss-art.js');
    const { DEATH_MARK_SVG } = await import('/src/ui/hud.js');
    const { TEAM_PALETTES, COLORBLIND_PALETTE } = await import('/src/config.js');
    const result = {};
    const entries = Object.entries({...WEAPON_ICONS, squid: SQUID}).map(([kind, markup]) => [kind, markup, '#ffffff']);
    for (const cracked of [false, true]) entries.push(['boss' + (cracked ? '-cracked' : ''), bossEmblem({cracked}).replaceAll('var(--boss, var(--enemy, #2f5bff))', '#2f5bff').replaceAll('var(--weak, var(--self, #ff8a14))', '#ff8a14'), '#ffffff']);
    for (const [palette, colors] of [...TEAM_PALETTES, COLORBLIND_PALETTE].entries()) {
      for (const [team, color] of [colors.a, colors.b].entries()) entries.push(['arrow-' + palette + '-' + team, '<svg viewBox="-13 -15 26 30"><path d="M0 -11 L9 11 L0 5.5 L-9 11 Z" fill="#15121c" stroke="#15121c" stroke-width="6" stroke-linejoin="round"/><path d="M0 -11 L9 11 L0 5.5 L-9 11 Z" fill="'+color+'" stroke="#fff" stroke-width="2.6" stroke-linejoin="round"/></svg>', color]);
      entries.push(['death-' + palette + '-0', DEATH_MARK_SVG, colors.a]);
      entries.push(['death-' + palette + '-1', DEATH_MARK_SVG, colors.b]);
    }
    for (const [kind, markup, color] of entries) {
      const parsed = new DOMParser().parseFromString(markup, 'text/html').querySelector('svg');
      const svg = new XMLSerializer().serializeToString(parsed).replace('<svg ', '<svg width="128" height="128" ').replaceAll('currentColor', color);
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
  const weaponDirectory = path.join(root, 'assets/ui/weapons');
  const markerDirectory = path.join(root, 'assets/ui/markers');
  if (!check) {
    await mkdir(weaponDirectory, { recursive:true });
    await mkdir(markerDirectory, { recursive:true });
  }
  for (const [kind, base64] of Object.entries(icons)) {
    const file = path.join(kind.startsWith('death-') || kind.startsWith('boss') || kind.startsWith('arrow-') ? markerDirectory : weaponDirectory, kind + '.png');
    const bytes = Buffer.from(base64, 'base64');
    if (check) {
      if (!bytes.equals(await readFile(file))) throw new Error('HUD icon differs from source: ' + kind);
    } else await writeFile(file, bytes);
  }
  console.log((check ? 'Checked ' : 'Exported ') + (Object.keys(icons).length - 26) + ' source weapon icons and 12 palette death markers plus 2 Boss emblems and 12 palette arrows');
} finally { await browser.close(); }
