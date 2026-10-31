// The "update available" link in the header line: the browser asks a release endpoint (here a local stub), the link appears only for a NEWER version.
const { JSDOM } = require("jsdom"), http = require("http");
const wait = (ms) => new Promise((r) => setTimeout(r, ms));
let pass = 0, fail = 0; const check = (n, ok, e) => { (ok ? pass++ : fail++); console.log((ok ? "PASS " : "FAIL ") + n + (e !== undefined ? "  -> " + String(e).slice(0, 170) : "")); };
(async () => {
  let reply = { status: 200, body: "{}" }, asked = 0;
  const srv = http.createServer((req, res) => { asked++; res.writeHead(reply.status, { "Content-Type": "application/json", "Access-Control-Allow-Origin": "*" }); res.end(reply.body); });
  await new Promise((r) => srv.listen(0, "127.0.0.1", r)); const url = "http://127.0.0.1:" + srv.address().port + "/latest";
  const open = async (hook) => { const dom = await JSDOM.fromURL("http://127.0.0.1:9092/", { runScripts: "dangerously", resources: "usable", pretendToBeVisual: true, beforeParse(w) { w.PS5_ANIM_MS = 0; w.localStorage.setItem("notif_seen", "0"); w.alert = () => {}; if (hook) w.PS5_UPDATE_URL = url; } }); await wait(2500); return dom; };
  let dom = await open(false); const cur = /Версия ([\d.]+) ·/.exec(dom.window.document.getElementById("top").textContent); dom.window.close();
  check("an automated browser does not ask anything by itself; the header has the plain version line and no link", !!cur && asked === 0, cur && cur[1]);
  const parts = cur[1].split(".").map(Number), bump = (i) => parts.map((v, k) => (k === i ? v + 1 : v)).join("."), lower = parts.map((v, k) => (k === parts.length - 1 ? Math.max(0, v - 1) : v)).join(".");
  const show = async (r) => { reply = r; const d = await open(true); const top = d.window.document.getElementById("top"); const out = { text: top.textContent, a: top.querySelector("a") && { href: top.querySelector("a").href, text: top.querySelector("a").textContent } }; d.window.close(); return out; };
  const next = bump(1), higher = await show({ status: 200, body: JSON.stringify({ tag_name: next }) });
  check("a newer release: 'Версия X (доступно обновление N) · Transmission … · адрес консоли …', and only 'доступно обновление N' is the link, to the page of the latest release", higher.a && higher.a.text === "доступно обновление " + next && higher.a.href === "https://github.com/freefelt/transmission_ps5/releases/latest" && new RegExp("^Версия " + cur[1].replace(/\./g, "\\.") + " \\(доступно обновление " + next.replace(/\./g, "\\.") + "\\) · Transmission [\\d.]+ · адрес консоли ").test(higher.text), JSON.stringify(higher));
  const vtag = await show({ status: 200, body: JSON.stringify({ tag_name: "v" + bump(1) }) });
  check("a tag with a leading 'v' is understood", !!vtag.a && vtag.a.text === "доступно обновление " + bump(1), JSON.stringify(vtag));
  const same = await show({ status: 200, body: JSON.stringify({ tag_name: cur[1] }) });
  check("the same version: no link", !same.a && !/доступно/.test(same.text), JSON.stringify(same));
  const older = await show({ status: 200, body: JSON.stringify({ tag_name: lower }) });
  check("an older release (for example the one that is newer on the console): no link", !older.a, JSON.stringify(older));
  const junk = await show({ status: 200, body: "not json at all" }), bad = await show({ status: 500, body: "{}" }), weird = await show({ status: 200, body: JSON.stringify({ tag_name: "<b>9.9</b>" }) });
  check("garbage, a server error and a tag that is not a version: no link and no error on the page", !junk.a && !bad.a && !weird.a && /^Версия/.test(junk.text), JSON.stringify([junk, bad, weird]));
  srv.close(); console.log(`\n${pass}/${pass + fail} update notice checks passed`); process.exit(fail ? 1 : 0);
})().catch((e) => { console.log("crashed", e); process.exit(2); });
