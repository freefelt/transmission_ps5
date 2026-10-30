const puppeteer = require("puppeteer-core");
let pass = 0, fail = 0; const check = (n, ok, e) => { (ok ? pass++ : fail++); console.log((ok ? "PASS " : "FAIL ") + n + (e !== undefined ? "  -> " + String(e).slice(0, 160) : "")); };
// Real files from the daemon server; @media themes are disabled (the PS5 browser does not apply them). We check the layout as the console sees it.
(async () => {
  const browser = await puppeteer.launch({ executablePath: require("./chrome"), args: ["--no-sandbox", "--disable-gpu"], headless: "new" });
  for (const theme of ["dark", "light"]) for (const w of [1280, 900, 500]) {
    const page = await browser.newPage(); await page.setViewport({ width: w, height: 640 });
    await page.emulateMediaFeatures([{ name: "prefers-color-scheme", value: "light" }]);
    await page.setRequestInterception(true);
    page.on("request", async (req) => {
      if (req.url().endsWith("transmission-app.css")) { const t = (await (await fetch(req.url())).text()).replace(/@media\(prefers-color-scheme:(dark|light)\)\{/g, "@media(max-width:0px){"); return req.respond({ status: 200, contentType: "text/css", body: t }); }
      if (req.url().endsWith("ps5-theme.css") && theme === "light") { const t = await (await fetch(req.url().replace("ps5-theme.css", "ps5-theme-light.css"))).text(); return req.respond({ status: 200, contentType: "text/css", body: t }); }
      req.continue();
    });
    await page.goto("http://127.0.0.1:9091/transmission/web/", { waitUntil: "networkidle2" }); await new Promise((r) => setTimeout(r, 2500));
    const m = await page.evaluate(() => {
      const b = document.querySelector("#torrent-list .torrent-progress-bar"), cs = getComputedStyle(b), bf = getComputedStyle(b, "::before"), af = getComputedStyle(b, "::after");
      return { doc: [document.documentElement.clientWidth, document.documentElement.scrollWidth], cont: [document.querySelector("#torrent-container").clientWidth, document.querySelector("#torrent-container").scrollWidth], outer: cs.borderTopLeftRadius, before: bf.borderTopLeftRadius, after: af.borderTopLeftRadius, bw: cs.borderTopWidth, h: cs.height, barBg: cs.backgroundColor, track: (() => { const x = document.createElement("div"); x.style.backgroundColor = "var(--color-border-default)"; document.body.appendChild(x); const c = getComputedStyle(x).backgroundColor; x.remove(); return c; })(), clip: bf.clipPath, afterBg: af.backgroundColor, lh: bf.lineHeight, sh1: bf.textShadow, sh2: af.textShadow, shAll: [...document.querySelectorAll("#torrent-list .torrent-progress-bar")].every((e) => getComputedStyle(e, "::before").textShadow === "none" && getComputedStyle(e, "::after").textShadow === "none"), bg: getComputedStyle(document.body).backgroundColor, rows: document.querySelectorAll("#torrent-list .torrent").length };
    });
    check(`${theme} ${w}px: no horizontal scrollbar (page ${m.doc.join("/")}, list ${m.cont.join("/")})`, m.doc[1] <= m.doc[0] && m.cont[1] <= m.cont[0] && m.rows > 0);
    if (w === 1280) check(`${theme}: the progress bar is styled like the disk meter: NO outer border, the empty part is the track color (${m.barBg}), the fill is rounded (${m.before}), the bar is 20px high`, m.bw === "0px" && m.h === "20px" && m.barBg === m.track && m.barBg !== "rgba(0, 0, 0, 0)" && parseFloat(m.outer) >= 9 && parseFloat(m.before) >= 9 && parseFloat(m.after) >= 9 && m.afterBg === "rgba(0, 0, 0, 0)" && /inset\(/.test(m.clip) && /round/.test(m.clip), JSON.stringify([m.bw, m.h, m.barBg, m.track, m.clip, m.afterBg]));
    if (w === 1280) check(`${theme}: the percent text has no shadow in ANY bar state and its line box equals the bar height (20px) so it is centered`, m.shAll === true && m.sh1 === "none" && m.sh2 === "none" && m.lh === "20px", JSON.stringify([m.shAll, m.sh1, m.sh2, m.lh]));
    await page.close();
  }
  console.log(`\n${pass}/${pass + fail} layout checks passed`); await browser.close(); process.exit(fail ? 1 : 0);
})().catch((e) => { console.log("crashed", e); process.exit(2); });
