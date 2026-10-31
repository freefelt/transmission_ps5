const { JSDOM } = require("jsdom");
const wait = (ms) => new Promise((r) => setTimeout(r, ms));
let pass = 0, fail = 0; const check = (n, ok, e) => { (ok ? pass++ : fail++); console.log((ok ? "PASS " : "FAIL ") + n + (e !== undefined ? "  -> " + e : "")); };
let confirmText = "";
(async () => {
  const errors = []; const dom = await JSDOM.fromURL("http://127.0.0.1:9092/", { runScripts: "dangerously", resources: "usable", pretendToBeVisual: true, beforeParse(w) { w.PS5_ANIM_MS = 0; w.confirm = (t) => { confirmText = t; return true; }; w.addEventListener("error", (e) => errors.push(String(e.message))); } });
  const d = dom.window.document; const $ = (i) => d.getElementById(i); await wait(2500);
  check("no JS errors; the Transmission card exists", errors.length === 0 && !!$("dmBtn") && !!$("dmState"), errors.join(";"));
  check("state shows the daemon as running with its pid; the button offers to stop it", /работает \(pid: \d+/.test($("dmState").textContent) && $("dmBtn").textContent === "Остановить Transmission", $("dmState").textContent + " | " + $("dmBtn").textContent);
  check("one merged section 'Transmission' (no separate 'Состояние' section)", [...d.querySelectorAll("h2")].map((h) => h.textContent).indexOf("Состояние") < 0 && $("dmBtn").closest(".card").contains($("status")));
  check("notification form has 9 inputs (incl. the two disk thresholds and the game pause messages checkbox; the disk panel mode is a select now), the expert form 9 (incl. daemon settings, the restart after a network change and log retention)", d.querySelectorAll("#form input").length === 9 && d.querySelectorAll("#expertForm input").length === 9 && !!$("f_daemon_restart_after_seconds") && !!$("f_daemon_stop_grace_seconds"));
  check("old controls are gone (restart-from-file, socket recreation)", !$("f_restart_elf_path") && !$("dgRebind") && !$("dgToggle") && !$("dgRestart"));
  check("diagnostics buttons present", ["dgAll", "dgProbe", "dgThreads", "dgLog", "dgLogFile", "dgDlog", "dgTlog", "dgSelect"].every((i) => !!$(i)));
  let native = 0; dom.window.confirm = () => { native++; return true; };
  $("dmBtn").click(); await wait(500);
  check("clicking 'Stop' asks for confirmation in OUR popup (not the system window) and explains what happens to the torrents", $("cfmModal").style.display === "flex" && /приостановлены/.test($("cfmText").textContent) && /возобновятся/.test($("cfmText").textContent) && native === 0, $("cfmText").textContent.slice(0, 60));
  $("cfmOk").click(); await wait(800);
  check("while stopping the button is disabled and the state says so", $("dmBtn").disabled || /останавливается|остановлен/.test($("dmState").textContent), $("dmState").textContent);
  let ok = false; for (let i = 0; i < 40 && !ok; i++) { await wait(1000); ok = /остановлен вручную/.test($("dmState").textContent); }
  check("after the stop: 'остановлен вручную', the button offers to start, the note explains", ok && $("dmBtn").textContent === "Запустить Transmission" && /сам не запустится/.test($("dmNote").textContent), $("dmState").textContent + " | " + $("dmBtn").textContent);
  check("the web-UI link is on the right (margin-left:auto)", $("trLink").style.marginLeft === "auto");
  check("the web-UI link is hidden while Transmission is stopped", $("trLink").style.display === "none");
  check("the stale numbers are not shown while Transmission is stopped: both rows hold just a dash (no 'нет данных: …' text, no 'Последний опрос')", /Торренты\s*-/.test($("status").textContent) && /Скорость\s*-/.test($("status").textContent) && !/нет данных/.test($("status").textContent) && !/Последний опрос/.test($("status").textContent) && !/▼/.test($("status").textContent), $("status").textContent.replace(/\s+/g, " ").slice(0, 120));
  $("dmBtn").click(); let sawStarting = false; for (let i = 0; i < 30; i++) { await wait(100); if (/запускается/.test($("dmState").textContent) || $("dmBtn").disabled) sawStarting = true; }
  check("clicking 'Start' shows 'запускается…' (the button is disabled meanwhile); on a fast host the start may finish within a second, so the state is watched, not read once", sawStarting || /работает \(pid/.test($("dmState").textContent), $("dmState").textContent);
  ok = false; for (let i = 0; i < 60 && !ok; i++) { await wait(1000); ok = /работает \(pid/.test($("dmState").textContent); }
  check("the web-UI link is back after the start", $("trLink").style.display !== "none");
  check("the daemon is back: running with a pid, the button offers to stop", ok && $("dmBtn").textContent === "Остановить Transmission" && !$("dmBtn").disabled, $("dmState").textContent);
  $("dgAll").click(); await wait(2500);
  check("'Снимок для диагностики' returns all sections, incl. the daemon log", ["=== Transmission check", "=== threads", "=== wrapper log", "=== daemon wrapper log"].every((s) => $("dgOut").textContent.includes(s)), $("dgOut").textContent.split("\n")[0]);
  $("dgLogFile").click(); await wait(800); check("'Файл журнала менеджера' shows shim.log from the disk (the lines of the manager are in it)", /shim\.log/.test($("dgOut").textContent) && /\b(daemon|monitor|syssvc):/.test($("dgOut").textContent), $("dgOut").textContent.slice(0, 120));
  $("dgAll").click(); await wait(2500); check("the diagnostic snapshot has the manager log FILE as well", /=== manager log file/.test($("dgOut").textContent) && /\b(daemon|monitor|syssvc):/.test(($("dgOut").textContent.split("=== manager log file")[1] || "").split("=== Transmission log")[0]));
  $("dgDlog").click(); await wait(800); check("'Журнал демона' works", /daemon\.log/.test($("dgOut").textContent));
  console.log(`\n${pass}/${pass + fail} page checks passed`); dom.window.close(); process.exit(fail ? 1 : 0);
})().catch((e) => { console.log("crashed", e); process.exit(2); });
