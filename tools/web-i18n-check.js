#!/usr/bin/env node
// Checks that every text the web client asks for is in the translation tables.
//
//   node tools/web-i18n-check.js <ru.strings> <en.strings> <web dir with ps5-*.js, transmission-app.js and index.html>
//
// In the web client (patches/web-client/web_patch.pl patches its code and adds ps5-compat.js, ps5-panel.js) every text is wrapped
// in ps5t("English text") or ps5t("template with {1}", [values]), and index.html marks texts with data-i18n attributes. The key of
// the translation tables is the English text itself. This tool finds every such text in the generated web files and reports
// the ones that are missing in the table (they would stay English).
'use strict';
const fs = require('fs');
const path = require('path');

const unesc = (x) => x.replace(/\\(.)/g, (_, c) => (c === 'n' ? '\n' : c));
const isWebKey = (k) => k !== 'LanguageName' && /[^a-z0-9_]/.test(k);
function table(text) {
  const m = new Map(), re = /^"((?:[^"\\]|\\.)*)" = "((?:[^"\\]|\\.)*)"$/gm;
  let r;
  while ((r = re.exec(text))) m.set(unesc(r[1]), unesc(r[2]));
  return m;
}
const jsUnescape = (s) => s.replace(/\\"/g, '"').replace(/\\'/g, "'").replace(/\\\\/g, '\\').replace(/\\u([0-9a-fA-F]{4})/g, (_, h) => String.fromCharCode(parseInt(h, 16)));

function check(ruPath, enPath, webDir) {
  const keys = new Set([...table(fs.readFileSync(enPath, 'utf8')).keys()].filter(isWebKey));
  const bad = [], used = new Set();
  for (const f of fs.readdirSync(webDir).filter((n) => n.endsWith('.js')).map((n) => path.join(webDir, n))) {
    if (f.endsWith('ps5-i18n.js')) continue;   // only explanations in a comment there
    const s = fs.readFileSync(f, 'utf8');
    const re = /ps5t\(\s*("(?:[^"\\]|\\.)*"|'(?:[^'\\]|\\.)*')/g;
    let m;
    while ((m = re.exec(s))) {
      const t = jsUnescape(m[1].slice(1, -1));
      if (s.slice(re.lastIndex).trimStart()[0] === '+') continue;   // the key is glued together in the code: checked separately by its templates
      used.add(t.trim());
    }
    const re2 = /ps5n\("[^"]*",\s*"([^"]*)"/g;
    while ((m = re2.exec(s))) used.add(m[1]);
  }
  for (const t of [...used].sort()) if (!keys.has(t)) bad.push(t);
  const indexPath = path.join(webDir, 'index.html');
  const html = fs.existsSync(indexPath) ? fs.readFileSync(indexPath, 'utf8') : '';
  const reA = /data-i18n(?:-[a-z-]+)?="([^"]*)"/g;
  let m;
  while ((m = reA.exec(html))) { const t = jsUnescape(m[1].replace(/&quot;/g, '"')); if (!keys.has(t)) bad.push('index.html: ' + t); }
  console.log('ps5t keys used in the web files:', used.size, '| missing in the table:', bad.length);
  bad.forEach((t) => console.log('  MISSING:', t.slice(0, 90)));
  return bad.length ? 1 : 0;
}

const a = process.argv.slice(2);
if (a.length !== 3) { console.log('usage: node tools/web-i18n-check.js <ru.strings> <en.strings> <web dir>'); process.exit(2); }
process.exit(check(a[0], a[1], a[2]));
