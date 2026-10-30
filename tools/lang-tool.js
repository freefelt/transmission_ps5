#!/usr/bin/env node
// Checks the translation tables of the settings page (a development tool, not part of the build).
//
//   node tools/lang-tool.js check <page dir> <en.strings> <ru.strings>   checks the tables against the page source (src/manager/page)
//   node tools/lang-tool.js candidates <page dir> <en.strings>           lists fragments of the page that look like UI text but are not in the table
//
// The source text of the page is English. A fragment of the page (the text between the delimiters ' " < > and the end of a line; the comments
// /* */ and // and the escaped \' and \" are skipped) is translated when it is EXACTLY an English text of the table. The same rules as lang_localize
// in src/manager/language.cc. Message keys (me_*, mr_*) and the English-text keys of the web interface are not anchors of the page.
// To add a text to the page: write it in English in the page, add `"slug" = "English text"` to en.strings and the translation with the
// same slug to ru.strings, then run `check`. Texts must not contain ' " < > \, and must not be a bare lowercase word: such a text would also
// replace the code (d.state==='running').
'use strict';
const fs = require('fs');

function pageText(dir) {
  const rd = (n) => fs.readFileSync(`${dir}/${n}`, 'utf8');
  return [rd('index.html').replace('@@STYLE@@\n', () => rd('style.css')).replace('@@SCRIPT@@\n', () => rd('settings.js'))];
}

function fragments(text) {
  const res = [], n = text.length, DEL = '\'"<>\n';
  let i = 0, seg = 0;
  const add = (end) => { const f = text.slice(seg, end); if (f) res.push(f); };
  while (i <= n) {
    const ch = i < n ? text[i] : '\n';
    if (ch === '\\' && i + 1 < n && '\'"'.includes(text[i + 1])) { i += 2; continue; }
    if (ch === '/' && i + 1 < n && text[i + 1] === '*') {
      let e = text.indexOf('*/', i + 2); e = e < 0 ? n : e + 2;
      add(i); i = e; seg = i; continue;
    }
    if (ch === '/' && i + 1 < n && text[i + 1] === '/' && (i === 0 || ' \t\n;{}'.includes(text[i - 1]))) {
      let e = text.indexOf('\n', i); e = e < 0 ? n : e;
      add(i); i = e; seg = i; continue;
    }
    if (DEL.includes(ch)) { add(i); seg = i + 1; }
    i++;
  }
  return res;
}

const allFragments = (dir) => pageText(dir).flatMap(fragments);

function die(msg) { console.error(msg); process.exit(1); }

// A simple .strings parser for the checks (like lang_parse): a list of [key, value], the first one is LanguageName.
function parseStrings(path) {
  const items = [];
  fs.readFileSync(path, 'utf8').split('\n').forEach((raw, idx) => {
    const line = raw.trim();
    if (!line || line.startsWith('#') || line.startsWith('//')) return;
    const m = /^"((?:[^"\\]|\\.)*)"\s*=\s*"((?:[^"\\]|\\.)*)"\s*;?$/.exec(line);
    if (!m) die(`${path}:${idx + 1}: syntax error`);
    const un = (t) => t.replace(/\\(.)/g, (_, c) => (c === 'n' ? '\n' : c));
    const k = un(m[1]), v = un(m[2]);
    // the same limits as lang_parse: otherwise the manager switches the whole translation off
    if (Buffer.byteLength(k) > 1000 || Buffer.byteLength(v) > 4000 || !k)
      die(`${path}:${idx + 1}: key longer than 1000 bytes or value longer than 4000 bytes (${Buffer.byteLength(k)}/${Buffer.byteLength(v)})`);
    items.push([k, v]);
  });
  return items;
}

// message / web client key (not a slug)
const isMsg = (k) => k.slice(0, 3) === 'me_' || k.slice(0, 3) === 'mr_' || (k !== 'LanguageName' && /[^a-z0-9_]/.test(k));
const placeholders = (v) => new Set(v.match(/\{[1-9]\}/g) || []);
const sameSet = (a, b) => a.size === b.size && [...a].every((x) => b.has(x));
const symDiff = (a, b) => [...a].filter((x) => !b.has(x)).concat([...b].filter((x) => !a.has(x))).sort();

// A fragment that probably is UI text: starts with a capital letter, has letters and a space or ends with a sentence mark.
function looksLikeText(f) {
  const t = f.trim();
  return /[a-z]{3}/.test(t) && /^[A-Z‘“(][A-Za-z0-9’”… ,.;:!?()/+&%-]{2,}$/.test(t) && (t.includes(' ') || '.:!?'.includes(t[t.length - 1]));
}

function toMap(items) { const m = new Map(); items.forEach(([k, v]) => m.set(k, v)); return m; }

function cmdCheck(dir, enPath, ruPath) {
  const fr = allFragments(dir), eni = parseStrings(enPath), rui = parseStrings(ruPath);
  let ok = true;
  if (eni[0][0] !== 'LanguageName' || rui[0][0] !== 'LanguageName') { console.log('ERROR: the first entry must be LanguageName'); ok = false; }
  const emap = toMap(eni.slice(1).filter(([k]) => !isMsg(k))), rmap = toMap(rui.slice(1).filter(([k]) => !isMsg(k)));
  const emsg = toMap(eni.slice(1).filter(([k]) => isMsg(k))), rmsg = toMap(rui.slice(1).filter(([k]) => isMsg(k)));
  const used = new Set(fr);
  const unused = [...emap].filter(([, v]) => !used.has(v)).map(([k]) => k);
  if (unused.length) {
    ok = false; console.log('ERROR: English texts of the table that are not a fragment of the page:', unused.length);
    unused.slice(0, 10).forEach((k) => console.log('   ', k, JSON.stringify(emap.get(k)).slice(0, 90)));
  }
  const inv = new Map();
  emap.forEach((v, k) => { if (!inv.has(v)) inv.set(v, []); inv.get(v).push(k); });
  const dups = [...inv].filter(([, ks]) => ks.length > 1);
  if (dups.length) { ok = false; console.log('ERROR: the same English text under several keys (a text can have only one translation):', JSON.stringify(dups.slice(0, 5).map(([v, ks]) => [v.slice(0, 40), ks]))); }
  const bare = [...inv.keys()].filter((v) => /^[a-z_][a-z0-9_]*$/.test(v));
  if (bare.length) { ok = false; console.log("ERROR: bare lowercase words as texts (they would also replace code like ==='running'); use a capital letter or several words:", JSON.stringify(bare.slice(0, 10))); }
  const badchar = [...inv.keys()].filter((v) => /['"<>\\\n]/.test(v));
  if (badchar.length) { ok = false; console.log('ERROR: texts with \' " < > \\ or a newline (they are delimiters of fragments; use typographic quotes):', JSON.stringify(badchar.slice(0, 10).map((v) => v.slice(0, 40)))); }
  const ek = new Set(emap.keys()), rk = new Set(rmap.keys());
  if (!sameSet(ek, rk)) { ok = false; console.log('ERROR: the page keys of en and ru differ:', JSON.stringify(symDiff(ek, rk).slice(0, 10))); }
  const badru = [...rmap].filter(([, v]) => /[<>\\]/.test(v)).map(([k]) => k);
  if (badru.length) { ok = false; console.log('ERROR: Russian values of the page with < > \\ :', JSON.stringify(badru.slice(0, 10))); }
  const emk = new Set(emsg.keys()), rmk = new Set(rmsg.keys());
  if (!sameSet(emk, rmk)) { ok = false; console.log('ERROR: the sets of message keys differ:', JSON.stringify(symDiff(emk, rmk).slice(0, 10))); }
  const badph = [...rmsg.keys()].filter((k) => emsg.has(k) && !sameSet(placeholders(rmsg.get(k)), placeholders(emsg.get(k))));
  if (badph.length) { ok = false; console.log('ERROR: the {n} placeholders of a message differ between English and Russian:', JSON.stringify(badph.slice(0, 10))); }
  const cand = [...new Set(fr.filter((f) => !inv.has(f) && looksLikeText(f)))];
  if (cand.length) console.log('WARNING: fragments that look like UI text but are not in the table (run `candidates` to see all):', cand.length);
  console.log('fragments in page:', fr.length, '| page keys en/ru:', emap.size, rmap.size, '| message keys en/ru:', emsg.size, rmsg.size, ok ? '| OK' : '| FAILED');
  process.exit(ok ? 0 : 1);
}

function cmdCandidates(dir, enPath) {
  const fr = allFragments(dir);
  const known = new Set(parseStrings(enPath).slice(1).filter(([k]) => !isMsg(k)).map(([, v]) => v));
  [...new Set(fr.filter((f) => !known.has(f) && looksLikeText(f)))].sort().forEach((f) => console.log(JSON.stringify(f)));
}

const a = process.argv.slice(2);
if (a.length === 4 && a[0] === 'check') cmdCheck(a[1], a[2], a[3]);
else if (a.length === 3 && a[0] === 'candidates') cmdCandidates(a[1], a[2]);
else { console.log(fs.readFileSync(__filename, 'utf8').split('\n').filter((l) => l.startsWith('//')).map((l) => l.slice(3)).join('\n')); process.exit(2); }
