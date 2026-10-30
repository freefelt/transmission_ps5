const puppeteer = require("puppeteer-core");
const http = require("http");
let pass = 0, fail = 0; const check = (n, ok, e) => { (ok ? pass++ : fail++); console.log((ok ? "PASS " : "FAIL ") + n + (e !== undefined ? "  -> " + String(e).slice(0, 260) : "")); };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const post = (body) => new Promise((res, rej) => { const d = JSON.stringify(body); const r = http.request({ host: "127.0.0.1", port: 9092, path: "/api/config", method: "POST", headers: { "Content-Type": "application/json", "Content-Length": Buffer.byteLength(d) } }, (x) => { let b = ""; x.on("data", (c) => (b += c)); x.on("end", () => res({ status: x.statusCode, body: b })); }); r.on("error", rej); r.end(d); });
// The "Open folder" item in the torrent context menu: off / BFpilot (5905) / PS5 Web File Manager (8888); a tab in the browser, the same window on the console.
(async () => {
  const browser = await puppeteer.launch({ executablePath: require("./chrome"), args: ["--no-sandbox", "--disable-gpu"], headless: "new" });
  const mk = async (ua) => { const p = await browser.newPage(); await p.setViewport({ width: 1100, height: 900 }); if (ua) await p.setUserAgent(ua); return p; };
  const page = await mk(); const errors = []; page.on("pageerror", (e) => errors.push(e.message));
  const open = async (p) => { await p.goto("http://127.0.0.1:9091/transmission/web/", { waitUntil: "networkidle2" }); await sleep(2500); };
  const rpc = (p, method, params) => p.evaluate(async (m, a) => { let sid = ""; for (let i = 0; i < 3; i++) { const r = await fetch("../rpc", { method: "POST", headers: { "X-Transmission-Session-Id": sid, "Content-Type": "application/json" }, body: JSON.stringify({ jsonrpc: "2.0", method: m, params: a || {}, id: 1 }) }); if (r.status === 409) { sid = r.headers.get("X-Transmission-Session-Id"); continue; } return await r.json(); } }, method, params);
  const z = Buffer.alloc(20, 7).toString("latin1"), z2 = Buffer.alloc(40, 8).toString("latin1");
  const single = Buffer.from("d8:announce27:http://127.0.0.1:1/announce4:infod6:lengthi65536e4:name9:of_single12:piece lengthi65536e6:pieces20:" + z + "ee", "latin1").toString("base64");
  const infolder = Buffer.from("d8:announce27:http://127.0.0.1:1/announce4:infod5:filesld6:lengthi65536e4:pathl8:data.binee" + "e4:name11:of_infolder12:piece lengthi65536e6:pieces20:" + z + "ee", "latin1").toString("base64");
  const multi = Buffer.from("d8:announce27:http://127.0.0.1:1/announce4:infod5:filesld6:lengthi65536e4:pathl5:a.binee" + "d6:lengthi65536e4:pathl5:b.binee" + "e4:name8:of_multi12:piece lengthi65536e6:pieces40:" + z2 + "ee", "latin1").toString("base64");
  await open(page);
  for (const t of (await rpc(page, "torrent_get", { fields: ["id"] })).result.torrents) await rpc(page, "torrent_remove", { ids: [t.id], delete_local_data: true });
  await rpc(page, "torrent_add", { metainfo: single, download_dir: "/mnt/ext1/torrents", paused: true });
  await rpc(page, "torrent_add", { metainfo: multi, download_dir: "/mnt/ext1/torrents", paused: true });
  await rpc(page, "torrent_add", { metainfo: infolder, download_dir: "/mnt/ext1/torrents", paused: true });
  const names = (await rpc(page, "torrent_get", { fields: ["name", "file_count"] })).result.torrents.map((t) => t.name + ":" + t.file_count).sort();
  check("setup: a plain single-file, a one-file-in-a-folder and a two-file torrent in /mnt/ext1/torrents", names.join() === "of_infolder:1,of_multi:2,of_single:1", names.join());
  const rightClick = async (p, name) => { const b = await p.evaluate((n) => { const li = [...document.querySelectorAll("#torrent-list .torrent")].find((x) => x.textContent.includes(n)); li.scrollIntoView(); const r = li.getBoundingClientRect(); return { x: r.left + 60, y: r.top + 20 }; }, name); await p.mouse.click(b.x, b.y, { button: "right" }); await sleep(500); };
  const menu = (p) => p.evaluate(() => { const m = document.querySelector(".context-menu"); if (!m) return null; const it = [...m.querySelectorAll(".context-menuitem")]; const i = it.findIndex((e) => e.classList.contains("ps5-openfolder")); return { n: it.length, idx: i, text: i >= 0 ? it[i].textContent : "", prev: i > 0 ? it[i - 1].textContent : "" }; });
  const closeMenu = async (p) => { await p.keyboard.press("Escape"); await p.evaluate(() => document.body.click()); await sleep(300); };
  // --- off
  await post({ web_openfolder: "off" }); await open(page); await rightClick(page, "of_single");
  let mn = await menu(page);
  check("setting 'Не показывать': the torrent menu has no 'Open folder' item", mn && mn.idx === -1, JSON.stringify(mn));
  await closeMenu(page);
  // --- BFpilot
  await post({ web_openfolder: "bfpilot" }); await open(page);
  await page.evaluate(() => { window.__opened = []; window.open = (u, t, f) => { window.__opened.push([u, t, f]); return null; }; });
  await rightClick(page, "of_single"); mn = await menu(page);
  check("'Открывать в BFpilot': the item 'Open folder' is in the menu, right after 'Set location…'", mn && mn.idx >= 0 && mn.text === "Open folder" && /location/i.test(mn.prev), JSON.stringify(mn));
  await page.evaluate(() => document.querySelector(".ps5-openfolder").click()); await sleep(400);
  let op = await page.evaluate(() => window.__opened);
  check("single-file torrent: opens its folder in BFpilot (port 5905) in a NEW window/tab: http://<console>:5905/#%2Fmnt%2Fext1%2Ftorrents", op.length === 1 && op[0][0] === "http://127.0.0.1:5905/#%2Fmnt%2Fext1%2Ftorrents" && op[0][1] === "_blank", JSON.stringify(op));
  check("the menu closes after the click", (await menu(page)) === null);
  await rightClick(page, "of_multi"); await page.evaluate(() => document.querySelector(".ps5-openfolder").click()); await sleep(400);
  op = await page.evaluate(() => window.__opened);
  check("multi-file torrent: opens ITS OWN folder (download folder + torrent name)", op.length === 2 && op[1][0] === "http://127.0.0.1:5905/#%2Fmnt%2Fext1%2Ftorrents%2Fof_multi", JSON.stringify(op[1]));
  // --- torrent with a SINGLE file in a folder: the folder icon and "Open folder" open the folder of the torrent, not the download folder
  const icons = await page.evaluate(() => Object.fromEntries([...document.querySelectorAll("#torrent-list .torrent")].map((li) => [li.querySelector(".torrent-name") ? li.querySelector(".torrent-name").textContent.trim() : li.textContent.trim().slice(0, 12), li.querySelector(".icon") ? li.querySelector(".icon").dataset.iconMultifile : "?"])));
  check("ONE FILE IN A FOLDER: the torrent gets the FOLDER icon like a multi-file one (data-icon-multifile true); a plain single file keeps the file icon (false)", icons.of_infolder === "true" && icons.of_multi === "true" && icons.of_single === "false", JSON.stringify(icons));
  await rightClick(page, "of_infolder"); await page.evaluate(() => document.querySelector(".ps5-openfolder").click()); await sleep(400);
  op = await page.evaluate(() => window.__opened);
  check("ONE FILE IN A FOLDER: 'Open folder' opens the folder of the torrent itself (download folder + torrent name), not the parent download folder", op.length === 3 && op[2][0] === "http://127.0.0.1:5905/#%2Fmnt%2Fext1%2Ftorrents%2Fof_infolder", JSON.stringify(op[2]));
  // --- toolbar button (for the console browser, where the context menu is not available)
  await open(page);
  await page.evaluate(() => { window.__opened = []; window.open = (u, t, f) => { window.__opened.push([u, t, f]); return null; }; });
  const tb = await page.evaluate(() => { const b = document.getElementById("toolbar-openfolder"); if (!b) return null; const p = b.previousElementSibling, pp = p && p.previousElementSibling, n = b.nextElementSibling, sv = b.querySelector("svg"), cs = sv ? getComputedStyle(sv) : null; return { sep: p && p.className, before: pp && pp.id, next: n && n.className, text: b.textContent.trim(), title: b.title, svg: !!sv, disabled: b.disabled, w: Math.round(sv ? sv.getBoundingClientRect().width : 0), mask: cs ? ([cs.maskImage, cs.webkitMaskImage, cs.backgroundImage].find((v) => v && v !== "none") || "none") : "", sepBefore: (() => { const i = document.getElementById("toolbar-inspector"); const q = i.previousElementSibling; return q && q.className; })() }; });
  check("TOOLBAR BUTTON: it stands after Inspector and a separator (like the one before Inspector), before the flexible space; label 'Folder', tooltip 'Open folder', has an icon of the same size as the others", tb && tb.sep === "toolbar-separator" && tb.before === "toolbar-inspector" && tb.sepBefore === "toolbar-separator" && /flexible-space/.test(tb.next || "") && tb.text === "Folder" && tb.title === "Open folder" && tb.svg && tb.w >= 20, JSON.stringify(tb));
  check("TOOLBAR BUTTON: the icon comes from the icon set (a mask or image is set on its svg) and the button is disabled while no torrent is selected", tb && tb.mask && tb.mask !== "none" && tb.disabled === true, JSON.stringify(tb && [tb.mask.slice(0, 40), tb.disabled]));
  await page.evaluate(() => { const li = [...document.querySelectorAll("#torrent-list .torrent")].find((x) => x.textContent.includes("of_multi")); li.click(); }); await sleep(500);
  check("TOOLBAR BUTTON: selecting a torrent enables it", (await page.evaluate(() => document.getElementById("toolbar-openfolder").disabled)) === false);
  await page.evaluate(() => document.getElementById("toolbar-openfolder").click()); await sleep(300);
  op = await page.evaluate(() => window.__opened);
  check("TOOLBAR BUTTON: a click opens the folder of the SELECTED torrent in BFpilot in a new window (multi-file: its own folder)", op.length === 1 && op[0][0] === "http://127.0.0.1:5905/#%2Fmnt%2Fext1%2Ftorrents%2Fof_multi" && op[0][1] === "_blank", JSON.stringify(op));
  await post({ web_openfolder: "off" }); await open(page);
  check("setting 'Не показывать': no toolbar button either", (await page.evaluate(() => !document.getElementById("toolbar-openfolder"))), null);
  await post({ web_openfolder: "bfpilot" }); await open(page);
  // --- PS5 Web File Manager
  await post({ web_openfolder: "webfm" }); await open(page);
  await page.evaluate(() => { window.__opened = []; window.open = (u, t, f) => { window.__opened.push([u, t, f]); return null; }; });
  await rightClick(page, "of_multi"); await page.evaluate(() => document.querySelector(".ps5-openfolder").click()); await sleep(400);
  op = await page.evaluate(() => window.__opened);
  check("'Открывать PS5 Web File Manager': port 8888", op.length === 1 && op[0][0] === "http://127.0.0.1:8888/#%2Fmnt%2Fext1%2Ftorrents%2Fof_multi", JSON.stringify(op));
  // --- console: the same window
  const cp = await mk("Mozilla/5.0 (PlayStation; PlayStation 5/9.00) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/15.0 Safari/605.1.15"); cp.on("pageerror", (e) => errors.push(e.message));
  await open(cp);
  await cp.setRequestInterception(true); const navs = [];
  cp.on("request", (r) => { if (r.isNavigationRequest() && r.frame() === cp.mainFrame()) { navs.push(r.url()); r.respond({ status: 200, contentType: "text/html", body: "<title>file manager</title>" }); } else r.continue(); });
  await rightClick(cp, "of_single"); await cp.evaluate(() => { window.__w = 0; window.open = () => { window.__w = 1; return null; }; document.querySelector(".ps5-openfolder").click(); }); await sleep(1200);
  check("on the CONSOLE browser (User-Agent has PlayStation) the SAME window navigates to the file manager with the folder in the address (no new window; a request never carries the #fragment, the window address does)", navs.length === 1 && navs[0] === "http://127.0.0.1:8888/" && cp.url() === "http://127.0.0.1:8888/#%2Fmnt%2Fext1%2Ftorrents" && (await browser.pages()).length === 3, JSON.stringify({ navs, url: cp.url(), pages: (await browser.pages()).length }));
  const cp2 = await mk("Mozilla/5.0 (PlayStation; PlayStation 5/9.00) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/15.0 Safari/605.1.15"); cp2.on("pageerror", (e) => errors.push(e.message));
  await post({ web_openfolder: "webfm" }); await open(cp2); await cp2.setRequestInterception(true); const navs2 = [];
  cp2.on("request", (r) => { if (r.isNavigationRequest() && r.frame() === cp2.mainFrame()) { navs2.push(r.url()); r.respond({ status: 200, contentType: "text/html", body: "<title>file manager</title>" }); } else r.continue(); });
  await cp2.evaluate(() => { const li = [...document.querySelectorAll("#torrent-list .torrent")].find((x) => x.textContent.includes("of_single")); li.click(); }); await sleep(500);
  await cp2.evaluate(() => document.getElementById("toolbar-openfolder").click()); await sleep(1200);
  check("TOOLBAR BUTTON on the CONSOLE browser (this is where the context menu is not available): the same window goes to the file manager with the folder in the address", navs2.length === 1 && navs2[0] === "http://127.0.0.1:8888/" && cp2.url() === "http://127.0.0.1:8888/#%2Fmnt%2Fext1%2Ftorrents", JSON.stringify({ navs2, url: cp2.url() }));
  await post({ web_openfolder: "off" });
  for (const t of (await rpc(page, "torrent_get", { fields: ["id"] })).result.torrents) await rpc(page, "torrent_remove", { ids: [t.id], delete_local_data: true });
  check("no JS errors", errors.length === 0, errors.join(";"));
  console.log(`\n${pass}/${pass + fail} open folder menu checks passed`); await browser.close(); process.exit(fail ? 1 : 0);
})().catch((e) => { console.log("crashed", e); process.exit(2); });
