const puppeteer = require("puppeteer-core");
let pass = 0, fail = 0; const check = (n, ok, e) => { (ok ? pass++ : fail++); console.log((ok ? "PASS " : "FAIL ") + n + (e !== undefined ? "  -> " + String(e).slice(0, 170) : "")); };
// node ui_panel_test.js phase(A|B|C)  A: two disks; B: one (the torrent on the second one removed); C: no torrents
(async () => {
  const phase = process.argv[2], iconset = process.argv[3] || "color";
  const browser = await puppeteer.launch({ executablePath: require("./chrome"), args: ["--no-sandbox", "--disable-gpu"], headless: "new" });
  const page = await browser.newPage(); await page.setViewport({ width: 1280, height: 640 });
  await page.emulateMediaFeatures([{ name: "prefers-color-scheme", value: "light" }]);
  await page.setRequestInterception(true);
  page.on("request", async (req) => { if (req.url().endsWith("transmission-app.css")) { const t = (await (await fetch(req.url())).text()).replace(/@media\(prefers-color-scheme:(dark|light)\)\{/g, "@media(max-width:0px){"); return req.respond({ status: 200, contentType: "text/css", body: t }); } req.continue(); });
  const errors = []; page.on("pageerror", (e) => errors.push(e.message));
  await page.goto("http://127.0.0.1:9091/transmission/web/", { waitUntil: "networkidle2" }); await new Promise((r) => setTimeout(r, 3500));
  const m = await page.evaluate(() => { const bar = document.getElementById("ps5-diskbar"), tb = document.getElementById("mainwin-toolbar"); return { exists: !!bar, shown: !!bar && bar.classList.contains("has") && getComputedStyle(bar).display !== "none", texts: [...document.querySelectorAll("#ps5-diskbar .ps5-disk-text")].map((e) => e.textContent), meters: [...document.querySelectorAll("#ps5-diskbar .ps5-meter i")].map((e) => e.style.width), disks: [...document.querySelectorAll("#ps5-diskbar .ps5-disk")].map((b) => { const i = b.querySelector(".ps5-disk-ico"), cs = getComputedStyle(i), r = i.getBoundingClientRect(), br = b.getBoundingClientRect(), body = b.querySelector(".ps5-disk-body").getBoundingClientRect(); return { label: b.querySelector(".ps5-disk-text").textContent.split(":")[0], kind: i.className.replace("ps5-disk-ico", "").trim(), w: Math.round(r.width), h: Math.round(r.height), mask: cs.webkitMaskImage || cs.maskImage, bg: cs.backgroundImage, icoLeft: r.left, bodyLeft: body.left, icoMid: r.top + r.height / 2, boxMid: br.top + br.height / 2 }; }), pad: getComputedStyle(document.getElementById("mainwin")).paddingTop, barTop: bar ? Math.round(bar.getBoundingClientRect().top) : null, barBottom: bar ? Math.round(bar.getBoundingClientRect().bottom) : null, tbTop: Math.round(tb.getBoundingClientRect().top), doc: [document.documentElement.clientWidth, document.documentElement.scrollWidth] }; });
  check("no JS errors on the page", errors.length === 0, errors.join(";"));
  check("a top padding of 16px above everything (twice the former 8px): the toolbar icons and the disk panel no longer touch the top edge", m.pad === "16px" && m.tbTop >= 16, `${m.pad}, toolbar top ${m.tbTop}`);
  if (phase === "A") {
    check("the panel is shown above the toolbar with FOUR disks: those that the torrents live on (Data, /mnt/ext1, /mnt/usb0, /tmp)", m.shown && m.texts.length === 4 && m.barTop >= 16 && m.barTop < m.tbTop, JSON.stringify(m.texts));
    check("format like BFpilot: 'Data: 8.2 GB free of 252 GB, 97% used'", m.texts.every((t) => /^(Data|\/tmp|\/mnt\/ext1|\/mnt\/usb0): [\d.]+ (B|KB|MB|GB|TB|PB) free of [\d.]+ (KB|MB|GB|TB|PB), \d+% used$/.test(t)), m.texts.join(" | "));
    check("'Data' (the internal storage) comes first; every disk has a usage meter", /^Data:/.test(m.texts[0]) && m.meters.length === 4 && m.meters.every((w) => /^\d+%$/.test(w)), m.meters.join());
    check("only the used disks are listed (nothing for /mnt/ext2, /mnt/usb1 ...)", !m.texts.some((t) => /\/mnt\/(ext[2-9]|usb[1-9])/.test(t)));
    const kinds = Object.fromEntries(m.disks.map((d) => [d.label, d.kind]));
    check("icon kinds: /mnt/ext1 = SSD (M.2), /mnt/usb0 = USB flash drive, Data and everything else = internal disk", kinds["Data"] === "int" && kinds["/mnt/ext1"] === "ssd" && kinds["/mnt/usb0"] === "usb" && kinds["/tmp"] === "int", JSON.stringify(kinds));
    check(`every chip has a 30px icon that is really drawn (${iconset}), and the three kinds look different`, m.disks.every((d) => d.w === 30 && d.h === 30 && (iconset === "mono" ? /data:image\/svg/.test(d.mask) : /data:image\/svg/.test(d.bg))) && new Set(m.disks.map((d) => (iconset === "mono" ? d.mask : d.bg))).size === 3, JSON.stringify(m.disks.map((d) => [d.kind, d.w, d.h])));
    check("the icon sits at the left of the text and the meter, vertically centered in the chip", m.disks.every((d) => d.icoLeft < d.bodyLeft && Math.abs(d.icoMid - d.boxMid) <= 1.5), JSON.stringify(m.disks.map((d) => [d.icoMid, d.boxMid])));
    await page.setViewport({ width: 500, height: 640 }); await new Promise((r) => setTimeout(r, 400));
    const narrow = await page.evaluate(() => [document.documentElement.clientWidth, document.documentElement.scrollWidth]);
    check("on a narrow window the panel wraps and there is still no horizontal scrollbar", narrow[1] <= narrow[0], narrow.join("/"));
    check("the gap between the disk panel and the toolbar is 12px", m.tbTop - m.barBottom === 12, `${m.barBottom} -> ${m.tbTop}`);
  } else if (phase === "D") {
    check("with 'Показывать состояние дисков' off the panel is not created at all, the top padding stays 16px", !m.exists && m.pad === "16px" && m.tbTop === 16, JSON.stringify({ exists: m.exists, pad: m.pad, tbTop: m.tbTop }));
  } else if (phase === "B") {
    check("when no torrent is left on the other disks, only 'Data' stays", m.shown && m.texts.length === 1 && /^Data:/.test(m.texts[0]), JSON.stringify(m.texts));
  } else {
    check("without any torrents the panel is hidden (the padding stays)", m.exists && !m.shown && m.texts.length === 0, JSON.stringify(m));
  }
  console.log(`\n${pass}/${pass + fail} panel checks passed (${phase})`); await browser.close(); process.exit(fail ? 1 : 0);
})().catch((e) => { console.log("crashed", e); process.exit(2); });
