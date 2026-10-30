const puppeteer = require("puppeteer-core");
let pass = 0, fail = 0; const check = (n, ok, e) => { (ok ? pass++ : fail++); console.log((ok ? "PASS " : "FAIL ") + n + (e !== undefined ? "  -> " + String(e).slice(0, 220) : "")); };
// Real daemon files. Filter: the state list and the Filter field have the same height, the text fits, the tracker filter is hidden; speeds on the right of the rows.
(async () => {
  const browser = await puppeteer.launch({ executablePath: require("./chrome"), args: ["--no-sandbox", "--disable-gpu"], headless: "new" });
  const page = await browser.newPage(); await page.setViewport({ width: 1280, height: 700 });
  await page.emulateMediaFeatures([{ name: "prefers-color-scheme", value: "light" }]); await page.setRequestInterception(true);
  page.on("request", async (req) => { if (req.url().endsWith("transmission-app.css")) { const t = (await (await fetch(req.url())).text()).replace(/@media\(prefers-color-scheme:(dark|light)\)\{/g, "@media(max-width:0px){"); return req.respond({ status: 200, contentType: "text/css", body: t }); } req.continue(); });
  const errors = []; page.on("pageerror", (e) => errors.push(e.message));
  await page.goto("http://127.0.0.1:9091/transmission/web/", { waitUntil: "networkidle2" }); await new Promise((r) => setTimeout(r, 3500));
  const bar = () => page.evaluate(() => {
    const ctx = document.createElement("canvas").getContext("2d"), q = (s) => document.querySelector(s), sel = q("#filter-mode"), inp = q("#torrent-search"), cs = getComputedStyle(sel), ci = getComputedStyle(inp);
    ctx.font = `${cs.fontStyle} ${cs.fontWeight} ${cs.fontSize} ${cs.fontFamily}`; const widest = Math.max(...[...sel.options].map((o) => ctx.measureText(o.text).width));
    const tr = q("#filter-tracker"), rows = document.querySelectorAll("#torrent-list .torrent").length, count = (q("#filter-count").textContent.match(/\d+/) || [""])[0];
    const rr = (e) => { const r = e.getBoundingClientRect(); return { w: +r.width.toFixed(1), h: +r.height.toFixed(1), top: +r.top.toFixed(1) }; };
    return { sel: rr(sel), inp: rr(inp), selFont: [cs.fontSize, cs.fontFamily], inpFont: [ci.fontSize, ci.fontFamily], need: Math.ceil(widest) + parseFloat(cs.paddingLeft) + parseFloat(cs.paddingRight) + 2, app: cs.appearance, radius: parseFloat(cs.borderTopLeftRadius), trDisplay: getComputedStyle(tr).display, trW: tr.getBoundingClientRect().width, trValue: tr.value, rows, count, doc: [document.documentElement.clientWidth, document.documentElement.scrollWidth] };
  });
  let m = await bar();
  check("no JS errors", errors.length === 0, errors.join(";"));
  check("the TRACKER filter is hidden (display none, zero width) and stays on 'All': every torrent is listed", m.trDisplay === "none" && m.trW === 0 && m.trValue === "all" && m.rows > 0 && String(m.rows) === m.count, JSON.stringify([m.trDisplay, m.trW, m.trValue, m.rows, m.count]));
  check("the state list and the Filter field have the SAME height (26px) and the same font", m.sel.h === 26 && m.inp.h === 26 && m.selFont[0] === m.inpFont[0] && m.selFont[1] === m.inpFont[1], JSON.stringify([m.sel, m.inp, m.selFont, m.inpFont]));
  check("they sit on one line (same top)", Math.abs(m.sel.top - m.inp.top) <= 0.6, `${m.sel.top} / ${m.inp.top}`);
  check("the state list is rounded and drawn by our style (appearance none)", m.app === "none" && m.radius >= 8, `${m.app}, ${m.radius}`);
  check("its width fits the LONGEST item ('Private torrents') plus padding: any selected text fits, even 'All'", m.sel.w >= m.need && m.sel.w < 200, `${m.sel.w}px (need ${m.need})`);
  const w0 = m.sel.w;
  for (const v of ["downloading", "private", "all"]) { await page.select("#filter-mode", v); await new Promise((r) => setTimeout(r, 300)); }
  m = await bar(); check("the width does not jump when another item is chosen", m.sel.w === w0, `${m.sel.w} vs ${w0}`);
  await page.setViewport({ width: 500, height: 700 }); await new Promise((r) => setTimeout(r, 500)); m = await bar();
  check("on a narrow window (500px) there is no horizontal scrollbar", m.doc[1] <= m.doc[0], m.doc.join("/"));
  await page.setViewport({ width: 1280, height: 700 }); await new Promise((r) => setTimeout(r, 400));
  // --- speeds on the right of the normal rows
  const rows = await page.evaluate(() => [...document.querySelectorAll("#torrent-list .torrent:not(.compact)")].map((li) => { const pd = li.querySelector(".torrent-peer-details"), l = pd.querySelector(".ps5-peers-left"), r = pd.querySelector(".ps5-peers-speed"), pb = li.querySelector(".torrent-progress-bar"); const R = (e) => e && e.getBoundingClientRect(); return { text: pd.textContent, left: l && l.textContent, right: r && r.textContent, lr: l && R(l).right, rl: r && R(r).left, rr: r && (() => { const g = document.createRange(); g.selectNodeContents(r); return g.getBoundingClientRect().right; })(), pbr: R(pb).right, lt: l && R(l).top, rt: r && R(r).top, disp: getComputedStyle(pd).display }; }));
  const split = rows.filter((r) => r.right);
  check("full rows with a speed (downloading and seeding) are split: caption on the left, speeds on the right", split.length >= 3 && split.every((r) => /^(Downloading from|Seeding to)/.test(r.left) && /^[\u25BC\u25B2]/.test(r.right) && r.disp === "flex"), JSON.stringify(split.slice(0, 3).map((r) => [r.left, r.right])));
  check("the speeds end 4px BEFORE the right edge of the progress bar (a 4px inset); the caption ends before them", split.every((r) => Math.abs((r.pbr - r.rr) - 4) <= 0.6 && r.lr <= r.rl), JSON.stringify(split.slice(0, 3).map((r) => [r.rr, r.pbr, r.pbr - r.rr])));
  check("caption and speeds are on one line", split.every((r) => Math.abs(r.lt - r.rt) <= 3), JSON.stringify(split.slice(0, 3).map((r) => [r.lt, r.rt])));
  check("the whole text is unchanged apart from the split (' - ' between the parts is gone from the screen)", split.every((r) => r.text === r.left + r.right), JSON.stringify(split.slice(0, 2).map((r) => r.text)));
  const gaps = await page.evaluate(() => [...document.querySelectorAll("#torrent-list .torrent:not(.compact)")].map((li) => { const d = li.querySelector(".torrent-progress-details").getBoundingClientRect(), b = li.querySelector(".torrent-progress-bar").getBoundingClientRect(), p = li.querySelector(".torrent-peer-details").getBoundingClientRect(), h = li.getBoundingClientRect().height; return { above: +(b.top - d.bottom).toFixed(1), below: +(p.top - b.bottom).toFixed(1), h }; }));
  check("in full rows the gap ABOVE the progress bar is 5px (was 3) and the gap BELOW it, to the 'Downloading from ...' line, is 5px (was 3)", gaps.length > 5 && gaps.every((g) => g.above === 5 && g.below === 5), JSON.stringify(gaps.slice(0, 3)));
  check("so every full row is 4px taller than before (92 -> 96px)", gaps.every((g) => Math.abs(g.h - 96) <= 0.6), JSON.stringify([...new Set(gaps.map((g) => g.h))]));
  // --- compact rows are left alone
  await page.click("#toolbar-overflow"); await new Promise((r) => setTimeout(r, 400));
  const toggled = await page.evaluate(() => { const e = [...document.querySelectorAll(".popup *, .context-menu *")].find((x) => x.children.length === 0 && /compact/i.test(x.textContent)); if (!e) return false; (e.closest("label, button, a, li") || e).click(); return true; });
  await new Promise((r) => setTimeout(r, 1200));
  const compact = await page.evaluate(() => ({ n: document.querySelectorAll("#torrent-list .torrent.compact").length, split: document.querySelectorAll("#torrent-list .torrent.compact .ps5-peers-speed").length, disp: [...document.querySelectorAll("#torrent-list .torrent.compact .torrent-peer-details")].slice(0, 2).map((e) => getComputedStyle(e).display) }));
  check("'Compact rows' mode: those rows are not touched (no split, no flex)", toggled && compact.n > 0 && compact.split === 0 && compact.disp.every((d) => d !== "flex"), JSON.stringify([toggled, compact]));
  const cg = await page.evaluate(() => { const li = document.querySelector("#torrent-list .torrent.compact"), g = (s) => { const cs = getComputedStyle(li.querySelector(s)); return [cs.marginTop, cs.marginBottom]; }; return { h: +li.getBoundingClientRect().height.toFixed(1), bar: g(".torrent-progress-bar"), peers: g(".torrent-peer-details") }; });
  const cb = await page.evaluate(() => { const b = document.querySelector("#torrent-list .torrent.compact .torrent-progress-bar"), cs = getComputedStyle(b), x = document.createElement("div"); x.style.backgroundColor = "var(--color-border-default)"; document.body.appendChild(x); const track = getComputedStyle(x).backgroundColor; x.remove(); return { bw: cs.borderTopWidth, h: cs.height, bg: cs.backgroundColor, track, radius: cs.borderTopLeftRadius, clip: getComputedStyle(b, "::before").clipPath, afterBg: getComputedStyle(b, "::after").backgroundColor }; });
  check("'Compact rows': the same new bar style (no border, track color, rounded fill, 20px), the geometry stays", cb.bw === "0px" && cb.h === "20px" && cb.bg === cb.track && parseFloat(cb.radius) >= 9 && /round/.test(cb.clip) && cb.afterBg === "rgba(0, 0, 0, 0)", JSON.stringify(cb));
  check("compact rows keep their exact old geometry (height 40, margins 5px)", cg.h === 40 && cg.bar.join() === "5px,5px" && cg.peers.join() === "5px,5px", JSON.stringify(cg));
  await page.setViewport({ width: 1280, height: 700, deviceScaleFactor: 2 }); await new Promise((r) => setTimeout(r, 300));
  console.log(`\n${pass}/${pass + fail} filter checks passed`); await browser.close(); process.exit(fail ? 1 : 0);
})().catch((e) => { console.log("crashed", e); process.exit(2); });
