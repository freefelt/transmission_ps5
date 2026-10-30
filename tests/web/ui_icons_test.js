const puppeteer = require("puppeteer-core");
let pass = 0, fail = 0; const check = (n, ok, e) => { (ok ? pass++ : fail++); console.log((ok ? "PASS " : "FAIL ") + n + (e !== undefined ? "  -> " + String(e).slice(0, 170) : "")); };
// node ui_icons_test.js <suite>: real daemon files; @media themes are disabled (as in the PS5 browser)
(async () => {
  const set = process.argv[2], mono = set === "mono";
  const browser = await puppeteer.launch({ executablePath: require("./chrome"), args: ["--no-sandbox", "--disable-gpu"], headless: "new" });
  const page = await browser.newPage(); await page.setViewport({ width: 1280, height: 700 });
  await page.emulateMediaFeatures([{ name: "prefers-color-scheme", value: "light" }]);
  await page.setRequestInterception(true);
  page.on("request", async (req) => { if (req.url().endsWith("transmission-app.css")) { const t = (await (await fetch(req.url())).text()).replace(/@media\(prefers-color-scheme:(dark|light)\)\{/g, "@media(max-width:0px){"); return req.respond({ status: 200, contentType: "text/css", body: t }); } req.continue(); });
  const errors = []; page.on("pageerror", (e) => errors.push(e.message));
  await page.goto("http://127.0.0.1:9091/transmission/web/", { waitUntil: "networkidle2" }); await new Promise((r) => setTimeout(r, 3500));
  const m = await page.evaluate(() => {
    const ids = ["toolbar-open", "toolbar-delete", "toolbar-start", "toolbar-pause", "toolbar-inspector", "toolbar-overflow"];
    const tb = ids.map((id) => { const s = document.querySelector(`#${id} svg`), cs = getComputedStyle(s); const kids = [...s.children].map((c) => getComputedStyle(c).display); return { id, mask: cs.webkitMaskImage || cs.maskImage, bg: cs.backgroundImage, w: Math.round(s.getBoundingClientRect().width), hiddenKids: kids.every((d) => d === "none"), ico: cs.getPropertyValue("--ps5-ico").length }; });
    const single = document.querySelector('#torrent-list .torrent .icon[data-icon-multifile="false"]'), multi = document.querySelector('#torrent-list .torrent .icon[data-icon-multifile="true"]');
    const lay = (e) => { const cs = getComputedStyle(e); return { mask: cs.webkitMaskImage || cs.maskImage, bg: cs.backgroundImage }; };
    return { tb, single: single && lay(single), multi: multi && lay(multi), hasMulti: !!multi, folderVar: getComputedStyle(document.documentElement).getPropertyValue("--folder-image-url").trim().slice(0, 30), link: !!document.querySelector('link[href$="ps5-icons.css"]') };
  });
  check(`[${set}] no JS errors; the stylesheet link is present`, errors.length === 0 && m.link, errors.join(";"));
  check(`[${set}] all six toolbar buttons (Open, Delete, Start, Stop, Inspector, burger) got a new image and the stock drawing is hidden`, m.tb.every((b) => b.ico > 100 && b.hiddenKids && (mono ? /data:image\/svg/.test(b.mask) : (/data:image\/svg/.test(b.bg) && (b.mask === "none" || b.mask === "")))), JSON.stringify(m.tb.map((b) => [b.id, b.ico, b.hiddenKids])));
  check(`[${set}] the burger is bigger (30px) than the others (26px)`, m.tb.find((b) => b.id === "toolbar-overflow").w === 30 && m.tb.find((b) => b.id === "toolbar-open").w === 26);
  check(`[${set}] a single-file torrent has a file icon (one image)`, mono ? (m.single.mask.match(/url\(/g) || []).length === 1 : /data:image\/svg/.test(m.single.bg), JSON.stringify(m.single).slice(0, 90));
  check(`[${set}] a multi-file torrent shows ONE folder icon (not file+folder)`, m.hasMulti && (mono ? (m.multi.mask.match(/url\(/g) || []).length === 1 && m.multi.mask !== m.single.mask : (/data:image\/svg/.test(m.multi.bg) && m.multi.bg !== m.single.bg && (m.multi.mask === "none" || m.multi.mask === ""))), JSON.stringify(m.multi).slice(0, 90));
  await page.evaluate(() => { const r = document.querySelector('#torrent-list .torrent .icon[data-icon-multifile="false"]'); r.closest(".torrent").click(); });
  await new Promise((r) => setTimeout(r, 400));
  const sel = await page.evaluate(() => { const row = document.querySelector("#torrent-list .torrent.selected"); const ic = row && row.querySelector(".icon"); if (!ic) return null; const cs = getComputedStyle(ic); return { bg: cs.backgroundColor, mask: cs.webkitMaskImage || cs.maskImage, img: cs.backgroundImage }; });
  check(`[${set}] the SELECTED torrent: ` + (mono ? "the icon is drawn in the 'selected' color (a mask), as before" : "no white square behind the colored icon (background-color is transparent)"), !!sel && (mono ? (sel.bg !== "rgba(0, 0, 0, 0)" && /data:image\/svg/.test(sel.mask)) : (sel.bg === "rgba(0, 0, 0, 0)" && /data:image\/svg/.test(sel.img))), JSON.stringify(sel).slice(0, 140));
  console.log(`\n${pass}/${pass + fail} icon checks passed [${set}]`); await browser.close(); process.exit(fail ? 1 : 0);
})().catch((e) => { console.log("crashed", e); process.exit(2); });
