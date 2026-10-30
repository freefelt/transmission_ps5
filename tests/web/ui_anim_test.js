const puppeteer = require("puppeteer-core");
let pass = 0, fail = 0; const check = (n, ok, e) => { (ok ? pass++ : fail++); console.log((ok ? "PASS " : "FAIL ") + n + (e !== undefined ? "  -> " + String(e).slice(0, 240) : "")); };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
// Real Chrome on the manager's settings page: during an animation the height of a section GROWS (or shrinks) smoothly instead of jumping.
(async () => {
  const browser = await puppeteer.launch({ executablePath: require("./chrome"), args: ["--no-sandbox", "--disable-gpu"], headless: "new" });
  const page = await browser.newPage(); await page.setViewport({ width: 900, height: 1100 }); const errors = []; page.on("pageerror", (e) => errors.push(e.message));
  await page.goto("http://127.0.0.1:9092/", { waitUntil: "networkidle2" }); await sleep(2600);   // the first 2 s have no animation
  const H = (sel) => page.evaluate((s) => { const e = document.querySelector(s); return e ? Math.round(e.getBoundingClientRect().height * 10) / 10 : -1; }, sel);
  const D = (sel) => page.evaluate((s) => getComputedStyle(document.querySelector(s)).display, sel);
  const sampleAfterClick = async (clickSel, boxSel, times) => { const out = []; const t0 = Date.now(); await page.evaluate((s) => document.querySelector(s).click(), clickSel); for (const t of times) { const wait = t - (Date.now() - t0); if (wait > 0) await sleep(wait); out.push(await H(boxSel)); } return out; };
  const growing = (a) => a.every((x, i) => i === 0 || x >= a[i - 1] - 0.5), shrinking = (a) => a.every((x, i) => i === 0 || x <= a[i - 1] + 0.5);
  // 1) accordion section
  check("the 'Настройки' section is closed at the start (height 0, display none)", (await H("#accSettingsBody")) === 0 && (await D("#accSettingsBody")) === "none");
  let s = await sampleAfterClick("#accSettingsBtn", "#accSettingsBody", [40, 90, 140, 420]); const full = s[3];
  check("OPENING: the height grows step by step (strictly between 0 and the full height in the middle of the animation), then reaches the natural height", full > 150 && s[0] < full && s[1] > 0 && s[1] < full && growing(s) && s[0] < s[2], JSON.stringify(s));
  s = await sampleAfterClick("#accSettingsBtn", "#accSettingsBody", [40, 90, 140, 420]);
  check("CLOSING: the height shrinks step by step and the section ends hidden (display none, height 0)", s[0] > 0 && s[0] < full + 1 && shrinking(s) && s[3] === 0 && (await D("#accSettingsBody")) === "none", JSON.stringify(s));
  // 2) a row that depends on a checkbox
  await page.evaluate(() => document.querySelector("#accSettingsBtn").click()); await sleep(400);
  const rowFull = await H("#f_row_low_disk_warn_gb");
  s = await sampleAfterClick("#f_notify_low_disk_space", "#f_row_low_disk_warn_gb", [40, 90, 140, 420]);
  check("a row hidden by a checkbox collapses smoothly (not instantly) and ends hidden", rowFull > 20 && s[0] > 0 && s[0] < rowFull && shrinking(s) && s[3] === 0 && (await D("#f_row_low_disk_warn_gb")) === "none", JSON.stringify([rowFull, ...s]));
  s = await sampleAfterClick("#f_notify_low_disk_space", "#f_row_low_disk_warn_gb", [40, 90, 140, 420]);
  check("... and appears smoothly again, ending at its natural height", s[0] < rowFull && growing(s) && Math.abs(s[3] - rowFull) <= 1.5, JSON.stringify([rowFull, ...s]));
  // 3) the list of the download folders: adding or removing a folder changes the height of the list smoothly (the card grows and shrinks, the things below slide)
  const post = (body) => page.evaluate(async (b) => (await fetch("/api/folders", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(b) })).status, body);
  const sampleFolders = async (act) => { const base = await H("#fdList"); const out = []; const t0 = Date.now(); await act(); for (const t of [40, 90, 140, 420]) { const wait = t - (Date.now() - t0); if (wait > 0) await sleep(wait); out.push(await H("#fdList")); } return { base, out }; };
  let fr = await sampleFolders(async () => { await post({ action: "add", path: "/mnt/ext1/animtest" }); await page.evaluate(() => fdRefresh()); });
  check("ADDING a folder: the list grows step by step to its new height (it does not jump)", fr.out[3] > fr.base + 20 && fr.out[0] >= fr.base - 0.5 && fr.out[1] > fr.base && fr.out[1] < fr.out[3] && growing(fr.out), JSON.stringify(fr));
  const grown = fr.out[3];
  fr = await sampleFolders(async () => { await post({ action: "remove", path: "/mnt/ext1/animtest" }); await page.evaluate(() => fdRefresh()); });
  check("REMOVING a folder: the list shrinks step by step to its old height", fr.base > fr.out[3] + 20 && fr.out[1] < fr.base && fr.out[1] > fr.out[3] && shrinking(fr.out), JSON.stringify(fr));
  fr = await sampleFolders(async () => { await page.evaluate(() => fdRefresh()); });
  check("a refresh with the same folders changes nothing (no flicker of the height)", fr.out.every((x) => Math.abs(x - fr.base) <= 1), JSON.stringify(fr));
  // the picker is a popup (checked by ui_picker_test.js)
  // 4) after all the animations no temporary styles are left on the page
  const left = await page.evaluate(() => [...document.querySelectorAll("[style]")].filter((e) => /transition|overflow: hidden/.test(e.getAttribute("style")) && e.id !== "dgOut").map((e) => e.id || e.className));
  check("no temporary animation styles are left on the page afterwards, no JS errors", left.length === 0 && errors.length === 0, JSON.stringify({ left, errors }));
  console.log(`\n${pass}/${pass + fail} animation checks passed`); await browser.close(); process.exit(fail ? 1 : 0);
})().catch((e) => { console.log("crashed", e); process.exit(2); });
