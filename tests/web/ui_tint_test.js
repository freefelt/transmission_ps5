const puppeteer = require("puppeteer-core");
let pass = 0, fail = 0; const check = (n, ok, e) => { (ok ? pass++ : fail++); console.log((ok ? "PASS " : "FAIL ") + n + (e !== undefined ? "  -> " + String(e).slice(0, 260) : "")); };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
// Tint of the sections: simple settings a bit greenish, expert a bit orange-ish, diagnostics reddish; the header and the content of a section share one frame. Real Chrome.
(async () => {
  const browser = await puppeteer.launch({ executablePath: require("./chrome"), args: ["--no-sandbox", "--disable-gpu"], headless: "new" });
  const page = await browser.newPage(); await page.setViewport({ width: 900, height: 1200 }); const errors = []; page.on("pageerror", (e) => errors.push(e.message));
  await page.goto("http://127.0.0.1:9092/", { waitUntil: "networkidle2" }); await sleep(2600);
  await page.evaluate(() => { for (const id of ["accSettingsBtn", "accExpertBtn", "accDiagBtn", "accCreditsBtn"]) document.getElementById(id).click(); }); await sleep(700);
  const col = (sel) => page.evaluate((s) => { const e = document.querySelector(s); const c = getComputedStyle(e); const m = c.backgroundColor.match(/[\d.]+/g).map(Number); const b = c.borderTopColor.match(/[\d.]+/g).map(Number); return { bg: m.slice(0, 3), alpha: m.length > 3 ? m[3] : 1, border: b.slice(0, 3), bw: c.borderTopWidth }; }, sel);
  const ctl = await col("#fdCard"), simple = await col("#accSettings"), expert = await col("#accExpert"), diag = await col("#accDiag"), credits = await col("#accCredits"), inner = await col("#form");
  const spread = (c) => Math.max(...c) - Math.min(...c);
  const near = (a, b) => Math.max(...a.map((v, i) => Math.abs(v - b[i])));
  check("control: a card outside the dropdowns keeps the old blue-grey colour rgb(27,33,43)", ctl.bg.join() === "27,33,43", ctl.bg);
  check("SIMPLE settings: the whole dropdown is a greenish grey (green is the strongest channel), only slightly different from the usual grey", simple.bg[1] > simple.bg[0] && simple.bg[1] > simple.bg[2] && spread(simple.bg) <= 16 && near(simple.bg, ctl.bg) <= 14, JSON.stringify(simple));
  check("EXPERT settings: an orange-ish grey (red strongest, blue weakest), only slightly different from the usual grey", expert.bg[0] > expert.bg[1] && expert.bg[1] >= expert.bg[2] && spread(expert.bg) <= 16 && near(expert.bg, ctl.bg) <= 14, JSON.stringify(expert));
  check("DIAGNOSTICS: a reddish grey (red strongest and clearly above green, blue not below green), different from the orange expert group", diag.bg[0] > diag.bg[1] && diag.bg[0] > diag.bg[2] && diag.bg[0] - diag.bg[1] >= 8 && diag.bg[2] >= diag.bg[1] && spread(diag.bg) <= 16 && diag.bg.join() !== expert.bg.join(), JSON.stringify([diag, expert]));
  check("the credits dropdown keeps the usual grey", credits.bg.join() === "27,33,43", JSON.stringify(credits));
  check("the borders follow the tint (green-ish / orange-ish / reddish), not the old blue-grey", simple.border[1] > simple.border[2] && expert.border[0] > expert.border[2] && diag.border[0] > diag.border[1], JSON.stringify([simple.border, expert.border, diag.border]));
  check("the card INSIDE a dropdown is flat (transparent, no border): the header and the content are one frame", inner.alpha === 0 && inner.bw === "0px", JSON.stringify(inner));
  const rowB = await page.evaluate(() => { const a = document.querySelector("#form .row"), b = document.querySelector("#expertForm .row"); return [getComputedStyle(a).borderTopColor, getComputedStyle(b).borderTopColor]; });
  check("the lines between rows follow the tint too", rowB[0] !== "rgb(42, 51, 64)" && rowB[1] !== "rgb(42, 51, 64)" && rowB[0] !== rowB[1], JSON.stringify(rowB));
  const hd = await page.evaluate(() => { const f = (acc, btn) => { const a = document.getElementById(acc), b = document.getElementById(btn), c = getComputedStyle(b); return { inside: a.contains(b), bw: c.borderTopWidth, below: c.borderBottomWidth, belowColor: c.borderBottomColor }; }; return { s: f("accSettings", "accSettingsBtn"), e: f("accExpert", "accExpertBtn"), d: f("accDiag", "accDiagBtn") }; });
  check("the HEADER of every dropdown sits inside its frame, has no frame of its own and, when open, a thin line under it", [hd.s, hd.e, hd.d].every((h) => h.inside && h.bw === "0px" && h.below === "1px"), JSON.stringify(hd));
  check("no JS errors", errors.length === 0, errors.join(";"));
  console.log(`\n${pass}/${pass + fail} section tint checks passed`); await browser.close(); process.exit(fail ? 1 : 0);
})().catch((e) => { console.log("crashed", e); process.exit(2); });
