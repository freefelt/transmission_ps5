const { JSDOM } = require("jsdom");
const fs = require("fs");
const wait = (ms) => new Promise((r) => setTimeout(r, ms));
let pass = 0, fail = 0; const check = (n, ok, e) => { (ok ? pass++ : fail++); console.log((ok ? "PASS " : "FAIL ") + n + (e !== undefined ? "  -> " + String(e).slice(0, 170) : "")); };
(async () => {
  const errors = []; fs.writeFileSync("/tmp/ps5tm_icon_mode", "delay");
  const dom = await JSDOM.fromURL("http://127.0.0.1:9092/", { runScripts: "dangerously", resources: "usable", pretendToBeVisual: true, beforeParse(w) { w.PS5_ANIM_MS = 0; w.confirm = () => true; w.alert = () => {}; w.addEventListener("error", (e) => errors.push(String(e.message))); } });
  const d = dom.window.document, $ = (i) => d.getElementById(i); await wait(2500);
  check("no JS errors; the icon card exists with its buttons", errors.length === 0 && !!$("iconState") && !!$("iconInstall") && !!$("iconRemove"), errors.join(";"));
  { $("iconHead").querySelector(".fd-q").click(); await wait(300);
    check("the '?' in the title of the card explains that a separate short process does the registration", $("fdModal").style.display === "flex" && /transmission_i\.elf/.test($("fdWarnBody").textContent) && /не пострадают/.test($("fdWarnBody").textContent) && $("iconHead").textContent.startsWith("Иконка на вкладке"));
    $("fdCancel").click(); await wait(300); }
  let done = false; for (let i = 0; i < 40 && !done; i++) { await wait(500); done = $("iconState").textContent === "установлена"; }
  check("the automatic installation at the first start finished: 'установлена', the buttons are usable", done && !$("iconInstall").disabled && !$("iconRemove").disabled, $("iconState").textContent);
  const MARK = "/data/Transmission/icon/registered", RES = "/data/Transmission/icon/result", state = async () => (await (await fetch("http://127.0.0.1:9092/api/state")).json()).icon.installed;
  fs.unlinkSync(MARK); const adopted = await state();
  check("an icon installed by an older version (the app folder, our param.json and the helper's answer 'ok', but no 'registered' marker) counts as installed, and the marker is written again", adopted === true && fs.existsSync(MARK));
  fs.unlinkSync(MARK); fs.writeFileSync(RES, "error: test\n"); const notAdopted = await state();
  check("the same files after a FAILED attempt of the helper ('error: ...') are NOT taken for an installation, no marker is written", notAdopted === false && !fs.existsSync(MARK));
  fs.writeFileSync(RES, "ok\n"); await state(); check("(back to 'ok': adopted again)", fs.existsSync(MARK));
  fs.writeFileSync("/tmp/ps5tm_icon_mode", "delay");
  $("iconRemove").click(); await wait(500);
  check("'Удалить' asks first in OUR popup ('Удалить иконку?', the red 'Удалить' button); nothing is removed before the confirmation", $("cfmModal").style.display === "flex" && /Удалить иконку\?/.test($("cfmTitle").textContent) && $("cfmOk").textContent === "Удалить" && $("iconState").textContent === "установлена");
  $("cfmOk").click(); await wait(1300);
  check("'Удалить' returns at once and the card shows the progress", $("iconState").textContent === "выполняется…" && $("iconRemove").disabled);
  done = false; for (let i = 0; i < 40 && !done; i++) { await wait(500); done = $("iconState").textContent === "не установлена"; }
  check("then 'не установлена'", done, $("iconState").textContent);
  fs.writeFileSync("/tmp/ps5tm_icon_mode", "fail");
  $("iconInstall").click(); await wait(2500);
  let err = false; for (let i = 0; i < 30 && !err; i++) { await wait(500); err = /0x80990001/.test($("iconMsg").textContent); }
  check("a failure is shown in red next to the buttons, the buttons stay usable", err && $("iconMsg").className.indexOf("err") >= 0 && !$("iconInstall").disabled && $("iconState").textContent === "не установлена", $("iconMsg").textContent);
  fs.writeFileSync("/tmp/ps5tm_icon_mode", "delay");
  $("iconInstall").click(); await wait(1800);
  check("'Установить' also returns at once: progress, both buttons disabled", $("iconState").textContent === "выполняется…" && $("iconInstall").disabled && $("iconRemove").disabled, $("iconState").textContent);
  fs.unlinkSync("/tmp/ps5tm_icon_mode");
  await wait(7000);
  done = false; for (let i = 0; i < 30 && !done; i++) { await wait(500); done = $("iconState").textContent === "установлена"; }
  check("a retry succeeds: the old error is cleared", done && $("iconMsg").textContent === "", $("iconMsg").textContent);
  console.log(`\n${pass}/${pass + fail} icon page checks passed`); dom.window.close(); process.exit(fail ? 1 : 0);
})().catch((e) => { console.log("crashed", e); process.exit(2); });
