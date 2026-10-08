// Prints what a .wasm imports and exports. Run after building: any import the
// loader does not provide is a runtime failure in the browser, and any import
// at all is worth knowing about for a module that is supposed to be standalone.
import { readFileSync } from 'node:fs';

const file = process.argv[2];
if (!file) {
  console.error('usage: node tools/wasm_imports.mjs <file.wasm>');
  process.exit(2);
}

const mod = await WebAssembly.compile(readFileSync(file));
const imports = WebAssembly.Module.imports(mod);
const exports = WebAssembly.Module.exports(mod);

if (imports.length === 0) {
  console.log('imports: none (fully standalone)');
} else {
  console.log(`imports: ${imports.length}`);
  for (const i of imports) console.log(`  ${i.module}.${i.name} (${i.kind})`);
}
const fns = exports.filter((e) => e.kind === 'function').map((e) => e.name);
console.log(`exports: ${fns.length} functions`);
console.log(`  ${fns.filter((n) => n.startsWith('rzf_')).join(', ')}`);
