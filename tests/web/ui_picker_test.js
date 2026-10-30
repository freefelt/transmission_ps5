const puppeteer = require("puppeteer-core");
let pass = 0, fail = 0; const check = (n, ok, e) => { (ok ? pass++ : fail++); console.log((ok ? "PASS " : "FAIL ") + n + (e !== undefined ? "  -> " + String(e).slice(0, 260) : "")); };
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
// Folder picker in real Chrome: sizes, scrolling, the default disk, entering a folder with the mouse and the keyboard, creating a folder, the warning; desktop and narrow width
(async () => {
  const browser = await puppeteer.launch({ executablePath: require("./chrome"), args: ["--no-sandbox", "--disable-gpu"], headless: "new" });
  const page = await browser.newPage(); const errors = []; page.on("pageerror", (e) => errors.push(e.message));
  const $ = (id) => page.$("#" + id);
  const ev = (f, ...a) => page.evaluate(f, ...a);
  for (const [w, h, label] of [[1100, 800, "desktop"], [390, 760, "phone"]]) {
    await page.setViewport({ width: w, height: h }); await page.goto("http://127.0.0.1:9092/", { waitUntil: "networkidle2" }); await sleep(1500);
    await ev(() => { document.getElementById("accSettings") && 0; const c = document.getElementById("fdCard"); c && c.scrollIntoView(); });
    await ev(() => document.getElementById("fdShowAdd").click()); await sleep(1200);
    const m = await ev(() => { const card = document.querySelector("#fdPickModal .modal-card").getBoundingClientRect(), list = document.getElementById("pkList"), sel = document.getElementById("pkDisk");
      const btn = document.getElementById("pkChoose").getBoundingClientRect();
      return { vis: getComputedStyle(document.getElementById("fdPickModal")).display, cw: Math.round(card.width), ch: Math.round(card.height), cl: Math.round(card.left), cr: Math.round(card.right), cb: Math.round(card.bottom), iw: innerWidth, ih: innerHeight, sw: document.documentElement.scrollWidth,
        lh: list.clientHeight, lsh: list.scrollHeight, ov: getComputedStyle(list).overflowY, disk: sel.value, rows: list.children.length, crumbs: document.getElementById("pkCrumbs").textContent, bb: Math.round(btn.bottom) }; });
    check(`${label}: the picker opens as a popup that fits the screen (not wider or taller than the window, no sideways scrolling of the page)`, m.vis === "flex" && m.cw <= m.iw && m.cl >= 0 && m.cr <= m.iw && m.ch <= m.ih && m.cb <= m.ih && m.sw <= m.iw, JSON.stringify(m));
    check(`${label}: the default disk is the M.2 (/mnt/ext1) and its folders are listed; a long list scrolls inside its own frame (the popup does not grow), the choose button stays on screen`, m.disk === "/mnt/ext1" && m.rows >= 30 && m.ov === "auto" && m.lsh > m.lh && m.lh <= m.ih * 0.46 && m.bb <= m.ih, JSON.stringify(m));
    await ev(() => document.getElementById("pkList").firstElementChild.focus()); await page.keyboard.press("Enter"); await sleep(900);
    const first = await ev(() => ({ crumbs: document.getElementById("pkCrumbs").textContent, up: /^Назад/.test(document.getElementById("pkList").firstElementChild.textContent) }));
    check(`${label}: Enter on a focused row goes into the folder (the keyboard works), and the 'Назад' row appears`, first.crumbs.indexOf("/mnt/ext1›") === 0 && first.up, JSON.stringify(first));
    await ev(() => document.getElementById("pkNewBtn").click()); await sleep(200);
    await page.type("#pkName", "Новая_" + label); await page.keyboard.press("Enter"); await sleep(1100);
    const made = await ev(() => ({ crumbs: document.getElementById("pkCrumbs").textContent, msg: document.getElementById("pkMsg").textContent }));
    check(`${label}: a folder is created with Enter in the name field and the picker goes into it`, made.crumbs.endsWith("›Новая_" + label) && /создана/.test(made.msg), JSON.stringify(made));
    const wrap = await ev(() => { const c = document.getElementById("pkCrumbs").getBoundingClientRect(); return { cr: c.right, iw: innerWidth }; });
    check(`${label}: the breadcrumbs stay inside the popup`, wrap.cr <= wrap.iw);
    await ev(() => document.getElementById("pkChoose").click()); await sleep(1500);
    const added = await ev(() => ({ rows: document.getElementById("fdList").children.length, last: document.getElementById("fdList").lastElementChild.textContent, modal: getComputedStyle(document.getElementById("fdModal")).display, pick: getComputedStyle(document.getElementById("fdPickModal")).display }));
    check(`${label}: choosing a folder on the M.2 closes the picker and adds the folder at once, without any warning`, added.pick === "none" && added.modal === "none" && /M\.2 SSD/.test(added.last) && added.last.indexOf("Новая_" + label) > 0, JSON.stringify(added));
    const lay = await ev(() => { const b = document.getElementById("fdShowAdd").getBoundingClientRect(), m = document.getElementById("fdMsg"), r = m.getBoundingClientRect(), bar = document.getElementById("fdBar").getBoundingClientRect(); return { txt: m.textContent, mleft: Math.round(r.left), bright: Math.round(b.right), mmid: Math.round((r.top + r.bottom) / 2), bmid: Math.round((b.top + b.bottom) / 2), barh: Math.round(bar.height), bh: Math.round(b.height) }; });
    check(`${label}: the message ('Папка добавлена') stands to the RIGHT of '+ Добавить', on its line, and the row is no taller than the button (nothing appears under the button, so the card does not grow and shrink)`, /добавлена/.test(lay.txt) && lay.mleft >= lay.bright && Math.abs(lay.mmid - lay.bmid) <= 6 && lay.barh <= lay.bh + 2, JSON.stringify(lay));
    // exFAT disk: the warning
    await ev(() => document.getElementById("fdShowAdd").click()); await sleep(1000);
    await ev(() => { const s = document.getElementById("pkDisk"); s.value = "/mnt/usb2"; s.dispatchEvent(new Event("change")); }); await sleep(900);
    await ev(() => document.getElementById("pkChoose").click()); await sleep(1200);
    const warn = await ev(() => { const mc = document.querySelector("#fdModal .modal-card").getBoundingClientRect(); return { vis: getComputedStyle(document.getElementById("fdModal")).display, title: document.getElementById("fdWarnTitle").textContent, w: Math.round(mc.width), iw: innerWidth, b: Math.round(mc.bottom), ih: innerHeight, conf: document.getElementById("fdConfirm").getBoundingClientRect().bottom }; });
    check(`${label}: an exFAT disk gives the warning popup that fits the window, with its buttons on screen`, warn.vis === "flex" && /exFAT/.test(warn.title) && warn.w <= warn.iw && warn.b <= warn.ih + 1 && warn.conf <= warn.ih + 1, JSON.stringify(warn));
    await ev(() => document.getElementById("fdCancel").click()); await sleep(300);
    // USB drive eject button: square, the same height as the 'Delete' button, with 'Delete' at the right edge
    await page.evaluate(async () => { await fetch("/api/folders", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ action: "add", path: "/mnt/usb2/Games", confirm: true }) }); });
    await page.goto("http://127.0.0.1:9092/", { waitUntil: "networkidle2" }); await sleep(1500);
    const eg = await page.evaluate(() => { const row = [...document.querySelectorAll("#fdList .fd-row")].find((r) => /usb2/.test(r.textContent)); if (!row) return null; const ej = row.querySelector(".fd-eject"), del = [...row.querySelectorAll("button")].find((b) => b.textContent === "Удалить"); if (!ej || !del) return null; const e = ej.getBoundingClientRect(), d = del.getBoundingClientRect(); return { ew: +e.width.toFixed(1), eh: +e.height.toFixed(1), dh: +d.height.toFixed(1), er: Math.round(e.right), dl: Math.round(d.left), dr: Math.round(d.right), rr: Math.round(row.getBoundingClientRect().right), svg: !!ej.querySelector("svg") }; });
    check(`${label}: the eject button is a SQUARE of the same height as 'Удалить' (not flat), with the eject icon; 'Удалить' is the last button at the right edge`, !!eg && Math.abs(eg.ew - eg.eh) <= 1.5 && Math.abs(eg.eh - eg.dh) <= 1.5 && eg.svg && eg.er <= eg.dl && Math.abs(eg.dr - eg.rr) <= 2, JSON.stringify(eg));
    // remove what was added so the second pass starts clean
    await page.evaluate(async () => { const j = await (await fetch("/api/folders")).json(); for (const f of j.folders) await fetch("/api/folders", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ action: "remove", path: f.path }) }); });
  }
  check("no JavaScript errors on the page", errors.length === 0, errors.join(";"));
  console.log(`\n${pass}/${pass + fail} picker checks passed`); await browser.close(); process.exit(fail ? 1 : 0);
})().catch((e) => { console.log("crashed", e); process.exit(2); });
