import { mkdir, readFile, writeFile } from 'node:fs/promises';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { referenceBrowser } from './reference-browser-session.mjs';

const root = path.resolve(import.meta.dirname, '..');
const [mode = '--svg-only'] = process.argv.slice(2);
if (!['--svg-only', '--raster', '--check'].includes(mode) || process.argv.length > 3)
  throw new Error('Usage: node tools/export-menu-source-icons.mjs [--svg-only|--raster|--check]');

const { GLYPHS } = await import(pathToFileURL(path.join(root, 'splat-ref/src/ui/ui-icons.js')).href);
const iconDefs = [
  ['controls', 'gamepad', '#211d27'],
  ['video', 'monitor', '#211d27'],
  ['audio', 'speaker', '#211d27'],
  ['gameplay', 'swords', '#211d27'],
  ['back', 'back', '#fff9e9'],
];
const dedupeAttributes = (markup) => markup.replace(/<([A-Za-z][^\s/>]*)([^>]*)>/g, (tag, name) => {
  const seen = new Set();
  const attributes = [...tag.matchAll(/\s+([A-Za-z_:][-A-Za-z0-9_:.]*)="([^"]*)"/g)]
    .filter(([, key]) => !seen.has(key) && seen.add(key))
    .map(([, key, value]) => ` ${key}="${value}"`).join('');
  return `<${name}${attributes}${tag.endsWith('/>') ? '/' : ''}>`;
});
const svgFor = (markup, color) => {
  markup = dedupeAttributes(markup);
  const openingTagEnd = markup.indexOf('>');
  if (!markup.startsWith('<svg ') || openingTagEnd < 0 || !markup.endsWith('</svg>'))
    throw new Error('Unexpected source glyph markup');
  const openingTag = markup.slice(0, openingTagEnd + 1).replace(/\s+class="[^"]*"/, '');
  const svg = openingTag.replace('<svg ', '<svg xmlns="http://www.w3.org/2000/svg" width="96" height="96" ')
    + markup.slice(openingTagEnd + 1);
  return svg.replaceAll('currentColor', color).replaceAll('var(--k, #15121c)', '#15121c') + '\n';
};
const svgIcons = iconDefs.map(([name, glyph, color]) => {
  if (!GLYPHS[glyph]) throw new Error(`Missing source UI glyph: ${glyph}`);
  return [name, svgFor(GLYPHS[glyph], color)];
});
const output = path.join(root, 'assets/ui/menu-icons');
if (mode !== '--check') await mkdir(output, { recursive: true });
for (const [name, svg] of svgIcons) {
  const file = path.join(output, `${name}.svg`);
  if (mode === '--check') {
    if ((await readFile(file, 'utf8')) !== svg) throw new Error(`Stale source menu SVG: ${name}`);
  } else await writeFile(file, svg);
}

if (mode === '--svg-only') {
  console.log(`Exported ${svgIcons.length} source UI glyphs as SVG; splat-ref was read only`);
} else {
  const browser = await referenceBrowser(root, 'menu-source-icons');
  try {
    const pngs = await browser.evaluate(`(async () => {
      const icons = ${JSON.stringify(svgIcons)};
      const output = {};
      for (const [name, svg] of icons) {
        const url = URL.createObjectURL(new Blob([svg], {type:'image/svg+xml'}));
        try {
          const image = new Image(); image.src = url; await image.decode();
          const canvas = document.createElement('canvas'); canvas.width = canvas.height = 96;
          canvas.getContext('2d').drawImage(image, 0, 0, 96, 96);
          output[name] = canvas.toDataURL('image/png').split(',')[1];
        } finally { URL.revokeObjectURL(url); }
      }
      return output;
    })()`);
    for (const [name] of svgIcons) {
      const file = path.join(output, `${name}.png`);
      const bytes = Buffer.from(pngs[name], 'base64');
      if (mode === '--check') {
        if (!bytes.equals(await readFile(file))) throw new Error(`Source menu icon differs from source: ${name}`);
      } else await writeFile(file, bytes);
    }
    console.log(`${mode === '--check' ? 'Checked' : 'Exported'} ${svgIcons.length} source menu icons; splat-ref was read only`);
  } finally { await browser.close(); }
}
