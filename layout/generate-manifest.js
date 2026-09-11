const fs = require('fs');
const path = require('path');

const dir = path.join(__dirname, 'NESTING-SVG');
const files = fs.readdirSync(dir)
  .filter((name) => name.endsWith('.svg'))
  .sort((a, b) => a.localeCompare(b, undefined, { numeric: true }));

fs.writeFileSync(
  path.join(dir, 'manifest.json'),
  JSON.stringify(files, null, 2) + '\n'
);

console.log(`Updated manifest.json with ${files.length} SVG file(s):`);
files.forEach((f) => console.log(`  - ${f}`));
