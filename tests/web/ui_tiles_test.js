const puppeteer = require("puppeteer-core");
const { execSync } = require("child_process");
let pass = 0, fail = 0; const check = (n, ok, e) => { (ok ? pass++ : fail++); console.log((ok ? "PASS " : "FAIL ") + n + (e !== undefined ? "  -> " + String(e).slice(0, 280) : "")); };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
// Folder tiles: the add window, "Set Torrent Location", Transmission preferences (Download to). Real Chrome against the real daemon.
(async () => {
  const browser = await puppeteer.launch({ executablePath: require("./chrome"), args: ["--no-sandbox", "--disable-gpu"], headless: "new" });
  const page = await browser.newPage(); await page.setViewport({ width: 1100, height: 1000 }); const errors = []; page.on("pageerror", (e) => errors.push(e.message));
  const adds = []; page.on("request", (r) => { const b = r.postData(); if (r.method() === "POST" && b && /torrent[_-]add/.test(b)) { try { adds.push(JSON.parse(b)); } catch (e) {} } });
  await page.goto("http://127.0.0.1:9091/transmission/web/", { waitUntil: "networkidle2" }); await sleep(2500);
  const rpc = (method, params) => page.evaluate(async (m, p) => { let sid = ""; for (let i = 0; i < 3; i++) { const r = await fetch("../rpc", { method: "POST", headers: { "X-Transmission-Session-Id": sid, "Content-Type": "application/json" }, body: JSON.stringify({ jsonrpc: "2.0", method: m, params: p || {}, id: 1 }) }); if (r.status === 409) { sid = r.headers.get("X-Transmission-Session-Id"); continue; } return await r.json(); } }, method, params);
  const list = (await rpc("ps5_folders")).result.folders; const sess = (await rpc("session_get", { fields: ["download_dir"] })).result.download_dir;
  const tiles = (root) => page.evaluate((sel) => [...document.querySelectorAll(sel + " .ps5-tile")].map((t) => ({ path: t.getAttribute("data-path"), cls: t.className, disabled: t.disabled, text: t.textContent, ico: t.querySelector(".ps5-disk-ico").className, w: Math.round(t.getBoundingClientRect().width), x: Math.round(t.getBoundingClientRect().left) })), root);
  const bg = Buffer.concat([Buffer.from("d8:announce27:http://127.0.0.1:1/announce4:infod6:lengthi65536e4:name9:tiles_one12:piece lengthi65536e6:pieces20:"), Buffer.alloc(20, 3), Buffer.from("ee")]).toString("base64");
  await rpc("torrent_add", { metainfo: bg, download_dir: "/mnt/ext1/torrents", paused: true });
  check("the server list has several folders to show (M.2, USB, internal)", list.length >= 3 && new Set(list.map((f) => f.kind)).size >= 2, JSON.stringify(list.map((f) => [f.path, f.kind, f.mounted])));
  // ---- add window
  await page.click("#toolbar-open"); await sleep(1200);
  let t = await tiles(".open-torrent");
  check("ADD dialog: one tile per folder of the list, in the order of the list (the plain text field is gone)", t.map((x) => x.path).join() === list.map((f) => f.path).join() && (await page.evaluate(() => getComputedStyle(document.getElementById("add-dialog-folder-input")).display)) === "none", t.map((x) => x.path));
  check("each tile shows the path, the type of disk and the free space ('M.2 SSD · … free of …'), with a disk icon of its kind", t.every((x, i) => x.text.includes(list[i].path) && /free of /.test(x.text) && x.ico.includes(list[i].kind)), t.map((x) => x.text.slice(0, 70)));
  check("the folder that is the default of Transmission is selected at once", t.filter((x) => /\bsel\b/.test(x.cls)).map((x) => x.path).join() === sess, JSON.stringify({ sess, sel: t.filter((x) => /\bsel\b/.test(x.cls)).map((x) => x.path) }));
  check("two columns: tiles are side by side and fit inside the dialog (no tile sticks out)", new Set(t.map((x) => x.x)).size === 2 && t.every((x) => x.w > 120), JSON.stringify(t.map((x) => [x.x, x.w])));
  const usb = list.find((f) => f.kind === "usb" && f.mounted && f.path.startsWith("/mnt/usb")), ssd = list.find((f) => f.kind === "ssd" && f.mounted);
  await page.evaluate((p) => document.querySelector('.ps5-tile[data-path="' + p + '"]').click(), usb.path); await sleep(500);
  const afterUsb = await page.evaluate(() => ({ v: document.getElementById("add-dialog-folder-input").value, row: getComputedStyle(document.getElementById("ps5-usbfix-row")).display }));
  check("clicking a USB tile puts its folder into the dialog and the 'Fix for USB drives' row appears", afterUsb.v === usb.path && afterUsb.row === "flex", JSON.stringify(afterUsb));
  await page.evaluate((p) => document.querySelector('.ps5-tile[data-path="' + p + '"]').click(), ssd.path); await sleep(500);
  const afterSsd = await page.evaluate(() => ({ v: document.getElementById("add-dialog-folder-input").value, row: getComputedStyle(document.getElementById("ps5-usbfix-row")).display, sel: [...document.querySelectorAll(".ps5-tile.sel")].map((x) => x.getAttribute("data-path")) }));
  check("clicking the M.2 tile selects only it and the USB row disappears", afterSsd.v === ssd.path && afterSsd.row === "none" && afterSsd.sel.join() === ssd.path, JSON.stringify(afterSsd));
  await page.evaluate(() => { const i = [...document.querySelectorAll(".open-torrent input[type=text], #torrent-upload-url, input[type=url], input[type=text]")].find((x) => x.id !== "add-dialog-folder-input" && x.getAttribute("data-key") !== "download_dir"); i.value = "magnet:?xt=urn:btih:" + "d".repeat(40); i.dispatchEvent(new Event("input", { bubbles: true })); const b = [...document.querySelectorAll("button")].find((x) => /^\s*Add\s*$/.test(x.textContent)); b.click(); }); await sleep(2000);
  const a = adds[adds.length - 1] && (adds[adds.length - 1].params || adds[adds.length - 1].arguments);
  check("'Add' sends exactly the folder of the selected tile", a && a.download_dir === ssd.path, JSON.stringify(a && a.download_dir));
  // ---- disconnected disk: a grey tile that cannot be chosen
  execSync("umount -l " + usb.path.split("/").slice(0, 3).join("/") + " 2>/dev/null || true"); await sleep(500);
  await page.click("#toolbar-open"); await sleep(1200); t = await tiles(".open-torrent"); const off = t.find((x) => x.path === usb.path);
  check("a DISCONNECTED disk: its tile is greyed out, disabled and says 'drive not connected' (not a fake free space)", off && /\boff\b/.test(off.cls) && off.disabled && /drive not connected/.test(off.text) && !/free of/.test(off.text), JSON.stringify(off && [off.cls, off.disabled, off.text]));
  const before = await page.evaluate(() => document.getElementById("add-dialog-folder-input").value);
  await page.evaluate((p) => document.querySelector('.ps5-tile[data-path="' + p + '"]').click(), usb.path); await sleep(300);
  check("clicking it changes nothing", (await page.evaluate(() => document.getElementById("add-dialog-folder-input").value)) === before);
  await page.evaluate(() => [...document.querySelectorAll("button")].find((x) => /^\s*Cancel\s*$/.test(x.textContent)).click()); await sleep(300);
  execSync("mount -t tmpfs -o size=128m tmpfs " + usb.path.split("/").slice(0, 3).join("/")); await sleep(300);
  // ---- «Set Torrent Location»
  const box = await page.evaluate(() => { const li = [...document.querySelectorAll("#torrent-list .torrent")].find((x) => /tiles_one/.test(x.textContent)); li.scrollIntoView(); const r = li.getBoundingClientRect(); return { x: r.left + 60, y: r.top + 20 }; });
  await page.mouse.click(box.x, box.y, { button: "right" }); await sleep(600);
  const opened = await page.evaluate(() => { const e = [...document.querySelectorAll(".context-menuitem")].find((x) => /set location/i.test(x.textContent)); if (!e) return false; e.click(); return true; }); await sleep(1200);
  t = await tiles(".move-dialog");
  check("SET LOCATION dialog: the same tiles, the text field is hidden, the torrent's CURRENT folder is highlighted", opened && t.length === list.length && t.filter((x) => /\bsel\b/.test(x.cls)).map((x) => x.path).join() === "/mnt/ext1/torrents" && (await page.evaluate(() => getComputedStyle(document.getElementById("torrent-path")).display)) === "none", JSON.stringify({ opened, n: t.length, sel: t.filter((x) => /\bsel\b/.test(x.cls)).map((x) => x.path) }));
  const target = list.find((f) => f.mounted && f.path !== "/mnt/ext1/torrents" && f.kind === "usb") || list.find((f) => f.mounted && f.path !== "/mnt/ext1/torrents");
  await page.evaluate((p) => document.querySelector('.move-dialog .ps5-tile[data-path="' + p + '"]').click(), target.path); await sleep(300);
  await page.evaluate(() => [...document.querySelectorAll(".move-dialog button")].find((x) => /^\s*(OK|Apply)\s*$/.test(x.textContent)).click()); await sleep(2500);
  const moved = (await rpc("torrent_get", { fields: ["name", "download_dir"] })).result.torrents.find((x) => x.name === "tiles_one");
  check("'OK' moves the torrent to the folder of the chosen tile", moved && moved.download_dir === target.path, JSON.stringify(moved));
  // ---- Transmission preferences: Download to
  await page.click("#toolbar-overflow"); await sleep(400);
  await page.evaluate(() => { const e = [...document.querySelectorAll(".popup button, .popup a, .popup [role=menuitem], .popup li")].find((x) => /Preferences/i.test(x.textContent)); e.click(); }); await sleep(1500);
  t = await tiles(".prefs-torrents-page");
  check("PREFERENCES 'Download to': the same tiles instead of the text field", t.length === list.length && (await page.evaluate(() => getComputedStyle(document.querySelector('input[data-key="download_dir"]')).display)) === "none", t.length);
  const pick = list.find((f) => f.mounted && f.path !== sess);
  await page.evaluate((p) => document.querySelector('.prefs-torrents-page .ps5-tile[data-path="' + p + '"]').click(), pick.path); await sleep(1800);
  const now = (await rpc("session_get", { fields: ["download_dir"] })).result.download_dir;
  check("clicking a tile there changes the default folder of Transmission to it", now === pick.path, now);
  check("no JS errors", errors.length === 0, errors.join(";"));
  console.log(`\n${pass}/${pass + fail} folder tiles checks passed`); await browser.close(); process.exit(fail ? 1 : 0);
})().catch((e) => { console.log("crashed", e); process.exit(2); });
