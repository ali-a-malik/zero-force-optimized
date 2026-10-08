// Contrast audit for the app's text and mark colours (WCAG 2.1 relative
// luminance). Text needs 4.5:1, marks and large text 3:1. Run after touching
// any colour token:  node tools/check_contrast.mjs
import { readFileSync } from 'node:fs';

const css = readFileSync(new URL('../web/styles.css', import.meta.url), 'utf8');
const token = (name) => css.match(new RegExp(`--${name}:\\s*(#[0-9a-f]{6})`, 'i'))?.[1];

const channels = (h) => [1, 3, 5].map((i) => parseInt(h.slice(i, i + 2), 16) / 255);
const linear = (c) => (c <= 0.03928 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4);
const luminance = (h) => {
  const [r, g, b] = channels(h).map(linear);
  return 0.2126 * r + 0.7152 * g + 0.0722 * b;
};
const ratio = (a, b) => {
  const [hi, lo] = [luminance(a), luminance(b)].sort((x, y) => y - x);
  return (hi + 0.05) / (lo + 0.05);
};

// Every colour the app paints on paper, with the job it does.
const surface = token('paper-raised');
const cases = [
  ['ink', 'text'], ['ink-soft', 'text'], ['ink-faint', 'text'],
  ['signal', 'text'], ['safe', 'text'], ['amber', 'text'],
  ['rule-strong', 'mark'], ['rule', 'decor'],
];

let failures = 0;
console.log(`surface ${surface}`);
for (const [name, job] of cases) {
  const colour = token(name);
  // Decorative dividers carry no information and are exempt from 1.4.11;
  // anything that marks a control boundary or encodes data is not, and uses
  // --rule-strong instead.
  if (job === 'decor') {
    console.log(`  [ -- ] --${name.padEnd(12)} ${colour}  ${ratio(colour, surface).toFixed(2)}:1  (divider, exempt)`);
    continue;
  }
  const need = job === 'text' ? 4.5 : 3;
  const got = ratio(colour, surface);
  const ok = got >= need;
  if (!ok) failures += 1;
  console.log(
    `  [${ok ? 'PASS' : 'FAIL'}] --${name.padEnd(12)} ${colour}  ${got.toFixed(2)}:1  ` +
      `(${job}, needs ${need}:1)`,
  );
}
console.log(failures ? `\nFAILED: ${failures} colours below threshold` : '\nPASSED: all colours meet their threshold');
process.exit(failures ? 1 : 0);
