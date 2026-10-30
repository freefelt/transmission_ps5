const puppeteer = require("puppeteer-core");
let pass = 0, fail = 0; const check = (n, ok, e) => { (ok ? pass++ : fail++); console.log((ok ? "PASS " : "FAIL ") + n + (e !== undefined ? "  -> " + String(e).slice(0, 170) : "")); };
// Other Transmission lists (dialogs: preferences, the "Open torrent" window) get the same look: we check that they are not broken
(async () => {
  const browser = await puppeteer.launch({ executablePath: require("./chrome"), args: ["--no-sandbox", "--disable-gpu"], headless: "new" });
  const page = await browser.newPage(); await page.setViewport({ width: 1280, height: 800 });
  const errors = []; page.on("pageerror", (e) => errors.push(e.message));
  await page.goto("http://127.0.0.1:9091/transmission/web/", { waitUntil: "networkidle2" }); await new Promise((r) => setTimeout(r, 2500));
  await page.click("#toolbar-overflow"); await new Promise((r) => setTimeout(r, 400));
  const menu = await page.evaluate(() => [...document.querySelectorAll(".popup button, .popup a, .popup [role=menuitem], .context-menu *")].map((e) => e.textContent.trim()).filter(Boolean).slice(0, 12));
  const prefs = await page.evaluateHandle(() => [...document.querySelectorAll(".popup button, .popup a, .popup [role=menuitem]")].find((e) => /Preferences/i.test(e.textContent)));
  if (prefs.asElement()) { await prefs.asElement().click(); await new Promise((r) => setTimeout(r, 900)); }
  const tabs = await page.evaluate(() => [...document.querySelectorAll(".tabs-button, .tabs-container button, .dialog-window button")].map((e) => e.textContent.trim()).filter(Boolean).slice(0, 14));
  const speed = await page.evaluateHandle(() => [...document.querySelectorAll(".tabs-button, .tabs-container button, .dialog-window label, .dialog-window button")].find((e) => /^Speed$/i.test(e.textContent.trim())));
  if (speed.asElement()) { await speed.asElement().click(); await new Promise((r) => setTimeout(r, 600)); }
  const dlg = await page.evaluate(() => { const sels = [...document.querySelectorAll(".dialog-container select, .dialog-window select, #prefs-dialog select")].filter((s) => s.getBoundingClientRect().width > 0); return sels.slice(0, 8).map((s) => { const cs = getComputedStyle(s), r = s.getBoundingClientRect(); return { w: Math.round(r.width), h: Math.round(r.height), app: cs.appearance, radius: parseFloat(cs.borderTopLeftRadius), text: s.options[s.selectedIndex] ? s.options[s.selectedIndex].text : "" }; }); });
  check("no JS errors", errors.length === 0, errors.join(";"));
  check("the Preferences dialog (Speed tab) shows dropdowns, drawn in our style: rounded, not collapsed (height >= 20px, width >= 60px)", dlg.length > 0 && dlg.every((s) => s.app === "none" && s.radius >= 8 && s.h >= 20 && s.w >= 60), JSON.stringify({ tabs, dlg }));
  console.log(`\n${pass}/${pass + fail} dialog checks passed`); await browser.close(); process.exit(fail ? 1 : 0);
})().catch((e) => { console.log("crashed", e); process.exit(2); });
