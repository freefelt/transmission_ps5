const puppeteer = require("puppeteer-core");
let pass = 0, fail = 0; const check = (n, ok, e) => { (ok ? pass++ : fail++); console.log((ok ? "PASS " : "FAIL ") + n + (e !== undefined ? "  -> " + String(e).slice(0, 240) : "")); };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
// Smooth appearance: the bell panel, new notifications inside it, warning popups (settings page) and windows on the Transmission page. Real Chrome.
(async () => {
  const browser = await puppeteer.launch({ executablePath: require("./chrome"), args: ["--no-sandbox", "--disable-gpu"], headless: "new" });
  const page = await browser.newPage(); await page.setViewport({ width: 900, height: 1100 }); const errors = []; page.on("pageerror", (e) => errors.push(e.message));
  const now = Math.floor(Date.now() / 1000), fake = { latest: 2, items: [{ time: now - 120, text: "Первое уведомление", kind: 1 }, { time: now - 60, text: "Второе уведомление", kind: 1 }] };
  await page.setRequestInterception(true);
  page.on("request", (r) => { if (r.method() === "GET" && /\/api\/notifications$/.test(r.url())) r.respond({ status: 200, contentType: "application/json", body: JSON.stringify(fake) }); else r.continue(); });
  await page.goto("http://127.0.0.1:9092/", { waitUntil: "networkidle2" }); await sleep(2600);   // the first 2 s have no animation
  // the action and the opacity/shift samples inside the page itself, from the moment of the action
  const sample = (act, sel, times) => page.evaluate((a, s, ts) => new Promise((res) => { const el = () => document.querySelector(s); const t0 = performance.now(); new Function(a)(); const out = []; ts.forEach((t) => setTimeout(() => { const e = el(), cs = e ? getComputedStyle(e) : null; const m = cs && cs.transform !== "none" ? cs.transform.match(/matrix\(([^)]+)\)/) : null; const v = m ? m[1].split(",").map(Number) : [1, 0, 0, 1, 0, 0]; out.push({ t: Math.round(performance.now() - t0), op: cs ? +(+cs.opacity).toFixed(3) : -1, ty: v[5], tx: v[4], sc: v[0], disp: cs ? cs.display : "none" }); if (out.length === ts.length) res(out); }, t)); }), act, sel, times);
  const rising = (a) => a.every((x, i) => i === 0 || x >= a[i - 1] - 0.01);
  // 1) bell panel
  let s = await sample("document.getElementById('bell').click()", "#bellPanel", [20, 70, 120, 450]);
  check("BELL PANEL: opens smoothly: opacity grows from near 0 to 1 (mid-way strictly between), and it slides down into place", s[0].op < 0.9 && s[0].op < s[2].op && s.some((x) => x.op > 0.02 && x.op < 0.98) && rising(s.map((x) => x.op)) && s[3].op === 1 && s[3].ty === 0 && s[0].ty < 0 && s[0].disp === "block", JSON.stringify(s));
  check("the first opening does not animate the old entries one by one (only the panel does)", (await page.evaluate(() => document.querySelectorAll("#bellPanel .nf.ps5new").length)) === 0);
  s = await sample("document.body.click()", "#bellPanel", [20, 70, 120, 450]);
  check("BELL PANEL: HIDING is smooth too: it is still there right after the click, fades out and slides up, and is gone at the end", s[0].disp === "block" && s[1].op < 0.95 && s[1].op > 0.02 && s[0].op >= s[1].op - 0.01 && s[1].op >= s[2].op - 0.01 && s[3].disp === "none", JSON.stringify(s));
  // 2) a new notification appears smoothly
  fake.items.push({ time: now - 5, text: "Третье уведомление", kind: 1 }); fake.latest = 3;
  s = await sample("document.getElementById('bell').click()", "#bellPanel .nf.ps5new", [40, 90]);
  const cls = await page.evaluate(() => [...document.querySelectorAll("#bellPanel .nf")].map((e) => [e.textContent.slice(0, 30), e.classList.contains("ps5new")]));
  await sleep(450);
  const fin = await page.evaluate(() => { const e = [...document.querySelectorAll("#bellPanel .nf")].find((x) => /Третье/.test(x.textContent)); const c = getComputedStyle(e); return { op: +c.opacity, tf: c.transform }; });
  check("NEW notification: only the new entry ('Третье…') gets the appearance animation, it slides in from the right and becomes fully opaque", cls.filter((c) => c[1]).length === 1 && /Третье/.test(cls.find((c) => c[1])[0]) && s[0].op < 0.95 && s[0].tx > 0 && s[1].op > s[0].op && fin.op === 1 && (fin.tf === "none" || fin.tf === "matrix(1, 0, 0, 1, 0, 0)"), JSON.stringify({ s, cls, fin }));
  await page.evaluate(() => document.body.click()); await sleep(200);
  // 3) warning popup (deleting a folder from the list)
  s = await sample("document.querySelector('.fd-row .fd-del').click()", "#fdDelModal", [20, 70, 120, 450]);
  const card = await sample("1", "#fdDelModal .modal-card", [450]);
  check("ALERT WINDOW (delete a folder from the list): the dark overlay fades in smoothly", s[0].op < 0.9 && rising(s.map((x) => x.op)) && s.some((x) => x.op > 0.02 && x.op < 0.98) && s[3].op === 1 && s[0].disp === "flex", JSON.stringify(s));
  const c2 = await sample("document.getElementById('fdDelCancel').click(); document.querySelector('.fd-row .fd-del').click()", "#fdDelModal .modal-card", [20, 70, 450]);
  check("... and its card rises and grows into place (starts lower and slightly smaller)", c2[0].ty > 0 && c2[0].sc < 1 && c2[0].op < 0.95 && c2[2].op === 1 && c2[2].ty === 0 && c2[2].sc === 1, JSON.stringify(c2));
  s = await sample("document.getElementById('fdDelCancel').click()", "#fdDelModal", [20, 80, 130, 450]);
  const cc = await sample("1", "#fdDelModal .modal-card", [450]);
  check("ALERT WINDOW: closing fades the overlay out smoothly and then hides it", s[0].disp === "flex" && s[1].op < 0.95 && s[1].op > 0.02 && s[1].op >= s[2].op - 0.01 && s[3].disp === "none", JSON.stringify(s));
  // the bell entries slide away smoothly on the "Clear" button
  await page.evaluate(() => document.getElementById("bell").click()); await sleep(600);
  s = await sample("[...document.querySelectorAll('#bellPanel button')].find((b) => b.textContent === 'Очистить').click()", "#bellPanel .nf", [20, 90]);
  const gone = await page.evaluate(() => document.querySelectorAll("#bellPanel .nf.ps5gone").length);
  check("CLEARING the bell list: the entries slide out and fade (class ps5gone on all of them) before the list is rebuilt", gone >= 2 && s[1].op < 0.95 && s[1].tx > 0, JSON.stringify({ gone, s }));
  await page.evaluate(() => document.body.click()); await sleep(500);
  // 4) Transmission page
  const tp = await browser.newPage(); await tp.setViewport({ width: 900, height: 1000 }); tp.on("pageerror", (e) => errors.push(e.message));
  await tp.goto("http://127.0.0.1:9091/transmission/web/", { waitUntil: "networkidle2" }); await sleep(2500);
  await tp.click("#toolbar-open"); await sleep(1500);
  await tp.evaluate(() => { const t = [...document.querySelectorAll(".open-torrent .ps5-tile")].find((x) => /usb/.test(x.getAttribute("data-path"))); if (t) t.click(); }); await sleep(500);
  const tm = await tp.evaluate(() => new Promise((res) => { const out = []; const t0 = performance.now(); document.getElementById("ps5-usbfix-help").click(); [15, 60, 400].forEach((t) => setTimeout(() => { const e = document.querySelector(".ps5-modal"), b = document.querySelector(".ps5-modal .ps5-modal-box"); out.push({ op: e ? +(+getComputedStyle(e).opacity).toFixed(3) : -1, bop: b ? +(+getComputedStyle(b).opacity).toFixed(3) : -1 }); if (out.length === 3) res(out); }, t)); }));
  check("TRANSMISSION PAGE: the window of the '?' button fades in too (and is fully visible after the animation)", tm[0].op < 0.9 && tm[0].op < tm[1].op + 0.001 && tm[2].op === 1 && tm[2].bop === 1, JSON.stringify(tm));
  const tc = await tp.evaluate(() => new Promise((res) => { const out = []; document.querySelector(".ps5-modal button").click(); [20, 90, 400].forEach((t) => setTimeout(() => { const e = document.querySelector(".ps5-modal"); out.push({ op: e ? +(+getComputedStyle(e).opacity).toFixed(3) : -1, out: e ? e.classList.contains("ps5-out") : false }); if (out.length === 3) res(out); }, t)); }));
  check("TRANSMISSION PAGE: closing the '?' window is smooth too (fades out, then removed from the page)", tc[0].out && tc[1].op < 0.97 && tc[1].op >= 0 && tc[2].op === -1, JSON.stringify(tc));
  check("no JS errors", errors.length === 0, errors.join(";"));
  console.log(`\n${pass}/${pass + fail} popup animation checks passed`); await browser.close(); process.exit(fail ? 1 : 0);
})().catch((e) => { console.log("crashed", e); process.exit(2); });
