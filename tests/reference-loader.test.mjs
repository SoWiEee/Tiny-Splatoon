import test from 'node:test';
import assert from 'node:assert/strict';
import { register } from 'node:module';

test('loader resolves the vendored Three.js addon path', async () => {
  register(new URL('../tools/reference-loader.mjs', import.meta.url));
  const addon = await import('three/addons/controls/OrbitControls.js');
  assert.equal(typeof addon.OrbitControls, 'function');
});
