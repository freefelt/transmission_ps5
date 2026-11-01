const puppeteer = require("puppeteer-core");
let pass = 0, fail = 0; const check = (n, ok, e) => { (ok ? pass++ : fail++); console.log((ok ? "PASS " : "FAIL ") + n + (e !== undefined ? "  -> " + String(e).slice(0, 220) : "")); };
const PS5_UA = "Mozilla/5.0 (PlayStation; PlayStation 5/11.20) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/15.4 Safari/605.1.15";
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
// Transmission web interface against the real daemon (download folder guard): error texts, "Use temporary folder", the file picker on the console.
(async () => {
  const browser = await puppeteer.launch({ executablePath: require("./chrome"), args: ["--no-sandbox", "--disable-gpu"], headless: "new" });
  const open = async (ua, touch) => { const page = await browser.newPage(); if (touch) await page.evaluateOnNewDocument(() => { Object.defineProperty(navigator, "maxTouchPoints", { get: () => 5, configurable: true }); }); await page.setViewport({ width: 1280, height: 800 }); if (ua) await page.setUserAgent(ua); const dialogs = []; page.on("dialog", async (d) => { dialogs.push(d.message()); await d.accept(); }); const errors = []; page.on("pageerror", (e) => errors.push(e.message)); await page.goto("http://127.0.0.1:9091/transmission/web/", { waitUntil: "networkidle2" }); await sleep(2500); return { page, dialogs, errors }; };
  const fileState = (page) => page.evaluate(() => { const i = document.querySelector('input[type="file"]'); const lab = i && i.id ? document.querySelector('label[for="' + i.id + '"]') : null; const url = document.querySelector('input[type="url"]'); const next = i && i.nextElementSibling; return { exists: !!i, shown: !!i && getComputedStyle(i).display !== "none" && i.getBoundingClientRect().width > 0, labelShown: !!lab && getComputedStyle(lab).display !== "none", urlShown: !!url && getComputedStyle(url).display !== "none", urlLabel: next ? next.textContent : "", cls: document.documentElement.className, accept: i ? i.getAttribute("accept") : null }; });
  // ---- ordinary browser
  let { page, dialogs, errors } = await open();
  await page.click("#toolbar-open"); await sleep(600);
  let f = await fileState(page);
  check("a normal browser: the .torrent file chooser is shown, no ps5-console class, the .torrent filter is kept", f.exists && f.shown && f.labelShown && !/ps5-console/.test(f.cls) && f.accept === ".torrent,application/x-bittorrent", JSON.stringify(f));
  await page.evaluate(() => { document.querySelector('input[type="url"]').value = "magnet:?xt=urn:btih:0123456789abcdef0123456789abcdef01234567"; document.getElementById("add-dialog-folder-input").value = "/nowhere"; const b = [...document.querySelectorAll("#mainwin ~ * button, body > div button, .dialog-buttons button, button")].find((x) => /^\s*Add\s*$/.test(x.textContent)); if (b) b.click(); });
  await sleep(1800);
  const body = await page.evaluate(() => document.body.innerText);
  check("adding to a folder that is NOT in the list: the web UI shows OUR message (not the bare 'Invalid params')", /is not in the list of download folders/.test(body) && !/^Invalid params$/m.test(body.split("Error adding")[1] || ""), body.slice(body.indexOf("Error adding"), body.indexOf("Error adding") + 220));
  const call = (method, params) => page.evaluate(async (method, params) => { let sid = ""; for (let i = 0; i < 2; i++) { const r = await fetch("../rpc", { method: "POST", headers: { "X-Transmission-Session-Id": sid, "Content-Type": "application/json" }, body: JSON.stringify({ jsonrpc: "2.0", method, params, id: 1 }) }); if (r.status === 409) { sid = r.headers.get("X-Transmission-Session-Id"); continue; } return await r.json(); } }, method, params);
  const j = await call("session_set", { incomplete_dir_enabled: true });
  check("an error of session-set (Preferences) is no longer silent: the message is shown in an alert, errorString is added to the reply", j.error && j.error.data.errorString === j.error.data.error_string && /The temporary folder is disabled/.test(dialogs.join("|")), JSON.stringify(dialogs));
  const n0 = dialogs.length; const ok = await call("session_get", {});
  check("a normal reply is left untouched and no alert appears for it", ok && ok.result && !ok.error && dialogs.length === n0, JSON.stringify(Object.keys(ok || {})));
  const loc = await call("torrent_set_location", { ids: [1], location: "/nowhere", move: false });
  check("an error of torrent-set-location is shown too (the Move dialog used to say nothing)", loc.error && /is not in the list of download folders/.test(dialogs.join("|")), JSON.stringify(loc.error || loc).slice(0, 120));
  // ---- Preferences: "Use temporary folder" removed
  await page.keyboard.press("Escape"); await sleep(300);
  await page.click("#toolbar-overflow"); await sleep(400);
  const prefs = await page.evaluateHandle(() => [...document.querySelectorAll(".popup button, .popup a, .popup [role=menuitem]")].find((e) => /Preferences/i.test(e.textContent)));
  if (prefs.asElement()) { await prefs.asElement().click(); await sleep(900); }
  const pr = await page.evaluate(() => { const g = (s) => { const e = document.querySelector(s); return e ? getComputedStyle(e).display : "missing"; }; return { row: g("#incomplete-dir-div"), input: g('input[data-key="incomplete_dir"]'), downloadTo: !!document.querySelector('input[data-key="download_dir"]') && (getComputedStyle(document.querySelector('input[data-key="download_dir"]')).display !== "none" || document.querySelectorAll(".prefs-torrents-page .ps5-tile").length > 0), text: document.body.innerText.includes("Download to:") }; });
  check("Preferences: 'Use temporary folder' and its path field are hidden; 'Download to:' stays (the folder tiles stand in for its text field)", pr.row === "none" && pr.input === "none" && pr.downloadTo && pr.text, JSON.stringify(pr));
  check("no JS errors in the normal browser", errors.length === 0, errors.join(";"));
  await page.close();
  // ---- PS5 console browser (User-Agent "PlayStation")
  ({ page, dialogs, errors } = await open(PS5_UA));
  await page.click("#toolbar-open"); await sleep(600);
  f = await fileState(page);
  check("the PS5 browser (User-Agent has 'PlayStation'): the .torrent file chooser and its label are HIDDEN, class ps5-console is set", f.exists && !f.shown && !f.labelShown && /ps5-console/.test(f.cls), JSON.stringify(f));
  check("... the URL field stays and its label no longer says 'Or enter a URL' (it says 'Enter a URL or magnet link:')", f.urlShown && f.urlLabel === "Enter a URL or magnet link:", f.urlLabel);
  check("no JS errors in the PS5 browser case", errors.length === 0, errors.join(";"));
  // ---- iPad Safari in desktop mode: the User-Agent is "Macintosh" (no iPad/iPhone), but the screen is a touch screen
  const IPAD_DESKTOP_UA = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/17.0 Safari/605.1.15";
  ({ page, dialogs, errors } = await open(IPAD_DESKTOP_UA, true));
  await page.click("#toolbar-open"); await sleep(600);
  f = await fileState(page);
  check("iPad Safari (Macintosh UA + touch): the file input has NO accept filter (otherwise the iOS Files picker greys out .torrent files)", f.exists && f.shown && f.accept === null, JSON.stringify(f));
  check("no JS errors in the iPad Safari case", errors.length === 0, errors.join(";"));
  await page.close();
  // ---- adding a .torrent FILE that fails: one Transmission window, no second browser window (alert)
  const fs = require("fs"), tfile = "/tmp/ps5_test_upload.torrent";
  fs.writeFileSync(tfile, Buffer.concat([Buffer.from("d8:announce27:http://127.0.0.1:1/announce4:infod6:lengthi65536e4:name4:test12:piece lengthi65536e6:pieces20:"), Buffer.alloc(20, 7), Buffer.from("ee")]));
  ({ page, dialogs, errors } = await open());
  await page.click("#toolbar-open"); await sleep(600);
  const fileInput = await page.$('input[type="file"]'); await fileInput.uploadFile(tfile);
  await page.evaluate(() => { document.getElementById("add-dialog-folder-input").value = "/nowhere"; const b = [...document.querySelectorAll("button")].find((x) => /^\s*Add\s*$/.test(x.textContent)); if (b) b.click(); });
  await sleep(2200);
  const text2 = await page.evaluate(() => document.body.innerText), shown = (text2.match(/is not in the list of download folders/g) || []).length;
  check("adding a .torrent FILE to a folder not in the list: exactly ONE message (Transmission's own window), no second browser alert", dialogs.filter((d) => /^Error adding/.test(d)).length === 0 && shown === 1 && /Error adding/.test(text2), JSON.stringify({ browserAlerts: dialogs, shownInPage: shown }));
  await page.close();
  console.log(`\n${pass}/${pass + fail} web UI console checks passed`); await browser.close(); process.exit(fail ? 1 : 0);
})().catch((e) => { console.log("crashed", e); process.exit(2); });
