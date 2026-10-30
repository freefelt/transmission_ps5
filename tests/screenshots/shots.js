// Takes the screenshots of the README in Chrome (called by make_screenshots.py): node shots.js <en|ru> <output folder>
const path = require("path");
const puppeteer = require(path.join(__dirname, "..", "web", "node_modules", "puppeteer-core"));
const chrome = require("../web/chrome");
const [lang, out] = process.argv.slice(2);
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
(async () => {
  const browser = await puppeteer.launch({ executablePath: chrome, args: ["--no-sandbox", "--disable-gpu", "--font-render-hinting=none"], headless: "new" });
  const page = await browser.newPage(); const ev = (f, ...a) => page.evaluate(f, ...a);
  // the address of the machine is replaced by a typical home address in the pictures
  let realIp = null;
  const mask = async () => { if (!realIp) realIp = await ev(async () => { try { return (await (await fetch("http://127.0.0.1:9092/api/state")).json()).ip; } catch (e) { return null; } }); if (realIp) await ev((ip) => { const w = document.createTreeWalker(document.body, NodeFilter.SHOW_TEXT); let n; while ((n = w.nextNode())) if (n.nodeValue.indexOf(ip) >= 0) n.nodeValue = n.nodeValue.split(ip).join("192.168.1.42"); }, realIp); };
  const shot = async (name, opts = {}) => { await mask(); await page.screenshot({ path: `${out}/${lang}-${name}.png`, ...opts }); console.log("shot", name); };
  // ---- the settings page
  await page.setViewport({ width: 980, height: 900, deviceScaleFactor: 1.5 });
  await page.goto("http://127.0.0.1:9092/", { waitUntil: "networkidle2" }); await sleep(2500);
  await shot("settings-top", { clip: { x: 0, y: 0, width: 980, height: 900 } });
  const card = await page.$("#fdCard"); await card.evaluate((e) => e.scrollIntoView({ block: "center" })); await sleep(600);
  await mask(); await card.screenshot({ path: `${out}/${lang}-folders.png` }); console.log("shot folders");
  await ev(() => document.getElementById("fdShowAdd").click()); await sleep(1500);
  const modal = await page.$("#fdPickModal .modal-card"); await modal.screenshot({ path: `${out}/${lang}-folder-picker.png` }); console.log("shot picker");
  await ev(() => document.getElementById("pkCancel") && document.getElementById("pkCancel").click()); await sleep(500);
  await page.goto("http://127.0.0.1:9092/", { waitUntil: "networkidle2" }); await sleep(1500);
  await page.evaluate(() => window.scrollTo(0, 0)); await sleep(400);
  await ev(() => document.getElementById("bell").click()); await sleep(1200);
  await shot("notifications", { clip: { x: 0, y: 0, width: 980, height: 620 } });
  // ---- the Transmission web interface
  await page.setViewport({ width: 1240, height: 780, deviceScaleFactor: 1.5 });
  await page.goto("http://127.0.0.1:9091/transmission/web/", { waitUntil: "networkidle2" }); await sleep(5000);
  await shot("web-ui");
  await ev(() => { const r = document.querySelector("#torrent-list .torrent"); r && r.click(); }); await sleep(500);
  await ev(() => document.getElementById("toolbar-open") && document.getElementById("toolbar-open").click()); await sleep(2200);
  await shot("add-dialog");
  await browser.close();
})().catch((e) => { console.log("crashed", e); process.exit(2); });
