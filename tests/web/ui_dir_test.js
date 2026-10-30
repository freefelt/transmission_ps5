const puppeteer = require("puppeteer-core");
let pass = 0, fail = 0; const check = (n, ok, e) => { (ok ? pass++ : fail++); console.log((ok ? "PASS " : "FAIL ") + n + (e !== undefined ? "  -> " + String(e).slice(0, 260) : "")); };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
// Torrent path on the right of the size line: the same value as "Location" in the inspector (getDownloadDir), against the real daemon.
(async () => {
  const browser = await puppeteer.launch({ executablePath: require("./chrome"), args: ["--no-sandbox", "--disable-gpu"], headless: "new" });
  const page = await browser.newPage(); await page.setViewport({ width: 1280, height: 900 }); const errors = []; page.on("pageerror", (e) => errors.push(e.message));
  await page.goto("http://127.0.0.1:9091/transmission/web/", { waitUntil: "networkidle2" }); await sleep(3500);
  const dirs = await page.evaluate(async () => { let sid = ""; for (let i = 0; i < 2; i++) { const r = await fetch("../rpc", { method: "POST", headers: { "X-Transmission-Session-Id": sid, "Content-Type": "application/json" }, body: JSON.stringify({ jsonrpc: "2.0", method: "torrent_get", params: { fields: ["name", "download_dir"] }, id: 1 }) }); if (r.status === 409) { sid = r.headers.get("X-Transmission-Session-Id"); continue; } const j = await r.json(); const m = {}; for (const t of j.result.torrents) m[t.name] = t.download_dir; return m; } });
  const rows = await page.evaluate(() => [...document.querySelectorAll("#torrent-list .torrent:not(.compact)")].map((li) => { const d = li.querySelector(".torrent-progress-details"), cs = getComputedStyle(d), af = getComputedStyle(d, "::after"); return { bg: af.backgroundColor, radius: af.borderRadius, padL: af.paddingLeft, name: li.querySelector(".torrent-name").textContent, attr: d.getAttribute("data-ps5-dir"), content: af.content, display: cs.display, justify: cs.justifyContent, afterDisplay: af.display, text: d.textContent }; }));
  check("there are full rows to look at", rows.length >= 5, rows.length);
  check("EVERY full row carries the folder of its torrent, exactly the value the inspector shows as 'Location' (the same RPC field)", rows.every((r) => r.attr && r.attr === dirs[r.name]), JSON.stringify(rows.filter((r) => r.attr !== dirs[r.name]).slice(0, 2)));
  check("the folders differ between torrents (several different folders are shown)", new Set(rows.map((r) => r.attr)).size >= 2, JSON.stringify([...new Set(rows.map((r) => r.attr))]));
  check("the folder is drawn on the right of the size line: the line is flex with space-between and the label comes from the attribute", rows.every((r) => r.display === "flex" && r.justify === "space-between" && r.content === '"' + r.attr + '"'), JSON.stringify(rows[0]));
  check("the folder is plain text: NO background, no rounded box, no left padding (it was only the inspector screenshot that had a background)", rows.every((r) => r.bg === "rgba(0, 0, 0, 0)" && r.radius === "0px" && r.padL === "0px"), JSON.stringify(rows[0]));
  check("the text of the size line on the left is untouched (it still ends with 'remaining' or 'Ratio ...', the path is not part of the text)", rows.every((r) => !r.text.includes("/mnt/") && !r.text.includes("/data/") && !r.text.includes("/tmp/")), rows.map((r) => r.text).slice(0, 2).join(" | "));
  await page.click("#toolbar-overflow"); await sleep(400);
  await page.evaluate(() => { const e = [...document.querySelectorAll(".popup *, .context-menu *")].find((x) => x.children.length === 0 && /compact/i.test(x.textContent)); (e.closest("label, button, a, li") || e).click(); });
  await sleep(1200);
  const comp = await page.evaluate(() => ({ rows: document.querySelectorAll("#torrent-list .torrent.compact").length, details: document.querySelectorAll("#torrent-list .torrent.compact .torrent-progress-details").length, attr: document.querySelectorAll("#torrent-list .torrent.compact [data-ps5-dir]").length }));
  check("'Compact rows': no folder label (that mode has no size line), nothing broken", comp.rows > 0 && comp.details === 0 && comp.attr === 0, JSON.stringify(comp));
  check("no JS errors", errors.length === 0, errors.join(";"));
  console.log(`\n${pass}/${pass + fail} torrent folder checks passed`); await browser.close(); process.exit(fail ? 1 : 0);
})().catch((e) => { console.log("crashed", e); process.exit(2); });
