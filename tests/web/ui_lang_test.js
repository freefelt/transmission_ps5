const puppeteer = require("puppeteer-core");
let pass = 0, fail = 0; const check = (n, ok, e) => { (ok ? pass++ : fail++); console.log((ok ? "PASS " : "FAIL ") + n + (e !== undefined ? "  -> " + String(e).slice(0, 260) : "")); };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
// Language selection in real Chrome: the English page by default, the list with a file from the folder, choosing, saving, the page reloads in the new language.
(async () => {
  const browser = await puppeteer.launch({ executablePath: require("./chrome"), args: ["--no-sandbox", "--disable-gpu"], headless: "new" });
  const page = await browser.newPage(); await page.setViewport({ width: 900, height: 1300 }); const errors = []; page.on("pageerror", (e) => errors.push(e.message)); page.on("dialog", (d) => d.dismiss());
  const open = async () => { await page.goto("http://127.0.0.1:9092/", { waitUntil: "networkidle2" }); await sleep(2600); };
  await open();
  const h = await page.evaluate(() => ({ lang: document.documentElement.lang, h2: [...document.querySelectorAll("h2")].map((x) => x.textContent), title: document.title, cyr: (document.body.innerText.match(/[\u0400-\u04FF]+/g) || []).slice(0, 8) }));
  check("ENGLISH page in Chrome (the default): lang=en, English headings and title, no JavaScript errors", h.lang === "en" && h.h2.includes("Download folders") && /settings/i.test(h.title) && errors.length === 0, JSON.stringify({ l: h.lang, h2: h.h2, t: h.title, e: errors }));
  check("the only Russian words left on the English page are the bilingual language heading and the language names in the list", h.cyr.every((w) => ["Язык", "Русский"].includes(w)), JSON.stringify(h.cyr));
  const opts = async () => page.evaluate(() => { const s = document.getElementById("f_language"); return s ? { val: s.value, o: [...s.options].map((x) => x.value + "=" + x.textContent) } : null; });
  await page.evaluate(() => { const b = document.getElementById("accSettingsBtn"); if (b.getAttribute("aria-expanded") !== "true") b.click(); }); await sleep(700);
  let o = await opts();
  check("the language list shows Русский, English and the Spanish test file; English is selected", o && o.val === "en" && o.o.includes("ru=Русский") && o.o.includes("en=English") && o.o.includes("file:es.strings=Español"), JSON.stringify(o));
  await page.select("#f_language", "file:es.strings"); await sleep(400);
  check("a changed language makes the Save button available", await page.evaluate(() => !document.getElementById("save").disabled && document.getElementById("saveBar").style.display !== "none"));
  await page.click("#save"); await sleep(3500);
  const es = await page.evaluate(() => ({ lang: document.documentElement.lang, h2: [...document.querySelectorAll("h2")].map((x) => x.textContent), save: document.getElementById("save").textContent, html: document.documentElement.innerHTML }));
  check("after Save the page reloads by itself in the chosen language (Spanish texts, English for the untranslated ones)", es.lang === "es" && es.h2.includes("Carpetas de descarga") && es.save === "Guardar" && es.h2.includes("When all downloads finish") && es.html.includes("Notifications") && !es.html.includes("Уведомления"), JSON.stringify(es));
  await page.evaluate(() => { const b = document.getElementById("accSettingsBtn"); if (b.getAttribute("aria-expanded") !== "true") b.click(); }); await sleep(700);
  await page.select("#f_language", "ru"); await sleep(400); await page.click("#save"); await sleep(3500);
  const ru = await page.evaluate(() => ({ lang: document.documentElement.lang, h2: [...document.querySelectorAll("h2")].map((x) => x.textContent) }));
  check("and back to Russian: the original page", ru.lang === "ru" && ru.h2.includes("Папки загрузок"), JSON.stringify(ru));
  check("no JavaScript errors in any language", errors.length === 0, errors.join(";"));
  console.log(`\n${pass}/${pass + fail} language selection checks passed`); await browser.close(); process.exit(fail ? 1 : 0);
})().catch((e) => { console.log("crashed", e); process.exit(2); });
