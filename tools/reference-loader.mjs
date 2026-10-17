import { pathToFileURL } from 'node:url';
import { resolve as resolvePath } from 'node:path';

const vendor = resolvePath(import.meta.dirname, '../splat-ref/vendor/three');

export function resolve(specifier, context, nextResolve) {
  if (specifier === 'three') return { url: pathToFileURL(resolvePath(vendor, 'build/three.module.js')).href, shortCircuit: true };
  if (specifier.startsWith('three/addons/')) return { url: pathToFileURL(resolvePath(vendor, 'jsm', specifier.slice(13))).href, shortCircuit: true };
  return nextResolve(specifier, context);
}
