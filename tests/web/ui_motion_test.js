const puppeteer = require("puppeteer-core");
const http = require("http");
let pass = 0, fail = 0; const check = (n, ok, e) => { (ok ? pass++ : fail++); console.log((ok ? "PASS " : "FAIL ") + n + (e !== undefined ? "  -> " + String(e).slice(0, 380) : "")); };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const post = (path, body) => new Promise((res) => { const d = JSON.stringify(body), q = http.request({ host: "127.0.0.1", port: 9092, path, method: "POST", headers: { "Content-Type": "application/json", "Content-Length": Buffer.byteLength(d) } }, (r) => { let t = ""; r.on("data", (c) => (t += c)); r.on("end", () => res([r.statusCode, t])); }); q.write(d); q.end(); });
function ben(v) { if (typeof v === "number") return Buffer.from(`i${v}e`); if (typeof v === "string") return Buffer.concat([Buffer.from(`${Buffer.byteLength(v)}:`), Buffer.from(v)]); if (Buffer.isBuffer(v)) return Buffer.concat([Buffer.from(`${v.length}:`), v]); if (Array.isArray(v)) return Buffer.concat([Buffer.from("l"), ...v.map(ben), Buffer.from("e")]); return Buffer.concat([Buffer.from("d"), ...Object.keys(v).sort().flatMap((k) => [ben(k), ben(v[k])]), Buffer.from("e")]); }
const meta = (name) => ben({ announce: "http://127.0.0.1:1/announce", info: { name, "piece length": 65536, pieces: Buffer.alloc(60, 7), files: [{ length: 65536, path: ["a.bin"] }, { length: 65536, path: ["sub", "b.bin"] }, { length: 65536, path: ["sub", "deep", "c.bin"] }] } }).toString("base64");
// Motion and rounding of the web client in real Chrome: the burger menu, windows, the inspector (layout), tabs (direction and rounding), the "Fix for USB drives" row, torrent icons
(async () => {
  for (const p of ["/mnt/usb2/torrents", "/mnt/ext1/torrents"]) await post("/api/folders", { action: "add", path: p, confirm: true });
  await post("/api/config", { language: "en" }); await sleep(1500);
  const browser = await puppeteer.launch({ executablePath: require("./chrome"), args: ["--no-sandbox", "--disable-gpu"], headless: "new" });
  const page = await browser.newPage(); await page.setViewport({ width: 1200, height: 850 }); const errors = []; page.on("pageerror", (e) => errors.push(e.message)); page.on("dialog", (d) => d.dismiss());
  const rpcAdd = (name) => page.evaluate(async (m) => { let sid = ""; for (let i = 0; i < 2; i++) { const r = await fetch("/transmission/rpc", { method: "POST", headers: { "X-Transmission-Session-Id": sid }, body: JSON.stringify({ method: "torrent-add", arguments: { metainfo: m, "download-dir": "/mnt/ext1/torrents", paused: true } }) }); if (r.status === 409) { sid = r.headers.get("X-Transmission-Session-Id"); continue; } return r.status; } return 0; }, meta(name));
  await page.goto("http://127.0.0.1:9091/transmission/web/", { waitUntil: "networkidle2" }); await sleep(1500);
  await rpcAdd("GRoot"); await rpcAdd("HSecond");
  await page.goto("http://127.0.0.1:9091/transmission/web/", { waitUntil: "networkidle2" }); await sleep(2200);
  const sample = (act, sel, times) => page.evaluate((a, s, ts) => new Promise((res) => { const out = []; new Function(a)(); ts.forEach((t) => setTimeout(() => { const e = document.querySelector(s), tc = document.getElementById("torrent-container"), dg = document.querySelector("dialog.open-torrent"); const cw = tc ? Math.round(tc.getBoundingClientRect().width) : -1, dh = dg ? Math.round(dg.getBoundingClientRect().height) : -1; if (!e) { out.push({ t, gone: true, cw, dh }); return; } const cs = getComputedStyle(e), r = e.getBoundingClientRect(), m = cs.transform !== "none" ? cs.transform.match(/matrix\(([^)]+)\)/) : null, v = m ? m[1].split(",").map(Number) : [1, 0, 0, 1, 0, 0]; out.push({ t, gone: false, op: +cs.opacity, tx: v[4], ty: v[5], an: cs.animationName, cls: e.className, mr: parseFloat(cs.marginRight) || 0, disp: cs.display, w: Math.round(r.width), h: Math.round(r.height), cw, dh }); }, t)); setTimeout(() => res(out), Math.max(...ts) + 80); }), act, sel, times);
  const ev = (f, ...a) => page.evaluate(f, ...a);
  const menuClick = (re) => `(()=>{const b=[...document.querySelectorAll('.overflow-menu button, .overflow-menu [data-action]')].find((x)=>${re}.test(x.textContent));b&&b.click()})()`;
  // ---- burger menu
  let s = await sample("document.getElementById('toolbar-overflow').click()", ".overflow-menu", [15, 70, 420]);
  check("BURGER MENU opens with the animation: 'ps5menuin', semi-transparent and shifted up at the start, fully opaque and in place at the end", s[0].an === "ps5menuin" && s[0].op < 0.95 && s[0].ty < 0 && s[2].op === 1 && s[2].ty === 0, JSON.stringify(s));
  s = await sample("document.getElementById('toolbar-overflow').click()", ".overflow-menu", [20, 80, 450]);
  check("BURGER MENU closes with the animation: it is still in the page right after the click (class ps5-out, 'ps5menuout', pointer events off), then it is gone", !s[0].gone && /ps5-out/.test(s[0].cls) && s[0].an === "ps5menuout" && s[2].gone === true, JSON.stringify(s));
  // ---- "About" window
  await ev(() => document.getElementById("toolbar-overflow").click()); await sleep(500);
  s = await sample(menuClick("/About/"), "dialog.about-dialog", [20, 80, 420]);
  check("A WINDOW (About) opens with the animation: 'ps5dlgin', transparent and lower at the start, opaque and in place at the end", !s[0].gone && s[0].an === "ps5dlgin" && s[0].op < 0.95 && s[0].ty > 0 && s[2].op === 1 && s[2].ty === 0, JSON.stringify(s));
  s = await sample("document.querySelector('dialog.about-dialog .dialog-buttons button').click()", "dialog.about-dialog", [20, 80, 450]);
  check("the window closes with the animation: still in the page right after the click (ps5-out, 'ps5dlgout'), then gone", !s[0].gone && /ps5-out/.test(s[0].cls) && s[0].an === "ps5dlgout" && s[2].gone === true, JSON.stringify(s));
  // ---- preferences (tabbed window): appearance, tabs, rounding
  await ev(() => document.getElementById("toolbar-overflow").click()); await sleep(500);
  s = await sample(menuClick("/Edit preferences/"), "#prefs-dialog", [20, 80, 420]);
  check("the preferences window (a tabbed one) opens with the animation too", !s[0].gone && s[0].an === "ps5dlgin" && s[0].op < 0.95 && s[2].op === 1, JSON.stringify(s));
  const pr = await ev(() => { const b = document.querySelector("#prefs-dialog .tabs-buttons"), t = document.querySelector("#prefs-dialog .tabs-button"); return { box: getComputedStyle(b).borderRadius, btn: getComputedStyle(t).borderRadius }; });
  check("the tab switcher of the preferences is rounded: container 14px, buttons 12px", pr.box === "14px" && pr.btn === "12px", JSON.stringify(pr));
  s = await sample("document.getElementById('prefs-tab-speed').click()", "#prefs-dialog .tabs-page:not(.hidden)", [25, 400]);
  const dir1 = await ev(() => document.querySelector("#prefs-dialog .tabs-pages").dataset.ps5dir);
  check("the content of the preferences tab slides in from the right when you go to the next tab (direction next)", dir1 === "next" && s[0].an === "ps5tabnext" && s[0].op < 0.95 && s[0].tx > 0 && s[1].op === 1 && s[1].tx === 0, JSON.stringify({ dir1, s }));
  s = await sample("document.getElementById('prefs-tab-torrent').click()", "#prefs-dialog .tabs-page:not(.hidden)", [25, 400]);
  const dir2 = await ev(() => document.querySelector("#prefs-dialog .tabs-pages").dataset.ps5dir);
  check("and from the left when you go back (direction prev)", dir2 === "prev" && s[0].an === "ps5tabprev" && s[0].tx < 0 && s[1].op === 1, JSON.stringify({ dir2, s }));
  await ev(() => document.querySelector("#prefs-dialog .tabs-container-close").click()); await sleep(500);
  // ---- inspector: slides in from the right, layout, closing
  await page.click("#torrent-list li"); await sleep(500);
  s = await sample("document.getElementById('toolbar-inspector').click()", "#inspector", [15, 110, 230, 520]);
  check("the INSPECTOR slides in from the right: at the start it is pushed out (negative right margin, transparent), the margin and the opacity grow smoothly, at the end it is in place and the inline styles are cleaned up", !s[0].gone && s[0].mr < -300 && s[0].op < 0.95 && s[1].mr > s[0].mr && s[2].mr >= s[1].mr && s[3].mr === 0 && s[3].op === 1, JSON.stringify(s.map((x) => ({ t: x.t, mr: x.mr, op: x.op }))));
  check("while the inspector comes in the torrent list is pushed smoothly (its width shrinks step by step)", s[0].cw > s[1].cw && s[1].cw >= s[2].cw && s[2].cw >= s[3].cw && s[0].cw - s[3].cw > 300, JSON.stringify(s.map((x) => x.cw)));
  await sleep(300);
  const ic = await ev(() => { const tb = document.querySelector("#inspector .tabs-buttons"), b = document.querySelector("#inspector .tabs-button"), sel = document.querySelector("#inspector .tabs-button.selected"); return { box: getComputedStyle(tb).borderRadius, btn: getComputedStyle(b).borderRadius, sel: getComputedStyle(sel).borderRadius }; });
  check("the tab switcher of the inspector is rounded: container 14px, buttons and the selected tab 12px", ic.box === "14px" && ic.btn === "12px" && ic.sel === "12px", JSON.stringify(ic));
  const rect = await ev(() => { const r = document.getElementById("inspector-tab-tiers").getBoundingClientRect(); return { x: r.left + r.width / 2, y: r.top + r.height / 2 }; });
  await page.mouse.move(rect.x, rect.y); await page.mouse.down();
  const pressed = await ev(() => getComputedStyle(document.getElementById("inspector-tab-tiers")).borderRadius);
  await page.mouse.up(); await sleep(400);
  check("a tab that is being PRESSED (mouse down) already has the new rounding (12px), not the old 5px of the stock style", pressed === "12px", pressed);
  s = await sample("document.getElementById('inspector-tab-peers').click()", "#inspector .tabs-page:not(.hidden)", [25, 400]);
  check("the content of the inspector tabs slides in too (peers is before tiers: direction prev; the page is visible at the end)", s[0].an === "ps5tabprev" && s[0].tx < 0 && s[0].op < 0.95 && s[1].op === 1 && s[1].tx === 0, JSON.stringify(s));
  await ev(() => document.getElementById("inspector-tab-files").click()); await sleep(900);
  const fr = await ev(() => { const g = (c) => { const e = document.querySelector(".file-priority-radiobox ." + c); return e ? getComputedStyle(e).borderRadius : null; }; const f = document.querySelector("#inspector-file-list .ps5-fico.file"); return { low: g("low"), normal: g("normal"), high: g("high"), fileColor: f ? getComputedStyle(f).color : null, op: f ? getComputedStyle(f).opacity : null }; });
  check("the file priority buttons: the left one is rounded on the left (7px 0 0 7px), the middle '−' stays square, the right one is rounded on the right (0 7px 7px 0)", fr.low === "7px 0px 0px 7px" && fr.normal === "0px" && fr.high === "0px 7px 7px 0px", JSON.stringify(fr));
  check("the file icon in the inspector is bright (not dim): light grey-white, fully opaque", fr.fileColor === "rgb(232, 237, 245)" && fr.op === "1", JSON.stringify(fr));
  s = await sample("document.getElementById('toolbar-inspector').click()", "#inspector", [20, 110, 480]);
  check("the inspector slides out: still in the page right after the click, pushed out to the right, then removed; the torrent list takes the space back smoothly", !s[0].gone && s[1].mr < s[0].mr + 1 && s[1].mr < -50 && s[2].gone === true && s[2].cw > s[0].cw + 300, JSON.stringify(s.map((x) => ({ t: x.t, mr: x.mr, op: x.op, cw: x.cw, gone: x.gone }))));
  // ---- torrent icons: no grey square on a paused row
  const icons = await ev(() => [...document.querySelectorAll("#torrent-list .torrent")].map((r) => ({ cls: r.className, bg: getComputedStyle(r.querySelector(".icon")).backgroundColor })));
  check("torrent icons have no grey square: a paused torrent row (selected and not selected) has a transparent icon background", icons.length === 2 && icons.some((i) => /selected/.test(i.cls)) && icons.some((i) => !/selected/.test(i.cls)) && icons.every((i) => /paused|torrent/.test(i.cls) && i.bg === "rgba(0, 0, 0, 0)"), JSON.stringify(icons));
  // ---- "Add torrents" window: the "Fix for USB drives" row unfolds smoothly, the window resizes smoothly
  await ev(() => document.getElementById("toolbar-open").click()); await sleep(1600);
  await ev(() => { const t = [...document.querySelectorAll(".ps5-tile")].find((x) => /ext1/.test(x.textContent)); t && t.click(); }); await sleep(900);
  const before = await ev(() => { const r = document.getElementById("ps5-usbfix-row"), d = document.querySelector("dialog.open-torrent"); return { disp: r ? getComputedStyle(r).display : null, dh: Math.round(d.getBoundingClientRect().height) }; });
  s = await sample("[...document.querySelectorAll('.ps5-tile')].find((x)=>/usb2/.test(x.textContent)).click()", "#ps5-usbfix-row", [30, 110, 480]);
  check("choosing a USB disk: the 'Fix for USB drives' row unfolds smoothly (height and opacity grow step by step) and the whole 'Add torrents' window grows with it", before.disp === "none" && s[0].h < s[1].h + 1 && s[1].h <= s[2].h && s[2].h >= 20 && s[0].op < 0.95 && s[2].op === 1 && s[0].dh < s[2].dh && s[0].dh >= before.dh && s[1].dh <= s[2].dh, JSON.stringify({ before, s: s.map((x) => ({ t: x.t, h: x.h, op: x.op, dh: x.dh })) }));
  s = await sample("[...document.querySelectorAll('.ps5-tile')].find((x)=>/ext1/.test(x.textContent)).click()", "#ps5-usbfix-row", [40, 120, 520]);
  check("choosing the M.2 again: the row folds smoothly and disappears (display none at the end), the window shrinks back step by step", s[0].h > s[1].h - 1 && s[2].disp === "none" && s[0].dh >= s[2].dh && s[0].dh > s[2].dh - 1, JSON.stringify(s.map((x) => ({ t: x.t, h: x.h, op: x.op, dh: x.dh, disp: x.disp }))));
  await ev(() => { const b = [...document.querySelectorAll("dialog.open-torrent .dialog-buttons button")].find((x) => /Cancel/.test(x.textContent)); b && b.click(); }); await sleep(500);
  check("no JavaScript errors in the web client", errors.length === 0, errors.join(";"));
  console.log(`\n${pass}/${pass + fail} motion and rounding checks passed`); await browser.close(); process.exit(fail ? 1 : 0);
})().catch((e) => { console.log("crashed", e); process.exit(2); });
