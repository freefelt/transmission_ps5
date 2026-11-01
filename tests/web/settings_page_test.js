const { JSDOM } = require("jsdom");
const wait = (ms) => new Promise((r) => setTimeout(r, ms));
let pass = 0, fail = 0; const check = (n, ok, e) => { (ok ? pass++ : fail++); console.log((ok ? "PASS " : "FAIL ") + n + (e !== undefined ? "  -> " + String(e).slice(0, 170) : "")); };
(async () => {
  const errors = [];
  const dom = await JSDOM.fromURL("http://127.0.0.1:9092/", { runScripts: "dangerously", resources: "usable", pretendToBeVisual: true,
    beforeParse(w) { w.PS5_ANIM_MS = 0; w.localStorage.setItem("notif_seen", "0"); w.alert = () => {}; w.confirm = () => true; w.addEventListener("error", (e) => errors.push(String(e.message))); } });
  const d = dom.window.document, $ = (i) => d.getElementById(i), ev = (el, t) => el.dispatchEvent(new dom.window.Event(t, { bubbles: true }));
  const pos = (a, b) => !!(a.compareDocumentPosition(b) & 4);   // b after a
  const lab = (list) => [...list].map((b) => b.textContent || b.getAttribute("aria-label")).join("|");   // the text of a button, or the name of an icon button
  await wait(2800);
  check("no JS errors; notifications + disk thresholds are shown by default (8 fields incl. the new checkbox for game pause messages; the disk panel mode is a select now)", errors.length === 0 && d.querySelectorAll("#form input").length === 9 && !!$("f_low_disk_warn_gb"), errors.join(";"));
  check("the original Transmission icon is to the left of the two header lines; the tab icon (favicon) is a different one: /favicon.png", !!d.querySelector(".hd img[src='/icon-128.png']") && d.querySelector(".hd img").nextElementSibling.contains($("top")) && /Transmission для PS5/.test(d.querySelector(".hd h1").textContent) && !d.querySelector("link[rel=icon][href='/icon-128.png']") && !!d.querySelector("link[rel=icon][href='/favicon.png']"));
  check("the state line shows pid and the memory of the process: 'работает (pid: N, память: x МиБ)'", /^работает \(pid: \d+, память: \d+\.\d МиБ\)$/.test($("dmState").textContent.trim()), $("dmState").textContent);
  check("the author of the icon is credited (inside the credits dropdown) with a link to their GitHub", (() => { const a = d.querySelector(".credit a[href='https://github.com/Zabriskije']"); return !!a && /Zabriskije/.test(a.textContent) && !!(d.querySelector(".credit").compareDocumentPosition($("accDiag")) & 2); })());
  check("the credits name the sources used: BFpilot, Payload Manager, ShadowMountPlus, websrv/ftpsrv, the SDK and Transmission, with links", (() => { const t = d.querySelector(".credit").textContent; const hrefs = [...d.querySelectorAll(".credit a")].map((a) => a.href); return /BFpilot/.test(t) && /Payload Manager/.test(t) && /ShadowMountPlus/.test(t) && /websrv/.test(t) && /ftpsrv/.test(t) && /ps5-payload-sdk/.test(t) && hrefs.some((h) => /ItsBlurf\/BFpilot/.test(h)) && hrefs.some((h) => /itsPLK\/ps5-payload-manager/.test(h)); })());
  check("the Transmission web theme is a select with 2 options, dark and light (no browser-default option); dark by default", $("f_web_theme").tagName === "SELECT" && [...$("f_web_theme").options].map((o) => o.value).join() === "dark,light" && $("f_web_theme").value === "dark" && $("form").contains($("f_web_theme")));
  { const sel = $("f_web_theme"); sel.value = "light"; ev(sel, "change"); await wait(100);
    check("changing the theme shows the Save/Reset bar like any other setting", $("saveBar").style.display === "flex" && $("save").disabled === false);
    $("reset").click(); await wait(100); check("Reset puts the select back to 'dark'", sel.value === "dark" && $("saveBar").style.display === "none"); }
  { const dk = $("f_web_diskbar"), row = $("f_row_web_diskbar");
    check("'Показывать состояние дисков' is a select with 3 variants (Не показывать / Показывать используемые / Показывать активные) in the 'Веб-интерфейс Transmission' group, 'all' by default, above the theme, with a caption explaining the two modes", !!dk && dk.tagName === "SELECT" && [...dk.options].map((o) => o.value).join() === "off,all,active" && dk.value === "all" && [...dk.options].map((o) => o.textContent).join("|") === "Не показывать|Показывать используемые|Показывать активные" && $("accSettingsBody").contains(dk) && /Показывать состояние дисков/.test(row.textContent) && !!row.querySelector(".fd-q") && pos(dk, $("f_web_theme")), row && row.textContent); }
  { const of = $("f_web_openfolder"), row = $("f_row_web_openfolder");
    check("'Пункт «Открыть папку»' is a select with 3 variants (Не показывать / BFpilot / PS5 Web File Manager), 'off' by default, in the 'Веб-интерфейс Transmission' group, with a caption naming both ports", !!of && of.tagName === "SELECT" && [...of.options].map((o) => o.value).join() === "off,bfpilot,webfm" && of.value === "off" && [...of.options].map((o) => o.textContent).join("|") === "Не показывать|BFpilot|PS5 Web File Manager" && $("accSettingsBody").contains(of) && !!row.querySelector(".fd-q"), row && row.textContent); }
  { const gp = $("f_game_pause"), row = $("f_row_game_pause");
    check("'Пауза во время игры' is a select with 3 variants (Не ставить / Когда игра на экране / Когда игра запущена), 'off' by default, in its own group 'Игры', with a caption that explains how it works (what it does when the game state is unknown, what it does not touch)", !!gp && gp.tagName === "SELECT" && [...gp.options].map((o) => o.value).join() === "off,screen,running" && gp.value === "off" && [...gp.options].map((o) => o.textContent).join("|") === "Не ставить|Когда игра на экране|Когда игра запущена" && $("accSettingsBody").contains(gp) && row.querySelector(".help").textContent.length < 120 && /Пока запущена игра/.test(row.textContent) && !/остановили сами/.test(row.textContent) && [...d.querySelectorAll("#form .help")].some((e) => e.textContent === "Игры"), row && row.textContent);
    { const q = row.querySelector(".fd-q"); q.click(); await wait(300);
      check("the short caption has a '?': it opens the full description in the popup (the title is the name of the setting, 4 paragraphs: what it does not touch, what happens when the game state is unknown, the 30 s delay), nothing is wrong with the select", !!q && $("fdModal").style.display === "flex" && $("fdWarnTitle").textContent === "Пауза во время игры" && $("fdWarnBody").querySelectorAll("p").length === 4 && /остановили сами/.test($("fdWarnBody").textContent) && /не удаётся/.test($("fdWarnBody").textContent) && !/системная служба/.test($("fdWarnBody").textContent) && $("fdConfirm").style.display === "none" && $("fdCancel").textContent === "Понятно" && gp.value === "off");
      $("fdCancel").click(); await wait(300); check("'Понятно' closes it", $("fdModal").style.display === "none"); }
    { const cb = $("f_notify_game_pause"), before = cb.checked; const q2 = $("f_row_notify_game_pause").querySelector(".fd-q"); q2.click(); await wait(300);
      check("a '?' inside a checkbox label does NOT toggle the checkbox, and the popup opens", cb.checked === before && $("fdModal").style.display === "flex" && $("fdWarnTitle").textContent === "Пауза и возобновление закачек во время игры"); $("fdCancel").click(); await wait(300); } }
  { const ic = $("f_web_icons");
    check("'Значки' is a select with 2 variants (color, mono) in the 'Веб-интерфейс Transmission' group, color by default, above the theme", !!ic && ic.tagName === "SELECT" && [...ic.options].map((o) => o.value).join() === "color,mono" && ic.value === "color" && $("accSettingsBody").contains(ic) && pos(ic, $("f_web_theme")) && /Цветные/.test(ic.textContent) && /Одноцветные/.test(ic.textContent)); }
  { const css = [...d.querySelectorAll("style")].map((e) => e.textContent).join(""); const m = css.match(/\.row\.sub\{([^}]*)\}/);
    check("nested rows (under 'Мало места на диске' and the sleep notification) keep an indent of 30px but have NO horizontal separator above and NO vertical bar", !!m && /margin-left:30px/.test(m[1]) && /border-top:0/.test(m[1]) && !/border-left/.test(m[1]) && !/padding-left/.test(m[1]), m && m[1]); }
  check("the sleep warnings are no longer on the page: no warning blocks, no 'Скрыть предупреждения' button and no hide dialog; they are shown in a popup when 'Активировать' is pressed", !$("sleepWarns") && !$("kstuffWarn") && !$("offWarn") && !$("warnHide") && !$("warnModal") && !/Скрыть предупреждения/.test(d.body.textContent) && !!$("actModal") && $("actModal").style.display === "none");
  { const realApi = dom.window.api, calls = []; const stub = (url, body, cb) => { calls.push([url, body]); if (url === "/api/state") return realApi(url, body, cb); cb(200, { ok: true }); };
    const st0 = JSON.parse(JSON.stringify(dom.window.state.config)), ver = dom.window.state.version, kst = dom.window.state.kstuff;
    dom.window.actOpen(); await wait(300);
    let txt = $("actModal").textContent;
    check("the warning for going to sleep: a popup with the text that used to stand on the page (not tested on all firmware, may hang or kernel panic, Transmission stops cleanly and starts again after wake-up), the 'don't show again' checkbox, 'Отмена' and 'Активировать'; no kstuff paragraph while kstuff is not running", $("actModal").style.display === "flex" && /Переход в режим сна/.test($("actTitle").textContent) && /проверялся не на всех прошивках/.test(txt) && /kernel panic/.test(txt) && /активные торренты приостанавливаются/.test(txt) && !/kstuff/.test(txt) && $("actHideRow").style.display !== "none" && /Больше не показывать это предупреждение/.test($("actHideRow").textContent) && /Отмена/.test($("actCancel").textContent) && /Активировать/.test($("actGo").textContent), $("actTitle").textContent);
    $("actCancel").click(); await wait(300);
    check("'Отмена' closes it and nothing is sent", $("actModal").style.display === "none" && calls.length === 0);
    dom.window.state.kstuff = true; dom.window.actOpen(); await wait(200);
    check("with kstuff running the popup adds the kstuff paragraph (etaHEN/kstuff may hang the console instead of sleeping)", /Сейчас запущен kstuff/.test($("actBody").textContent) && /etaHEN\/kstuff/.test($("actBody").textContent));
    d.dispatchEvent(new dom.window.KeyboardEvent("keydown", { key: "Escape", keyCode: 27, bubbles: true })); await wait(300); dom.window.state.kstuff = kst;
    check("Escape closes the popup too", $("actModal").style.display === "none");
    dom.window.state.config.autosleep_action = "poweroff"; dom.window.actOpen(); await wait(200); txt = $("actModal").textContent;
    check("the warning for powering off: its own title and text (HALT and POWEROFF, no normal shutdown, other apps and services cannot close, use sleep for the safety of data) and NO 'don't show again' checkbox", /Выключение консоли/.test($("actTitle").textContent) && /HALT и POWEROFF/.test(txt) && /сохранения игр/.test(txt) && !/проверялся не на всех прошивках/.test(txt) && $("actHideRow").style.display === "none", $("actTitle").textContent);
    $("actCancel").click(); await wait(200); dom.window.state.config.autosleep_action = "sleep";
    dom.window.api = stub; dom.window.actOpen(); await wait(200); $("actHide").checked = true; $("actGo").click(); await wait(500);
    const order = calls.filter((c) => c[0] !== "/api/state").map((c) => c[0] + ":" + JSON.stringify(c[1]));
    check("'Активировать' with the checkbox ticked saves 'hidden until the next version' first, then arms", $("actModal").style.display === "none" && order[0] === "/api/config:" + JSON.stringify({ sleep_warnings_hidden: ver }) && order[1] === "/api/autosleep:" + JSON.stringify({ action: "arm" }), order);
    calls.length = 0; dom.window.state.config.sleep_warnings_hidden = ver; $("asBtn").disabled = false; $("asBtn").click(); await wait(400);
    check("when the warnings are hidden for this version the 'Активировать' button arms at once, without the popup (for sleep)", $("actModal").style.display === "none" && calls.some((c) => c[0] === "/api/autosleep" && c[1].action === "arm"), calls.map((c) => c[0]));
    calls.length = 0; dom.window.state.config.autosleep_action = "poweroff"; $("asBtn").disabled = false; $("asBtn").click(); await wait(400);
    check("but for powering off the popup is always shown, even if the sleep warnings are hidden", $("actModal").style.display === "flex" && !calls.some((c) => c[0] === "/api/autosleep"), calls.map((c) => c[0]));
    $("actCancel").click(); await wait(200);
    dom.window.api = realApi; Object.assign(dom.window.state.config, st0); }
  check("the section is now 'При завершении всех загрузок' (was 'Режим сна'); the checkbox is worded 'Выполнить действие при завершении всех загрузок'", [...d.querySelectorAll("h2")].some((h) => h.firstChild.textContent === "При завершении всех загрузок") && ![...d.querySelectorAll("h2")].some((h) => h.textContent === "Режим сна") && $("asRow").querySelector("label").firstChild.textContent === "Выполнить действие при завершении всех загрузок" && !/Переходить/.test(d.body.textContent));
  { const ac = $("sf_autosleep_action"), mr = $("f_row_sleep_method");
    check("the action list 'Действие' (Уйти в режим сна / Выключить консоль) is the first setting of the section, 'sleep' by default", !!ac && ac.tagName === "SELECT" && [...ac.options].map((o) => o.value).join() === "sleep,poweroff" && [...ac.options].map((o) => o.textContent).join("|") === "Уйти в режим сна|Выключить консоль" && ac.value === "sleep" && $("sleepForm").firstElementChild.contains(ac), ac && ac.value);
    const shown = (e) => e.style.display !== "none";
    check("'Способ перехода в режим сна' is in the Advanced settings (group 'Режим сна'), not in the sleep box, and always shown (it does not depend on the action)", !!mr && $("accExpertBody").contains(mr) && !$("sleepForm").contains(mr) && !$("sf_sleep_method") && shown(mr) && (ac.value = "poweroff", ev(ac, "change"), await wait(500), shown(mr)) && (ac.value = "sleep", ev(ac, "change"), await wait(500), shown(mr)) && [...d.querySelectorAll("#expertForm .help")].some((e) => e.textContent === "Режим сна"));
    check("the sleep method list says 'Системный запрос (рекомендуется)' without any word about a helper", [...$("f_sleep_method").options].map((o) => o.textContent).join("|") === "Системный запрос (рекомендуется)|Прямой (флаг системы)" && !/помощник/.test($("sleepForm").textContent)); }
  { const cb = $("accCreditsBtn"), body = $("accCreditsBody"), cr = d.querySelector(".credit");
    check("ALL the credit text (author of the icon, sources, icon sets) is under the closed dropdown 'Благодарности и лицензии'", !!cb && /Благодарности и лицензии/.test(cb.textContent) && cb.getAttribute("aria-expanded") === "false" && body.style.display === "none" && body.contains(cr) && !!cb.querySelector("svg.chev") && d.querySelectorAll(".credit").length === 1 && /Zabriskije/.test(cr.textContent) && /BFpilot/.test(cr.textContent) && /Lucide/.test(cr.textContent));
    cb.click(); await wait(100);
    check("clicking it opens the credits (chevron flips); the state is NOT remembered (all three sections are collapsed at every page load)", body.style.display === "block" && cb.classList.contains("open") && dom.window.localStorage.getItem("acc_accCredits") === null);
    cb.click(); await wait(100);
    check("and a second click closes it again", body.style.display === "none" && !cb.classList.contains("open")); }
  { const rp = d.querySelector(".repo"), a = rp && rp.querySelector("a");
    check("the line 'Репозиторий проекта: https://github.com/freefelt/Transmission-PS5' is always visible at the bottom (outside any dropdown), as a link that opens in a new tab", !!rp && rp.textContent.trim() === "Репозиторий проекта: https://github.com/freefelt/Transmission-PS5" && !!a && a.href === "https://github.com/freefelt/Transmission-PS5" && a.target === "_blank" && /noopener/.test(a.rel) && !["accSettingsBody", "accExpertBody", "accCreditsBody"].some((id) => $(id).contains(rp)) && rp.style.display !== "none"); }
  { const b = $("dgDisks"); b.click(); await wait(1800);
    check("the Diagnostics section has a 'Диски' button: it shows which of /mnt/ext0..7 and /mnt/usb0..7 are connected", !!b && $("accDiagBody").contains(b) && /\/mnt\/usb0/.test($("dgOut").textContent) && /\/mnt\/ext7/.test($("dgOut").textContent) && /Whether the drive is connected/.test($("dgOut").textContent), $("dgOut").textContent.slice(0, 120)); }
  // --- "Download folders"
  { const card = $("fdCard"), post = (b) => fetch("http://127.0.0.1:9092/api/folders", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(b) }).then((r) => r.json().then((j) => [r.status, j]));
    check("the 'Папки загрузок' card is right after the Transmission card and before the Sleep section", !!card && pos($("dmBtn"), card) && pos(card, $("asArm")) && /Папки загрузок/.test(card.previousElementSibling.textContent));
    dom.window.fdRefresh(); await wait(800);
    check("a new installation: the empty list shows the hint that no torrent can be added until a folder is added", $("fdEmpty").style.display !== "none" && /нельзя/.test($("fdEmpty").textContent) && $("fdList").children.length === 0);
    check("the '+ Добавить' button sits lower (22px above it) and the messages have their place to the RIGHT of it, in the same row (no row of their own under it that would grow the card)", $("fdBar").style.marginTop === "22px" && !$("fdMsgRow") && $("fdMsg").parentNode === $("fdBar") && $("fdMsg").previousElementSibling === $("fdShowAdd") && $("fdMsg").textContent === "");
  { const realApi = dom.window.api, calls = []; let native = 0; dom.window.confirm = () => { native++; return true; }; dom.window.alert = () => { native++; };
    dom.window.api = (url, body, cb) => { calls.push([url, body]); if (url === "/api/state") return realApi(url, body, cb); cb(200, { ok: true }); };
    const running = dom.window.state && dom.window.state.daemon && (dom.window.state.daemon.state === "running" || dom.window.state.daemon.state === "not_responding");
    $("dmBtn").click(); await wait(300);
    check("'Остановить Transmission' asks in OUR popup (not the system window): title, the explanation about paused torrents, the red 'Остановить' and 'Отмена'; nothing is sent yet", running === true && $("cfmModal").style.display === "flex" && /Остановить Transmission\?/.test($("cfmTitle").textContent) && /приостановлены и возобновятся/.test($("cfmText").textContent) && $("cfmOk").textContent === "Остановить" && $("cfmOk").className === "danger" && $("cfmCancel").style.display !== "none" && native === 0 && !calls.some((c) => c[0] === "/api/daemon"), [running, $("cfmTitle").textContent, native]);
    $("cfmCancel").click(); await wait(300);
    check("'Отмена' closes it and nothing is sent", $("cfmModal").style.display === "none" && !calls.some((c) => c[0] === "/api/daemon"));
    $("dmBtn").click(); await wait(300); d.dispatchEvent(new dom.window.KeyboardEvent("keydown", { key: "Escape", keyCode: 27, bubbles: true })); await wait(300);
    check("Escape closes it too", $("cfmModal").style.display === "none" && !calls.some((c) => c[0] === "/api/daemon"));
    $("dmBtn").click(); await wait(300); $("cfmOk").click(); await wait(400);
    check("'Остановить' sends the stop request", calls.some((c) => c[0] === "/api/daemon" && c[1].action === "stop") && $("cfmModal").style.display === "none" && native === 0, calls.map((c) => c[0]));
    calls.length = 0; dom.window.api = (url, body, cb) => { calls.push([url, body]); if (url === "/api/state") return realApi(url, body, cb); cb(409, { message: "Что-то пошло не так" }); };
    dom.window.actArm(); await wait(400);
    check("an error message is shown in the same popup with one 'OK' button (no system alert)", $("cfmModal").style.display === "flex" && $("cfmTitle").textContent === "Не удалось" && /Что-то пошло не так/.test($("cfmText").textContent) && $("cfmCancel").style.display === "none" && $("cfmOk").textContent === "OK" && $("cfmOk").className === "" && native === 0, [$("cfmTitle").textContent, native]);
    $("cfmOk").click(); await wait(300);
    check("'OK' closes it", $("cfmModal").style.display === "none");
    if ($("iconRemove")) { $("iconRemove").disabled = false; $("iconRemove").click(); await wait(300);
      check("'Удалить иконку' asks in our popup as well", $("cfmModal").style.display === "flex" && /Удалить иконку\?/.test($("cfmTitle").textContent) && $("cfmOk").textContent === "Удалить" && native === 0); $("cfmCancel").click(); await wait(300); }
    dom.window.api = realApi; delete dom.window.confirm; delete dom.window.alert; }
    check("there is no inline add form any more: only the '+ Добавить' button; the folder is chosen in a popup", !$("fdAdd") && !$("fdPath") && !$("fdDisk") && !!$("fdPickModal") && $("fdPickModal").style.display === "none" && /\+ Добавить/.test($("fdShowAdd").textContent));
    $("fdShowAdd").click(); await wait(900);
    check("'+ Добавить' opens the picker with the disk select on top: the mounted /mnt/usb2 (USB drive, its file system, size, 'не рекомендуется' for exFAT) and the internal disk /data last", $("fdPickModal").style.display === "flex" && [...$("pkDisk").options].some((o) => o.value === "/mnt/usb2" && /USB-диск/.test(o.textContent) && /exFAT/.test(o.textContent) && /не рекомендуется/.test(o.textContent) && /свободно/.test(o.textContent)) && $("pkDisk").options[$("pkDisk").options.length - 1].value === "/data" && /не рекомендуется/.test($("pkDisk").options[$("pkDisk").options.length - 1].textContent), [...$("pkDisk").options].map((o) => o.textContent));
    check("the default disk is the M.2 (if connected), otherwise the first USB drive, otherwise /data; here a USB drive is the only external disk: it is chosen and its contents are listed under the breadcrumbs", $("pkDisk").value === "/mnt/usb2" && $("pkCrumbs").textContent === "/mnt/usb2" && !/Назад/.test($("pkList").textContent), [$("pkDisk").value, $("pkCrumbs").textContent]);
    $("pkNewBtn").click(); await wait(100);
    check("'Новая папка' reveals the name field with 'Создать' and 'Отмена'", $("pkNew").style.display === "flex" && /Создать/.test($("pkMake").textContent) && /Отмена/.test($("pkMakeCancel").textContent));
    $("pkName").value = "torrents"; $("pkMake").click(); await wait(1000);
    check("creating a folder: the picker goes into it (breadcrumbs /mnt/usb2 › torrents), says 'Папка создана', and the 'Назад' row is the first row of the list", $("pkCrumbs").textContent === "/mnt/usb2›torrents" && /Папка создана/.test($("pkMsg").textContent) && /^Назад/.test($("pkList").firstElementChild.textContent) && /Вложенных папок нет/.test($("pkList").textContent), [$("pkCrumbs").textContent, $("pkMsg").textContent]);
    $("pkNewBtn").click(); await wait(100); $("pkName").value = "a/b"; $("pkMake").click(); await wait(700);
    check("a bad name is refused with a red message and the picker stays in the same folder", $("pkMsg").classList.contains("bad") && $("pkCrumbs").textContent === "/mnt/usb2›torrents", $("pkMsg").textContent);
    $("pkMakeCancel").click(); await wait(100);
    $("pkList").firstElementChild.click(); await wait(800);
    check("'Назад' goes up to the disk root, and the new folder is in the list", $("pkCrumbs").textContent === "/mnt/usb2" && [...$("pkList").children].some((r) => /torrents/.test(r.textContent)), $("pkCrumbs").textContent);
    [...$("pkList").children].find((r) => /torrents/.test(r.textContent)).click(); await wait(800);
    check("a click on a folder goes into it; the breadcrumb of the disk root is a button that goes back", $("pkCrumbs").textContent === "/mnt/usb2›torrents" && $("pkCrumbs").firstElementChild.tagName === "BUTTON");
    d.dispatchEvent(new dom.window.KeyboardEvent("keydown", { key: "Escape", keyCode: 27, bubbles: true })); await wait(300);
    check("Escape closes the picker", $("fdPickModal").style.display === "none" && $("fdList").children.length === 0);
    $("fdShowAdd").click(); await wait(900); [...$("pkList").children].find((r) => /torrents/.test(r.textContent)).click(); await wait(800);
    $("pkChoose").click(); await wait(1000);
    check("choosing a folder on an exFAT disk opens the warning with the explanation (no sparse files, the fix, format the drive with the console, all data is deleted) and the 'Всё равно добавить' button; nothing is added yet", $("fdModal").style.display === "flex" && /exFAT/.test($("fdWarnTitle").textContent) && /разреженных/.test($("fdModal").textContent) && /Настройки → Хранилище/.test($("fdModal").textContent) && /данные на диске удаляются/.test($("fdModal").textContent) && $("fdConfirm").style.display !== "none" && /Отмена/.test($("fdCancel").textContent) && $("fdList").children.length === 0, $("fdWarnTitle").textContent);
    $("fdCancel").click(); await wait(300);
    check("'Отмена' closes the warning and adds nothing", $("fdModal").style.display === "none" && $("fdList").children.length === 0);
    dom.window.fdAdd("/mnt/usb2/torrents", false); await wait(700); $("fdConfirm").click(); await wait(1300);
    check("'Всё равно добавить' adds it: the row shows the path, the kind, free space, the red 'не рекомендуется (exFAT)' and a '?' button; the empty hint disappears", $("fdList").children.length === 1 && /\/mnt\/usb2\/torrents/.test($("fdList").textContent) && /USB-диск · свободно/.test($("fdList").textContent) && $("fdEmpty").style.display === "none" && /добавлена/.test($("fdMsg").textContent) && (() => { const r = $("fdList").children[0], sp = r.querySelector(".fd-red"), q = r.querySelector(".fd-q"); return !!sp && /^не рекомендуется \(exFAT\)$/.test(sp.textContent) && !!q && q.textContent === "?"; })(), $("fdList").textContent);
    { const q = $("fdList").children[0].querySelector(".fd-q"); q.click(); await wait(300);
      check("the '?' opens the same explanation as at adding, but only for reading: no add button, the close button says 'Понятно'", $("fdModal").style.display === "flex" && /exFAT/.test($("fdWarnTitle").textContent) && /разреженных/.test($("fdModal").textContent) && $("fdConfirm").style.display === "none" && $("fdCancel").textContent === "Понятно" && !/Всё равно добавить папку/.test($("fdModal").textContent));
      $("fdCancel").click(); await wait(300);
      check("'Понятно' closes it; when the warning opens again for adding the buttons are back ('Отмена' and 'Всё равно добавить')", $("fdModal").style.display === "none"); }
  { const row = $("fdList").children[0], all = [...row.querySelectorAll("button")], eject = row.querySelector(".fd-eject");
    check("the row of a USB drive: the '?' of the warning, three square icon buttons without text (eject, open the folder, delete; each is named for readers and in the tooltip) with 'delete' as the LAST one, at the right edge", !!eject && eject.textContent === "" && eject.getAttribute("aria-label") === "Размонтировать диск" && eject.title === "Размонтировать диск" && !!eject.querySelector("svg") && lab(all) === "?|Размонтировать диск|Открыть папку|Удалить" && all.every((b, i) => i === 0 || (b.textContent === "" && !!b.querySelector("svg") && b.title === b.getAttribute("aria-label"))) && all[all.length - 1].classList.contains("fd-del") && all.indexOf(eject) === all.length - 3, lab(all));
    eject.click(); await wait(300);
    check("'Размонтировать диск' asks first: a popup with the path and the explanation (torrents are stopped, the other partitions are unmounted too); only 'Отмена' and 'Размонтировать'; nothing is sent yet", $("fdUmModal").style.display === "flex" && $("umPath").textContent === "/mnt/usb2/torrents" && /остановлены/.test($("umBody").textContent) && /разделами/.test($("umBody").textContent) && $("umGo").style.display !== "none" && $("umForce").style.display === "none" && $("fdList").children.length === 1, $("umTitle").textContent);
    $("umCancel").click(); await wait(300);
    check("'Отмена' closes it and the drive stays mounted (the folder row is still 'connected')", $("fdUmModal").style.display === "none" && !$("fdList").children[0].classList.contains("off"));
    const realApi = dom.window.api; let calls = []; const um = () => calls.filter((b) => b && b.action === "unmount");
    dom.window.api = (url, body, cb) => { calls.push(body); if (body && body.force) cb(200, { ok: true, message: "USB-диск /mnt/usb2 извлечён принудительно" }); else cb(409, { busy: true, error: "занят", holders: [{ pid: 123, name: "SceShellCore", kind: "system", count: 2, sample: "/mnt/usb2/torrents/a.bin" }, { pid: 456, name: "BFpilot", kind: "app", count: 1, sample: "/mnt/usb2/x" }] }); };
    dom.window.umOpen({ path: "/mnt/usb2/torrents", device: "/dev/da1p1", kind: "usb" }); await wait(200); $("umGo").click(); await wait(300);
    check("a busy drive: the popup says 'Диск занят', lists who holds it (system / application, name, pid, number of open files, a sample path) and shows the red 'Извлечь принудительно' button with the explanation", $("umTitle").textContent === "Диск занят" && /Кто держит диск/.test($("umHolders").textContent) && /система: SceShellCore \(pid 123\), открытых файлов: 2/.test($("umHolders").textContent) && /приложение: BFpilot \(pid 456\)/.test($("umHolders").textContent) && /a\.bin/.test($("umHolders").textContent) && $("umForce").style.display !== "none" && $("umGo").style.display === "none" && /Принудительное извлечение сначала сбрасывает данные/.test($("umBody").textContent) && !/расширенное хранилище/.test($("umBody").textContent) && um().length === 1 && !um()[0].force, [$("umTitle").textContent, $("umHolders").textContent.slice(0, 160)]);
    $("umForce").click(); await wait(300);
    check("'Извлечь принудительно' sends the same request with force=true and shows the result with a close button 'Готово'", um().length === 2 && um()[1].force === true && $("umTitle").textContent === "Диск размонтирован" && /принудительно/.test($("umBody").textContent) && $("umCancel").textContent === "Готово" && $("umForce").style.display === "none");
    dom.window.api = (url, body, cb) => cb(409, { busy: true, error: "занят", holders: [] });
    dom.window.umOpen({ path: "/mnt/usb2/torrents", device: "/dev/da1p1", kind: "usb" }); await wait(200);
    $("umGo").click(); await wait(300);
    dom.window.api = realApi;
    dom.window.fdRender({ folders: [{ path: "/mnt/ext0/torrents", kind: "usb", mounted: true, exists: true, free: 1e12, total: 2e12, fs: "UFS", fsbad: false, device: "/dev/es0.crypt" }], candidates: [], limit: 20 }); await wait(400);
    { const row = $("fdList").children[0], q = row.querySelector(".fd-q");
      check("the extended storage of the console (/dev/es*) HAS the eject button (the system disconnects it): '?', the eject icon without text and 'Удалить', with the plain note '· расширенное хранилище консоли'", !!row.querySelector(".fd-eject") && /расширенное хранилище консоли/.test(row.textContent) && !row.querySelector(".fd-red") && !!q && lab(row.querySelectorAll("button")) === "?|Размонтировать диск|Открыть папку|Удалить");
      q.click(); await wait(300);
      check("the '?' explains that the system disconnects the drive and that the console has its own way too ('Настройки → Хранилище → … «Безопасно удалить из PS5»'); read only: 'Понятно'", $("fdModal").style.display === "flex" && /Расширенное хранилище консоли/.test($("fdWarnTitle").textContent) && /Безопасно удалить из PS5/.test($("fdModal").textContent) && $("fdConfirm").style.display === "none" && $("fdCancel").textContent === "Понятно");
      $("fdCancel").click(); await wait(300); dom.window.fdRefresh(); await wait(700); dom.window.api = (url, body, cb) => cb(409, { busy: true, error: "занят", holders: [] }); }
    check("when nobody can be named the popup says so ('вероятнее всего, система') and still offers the forced eject", /вероятнее всего, система/.test($("umHolders").textContent) && $("umForce").style.display !== "none");
    dom.window.api = (url, body, cb) => cb(409, { error: "Не удалось размонтировать /mnt/usb2" }); $("umForce").click(); await wait(300);
    check("another failure: the popup shows the message in red with a 'Закрыть' button", $("umTitle").textContent === "Не удалось размонтировать диск" && /Не удалось размонтировать/.test($("umBody").textContent) && $("umCancel").textContent === "Закрыть" && !!$("umBody").querySelector(".fd-red"));
    d.dispatchEvent(new dom.window.KeyboardEvent("keydown", { key: "Escape" })); await wait(300); dom.window.api = realApi;
    check("Escape closes the unmount popup", $("fdUmModal").style.display === "none"); }
    { const css = [...d.querySelectorAll("style")].map((e) => e.textContent).join(""), rows = [...$("fdList").children], ic = rows[0].querySelector(".fd-ico");
      const img = (k) => (css.match(new RegExp("\\.fd-ico\\." + k + "\\{--ico-color:(url\\([^;]*\\));--ico-mono:(url\\([^;}]*\\))")) || []).slice(1);
      check("every folder row has a disk icon matching its kind (the flash drive here), placed before the path", !!ic && ic.classList.contains("usb") && ic === rows[0].firstElementChild && !rows[0].classList.contains("off"));
      check("three different icons, each in a colored and a mono variant (M.2 SSD, flash drive, internal disk)", ["ssd", "usb", "int"].every((k) => img(k).length === 2 && img(k).every((u) => /data:image\/svg/.test(u))) && new Set(["ssd", "usb", "int"].map((k) => img(k)[0])).size === 3 && new Set(["ssd", "usb", "int"].map((k) => img(k)[1])).size === 3);
      const w = await (await fetch("http://127.0.0.1:9092/api/config", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ web_icons: "mono" }) })).status; dom.window.refresh(); await wait(1500);
      check("with the 'Значки: Одноцветные' setting the icons switch to the mono style (class fd-mono on the card), and back to colors", w === 200 && $("fdCard").classList.contains("fd-mono") && (await fetch("http://127.0.0.1:9092/api/config", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ web_icons: "color" }) }), dom.window.refresh(), await wait(1500), !$("fdCard").classList.contains("fd-mono")));
      check("the success message ('Папка добавлена') disappears by itself after a few seconds", (await wait(2500), $("fdMsg").textContent === "")); }
    dom.window.fdAdd("/mnt/usb3/torrents", false); await wait(900);
    check("an unplugged disk is refused with a red message", /не подключён/.test($("fdMsg").textContent) && $("fdMsg").classList.contains("bad") && $("fdList").children.length === 1);
    dom.window.fdAdd("/data/pagetest", false); await wait(900);
    check("an internal-disk path opens the warning 'Внутренний диск: так лучше не делать' with 'лучше использовать внешний USB-диск или M.2 SSD'", $("fdModal").style.display === "flex" && /Внутренний диск/.test($("fdWarnTitle").textContent) && /Лучше использовать внешний USB-диск или M\.2 SSD/.test($("fdModal").textContent) && $("fdConfirm").style.display !== "none" && $("fdList").children.length === 1);
    $("fdCancel").click(); await wait(300);
    check("'Отмена' closes the warning and adds nothing", $("fdModal").style.display === "none" && $("fdList").children.length === 1);
    dom.window.fdAdd("/data/pagetest", false); await wait(700); $("fdConfirm").click(); await wait(1300);
    check("'Всё равно добавить' adds it; the row is marked 'не рекомендуется' in RED with a '?' button; the '?' explains it without the add button", $("fdList").children.length === 2 && /Внутренний диск/.test($("fdList").textContent) && (() => { const sp = $("fdList").children[1].querySelector(".fd-red"), q = $("fdList").children[1].querySelector(".fd-q"); return !!sp && sp.textContent === "не рекомендуется" && !!q; })());
    { const q = $("fdList").children[1].querySelector(".fd-q"); q.click(); await wait(300);
      check("the '?' of an internal disk opens the explanation 'Внутренний диск: так лучше не делать' for reading only", $("fdModal").style.display === "flex" && /Внутренний диск/.test($("fdWarnTitle").textContent) && $("fdConfirm").style.display === "none" && $("fdCancel").textContent === "Понятно");
      $("fdCancel").click(); await wait(300); }
  { const rows = [...$("fdList").children], internal = rows.find((r) => /Внутренний диск/.test(r.textContent));
    check("a folder on the internal disk has 'Удалить' and the '?' of the warning, but no unmount", !!internal && lab(internal.querySelectorAll("button")) === "?|Открыть папку|Удалить", internal && lab(internal.querySelectorAll("button"))); }
    { const w = dom.window, row = $("fdList").children[0], op = row.querySelector(".fd-open"), path = row.querySelector(".fd-path").textContent; let opened = [];
      w.open = (u, t) => { opened.push([u, t]); };
      const off = w.getComputedStyle(op).display; op.click();
      w.state.config.web_openfolder = "bfpilot"; w.render(w.state); const on = w.getComputedStyle(op).display; op.click();
      w.state.config.web_openfolder = "webfm"; w.render(w.state); op.click();
      w.state.config.web_openfolder = "off"; w.render(w.state); const off2 = w.getComputedStyle(op).display;
      check("'Открыть папку' next to a download folder: hidden while the 'Open folder' setting is off (and does nothing); with BFpilot it opens http://<host>:5905/#<path> in a new tab, with the web file manager :8888", off === "none" && off2 === "none" && on !== "none" && opened.length === 2 && opened[0][0] === "http://127.0.0.1:5905/#" + encodeURIComponent(path) && opened[0][1] === "_blank" && opened[1][0] === "http://127.0.0.1:8888/#" + encodeURIComponent(path), JSON.stringify([off, on, off2, opened])); }
    for (let i = 0; i < 6; i++) await post({ action: "add", path: "/mnt/usb2/f" + i, confirm: true });
    dom.window.fdRefresh(); await wait(900);
    check("at 8 folders the add form is hidden and the limit message is shown (config.cfg can hold more)", $("fdList").children.length === 8 && $("fdShowAdd").style.display === "none" && $("fdBar").style.display !== "none" && $("fdLimit").style.display !== "none" && /config\.cfg/.test($("fdLimit").textContent));
    { const delBtn = (r) => r.querySelector(".fd-del"), first = delBtn($("fdList").children[0]), path0 = $("fdList").children[0].querySelector(".fd-path").textContent, n0 = $("fdList").children.length;
      first.click(); await wait(200);
      check("'Удалить' first asks for confirmation: a dialog with the path, saying that files and torrents are not deleted", $("fdDelModal").style.display === "flex" && $("fdDelPath").textContent === path0 && /не удаляются/.test($("fdDelModal").textContent) && $("fdList").children.length === n0);
      $("fdDelCancel").click(); await wait(300);
      check("'Отмена' closes it and the folder stays", $("fdDelModal").style.display === "none" && $("fdList").children.length === n0);
      first.click(); await wait(200); d.dispatchEvent(new dom.window.KeyboardEvent("keydown", { key: "Escape", keyCode: 27, bubbles: true })); await wait(200);
      check("Escape closes the delete dialog too", $("fdDelModal").style.display === "none" && $("fdList").children.length === n0);
      first.click(); await wait(200); $("fdDelConfirm").click(); await wait(1000);
      check("'Удалить из списка' removes exactly that folder and says so", $("fdList").children.length === n0 - 1 && ![...$("fdList").querySelectorAll(".fd-path")].some((e) => e.textContent === path0) && /Папка удалена/.test($("fdMsg").textContent));
      await wait(3600);
      check("the message 'Папка удалена' disappears by itself (it used to stay forever)", $("fdMsg").textContent === ""); }
    for (let n = $("fdList").children.length; n > 0; n--) { $("fdList").children[0].querySelector(".fd-del").click(); await wait(250); $("fdDelConfirm").click(); await wait(700); }
    await wait(800);
    check("with every folder deleted (each with its confirmation) the hint is back and the '+ Добавить' button too ", $("fdList").children.length === 0 && $("fdEmpty").style.display !== "none" && $("fdShowAdd").style.display !== "none"); }
  check("the sources line says 'которые помогли разработке'", /Примеры и исходники, которые помогли разработке/.test(d.querySelector(".credit").textContent) && !/по которым сделано/.test(d.querySelector(".credit").textContent));
  { const n = $("f_notify_low_disk_space"), w = $("f_row_low_disk_warn_gb"), c = $("f_row_low_disk_critical_gb"); const was = n.checked;
    check("the two disk thresholds are shown under the 'Мало места на диске' checkbox while it is on", was === true && w.style.display !== "none" && c.style.display !== "none" && w.classList.contains("sub"));
    n.checked = false; ev(n, "change"); await wait(100);
    check("they are hidden when the checkbox is off (and the Save bar appears for the change)", w.style.display === "none" && c.style.display === "none" && $("saveBar").style.display === "flex");
    $("reset").click(); await wait(100);
    check("Reset brings the checkbox and the thresholds back", n.checked === true && w.style.display !== "none" && c.style.display !== "none"); }
  check("the icon is closer to the header text (gap 11px instead of 14px)", /\.hd\{[^}]*gap:11px/.test([...d.querySelectorAll("style")].map((e) => e.textContent).join("")));
  check("total speed formatting like Transmission: 0 -> '0 kB/s', 719000 -> '719 kB/s', 3190000 -> '3.19 MB/s', 45600 -> '45.6 kB/s'", dom.window.fmtRate(0) === "0 kB/s" && dom.window.fmtRate(719000) === "719 kB/s" && dom.window.fmtRate(3190000) === "3.19 MB/s" && dom.window.fmtRate(45600) === "45.6 kB/s", [0, 719000, 3190000, 45600].map((x) => dom.window.fmtRate(x)));
  { const txt = $("status").textContent;
    check("the Transmission card has the speed row right under the torrents line, written like in the torrent list ('▼ ... ▲ ...')", /Скорость\s*▼ \d[\d.]* [kMG]B\/s ▲ \d[\d.]* [kMG]B\/s/.test(txt) && txt.indexOf("Торренты") < txt.indexOf("Скорость") && txt.indexOf("Скорость") < txt.indexOf("Последний опрос"), txt.slice(txt.indexOf("Торренты"), txt.indexOf("Торренты") + 120)); }
  { dom.window.dmRender({ state: "not_responding", pid: 5, since_age: 3, desired_running: true, busy: false, mem_kib: 1000, last_error: "", unresponsive_for: 45, restart_in: 75, stuck: false });
    const st1 = $("dmState").textContent, n1 = $("dmNote").textContent;
    dom.window.dmRender({ state: "not_responding", pid: 5, since_age: 3, desired_running: true, busy: false, mem_kib: 1000, last_error: "", unresponsive_for: 200, restart_in: 0, stuck: true });
    const n2 = $("dmNote").textContent;
    dom.window.dmRender({ state: "not_responding", pid: 5, since_age: 3, desired_running: true, busy: false, mem_kib: 1000, last_error: "", unresponsive_for: 12, restart_in: -1, stuck: false });
    const n3 = $("dmNote").textContent;
    check("a busy daemon: 'занят, не отвечает 45 с' and 'Если ответа не будет, принудительный перезапуск через 75 с'", /занят, не отвечает 45 с/.test(st1) && /Transmission занят/.test(n1) && /принудительный перезапуск через 75 с/.test(n1), [st1, n1]);
    check("a process that cannot be killed: the note says it is probably waiting for the disk and advises to unplug the USB disk (no countdown)", /ждёт диск/.test(n2) && /отключите USB-диск/.test(n2) && !/через 0 с/.test(n2), n2);
    check("the forced restart is disabled (0): no countdown is promised", /Transmission занят/.test(n3) && !/принудительный перезапуск/.test(n3), n3); }
  dom.window.refresh(); await wait(1500);   // restore the real state after the stubs
  { const css = [...d.querySelectorAll("style")].map((e) => e.textContent).join("");
    check("Diagnostics buttons are laid out in wrapping rows with gaps (two .btnrow blocks, flex-wrap and gap), no more overlapping", d.querySelectorAll(".btnrow").length === 2 && !!$("dgAll").closest(".btnrow") && !!$("dgTlog").closest(".btnrow") && /\.btnrow\{display:flex;flex-wrap:wrap;gap:10px/.test(css), d.querySelectorAll(".btnrow").length); }
  { const w = dom.window, body = $("accSettingsBody"), btn = $("accSettingsBtn"), snap = () => ({ disp: body.style.display, tr: body.style.transition, ov: body.style.overflow, h: body.style.height });
    w.PS5_ANIM_MS = 120; w.animReady = true;
    const was = body.style.display; btn.click(); const m1 = snap(); await wait(260); const a1 = snap();
    check("an accordion section OPENS with an animation: it is shown at once with a height/opacity transition, and after the animation the temporary styles are gone", was === "none" && m1.disp === "block" && /height/.test(m1.tr) && m1.ov === "hidden" && a1.disp === "block" && a1.tr === "" && a1.h === "" && w.isShown(body) === true, [was, m1, a1]);
    btn.click(); const m2 = snap(); await wait(260); const a2 = snap();
    check("... and CLOSES with an animation: still shown at the start, hidden when it ends, no temporary styles left", m2.disp === "block" && /height/.test(m2.tr) && a2.disp === "none" && a2.tr === "" && a2.h === "" && w.isShown(body) === false, [m2, a2]);
    btn.click(); await wait(20); btn.click(); await wait(300);
    check("two fast clicks (open and close again during the animation) end consistently: closed, nothing left over, the state is remembered right", body.style.display === "none" && body.style.transition === "" && w.isShown(body) === false && btn.getAttribute("aria-expanded") === "false", snap());
    btn.click(); await wait(300);
    const dep = $("f_notify_low_disk_space"), row = $("f_row_low_disk_warn_gb"); const rowWas = row.style.display !== "none";
    dep.checked = false; dep.dispatchEvent(new w.Event("change", { bubbles: true })); const r1 = { disp: row.style.display, tr: row.style.transition }; await wait(260);
    check("a row that depends on a checkbox is hidden with an animation too (and shown again the same way)", rowWas && r1.disp !== "none" && /height/.test(r1.tr) && row.style.display === "none", [rowWas, r1, row.style.display]);
    dep.checked = true; dep.dispatchEvent(new w.Event("change", { bubbles: true })); await wait(260);
    check("... the row is back after the checkbox is set again", row.style.display !== "none" && row.style.transition === "", row.style.display);
    btn.click(); await wait(300); w.PS5_ANIM_MS = 0; }
  { const w = $("f_low_disk_warn_gb"), c = $("f_low_disk_critical_gb");
    check("thresholds: defaults 'Мало места' 5 and 'Диск почти заполнен' 1, and the second one's maximum is always one less than the first", w.value === "5" && c.value === "1" && c.max === "4" && w.min === "1" && c.min === "0");
    c.value = "9"; ev(c, "input"); await wait(100);
    check("'Диск почти заполнен' cannot be entered >= 'Мало места': it is clamped AT INPUT (9 -> 4), no error message needed", c.value === "4" && c.max === "4");
    w.value = "3"; ev(w, "input"); ev(w, "change"); await wait(100);
    check("lowering 'Мало места' lowers the other one too (3 -> max 2, value 2)", c.value === "2" && c.max === "2");
    w.value = "1"; ev(w, "change"); await wait(100);
    check("with 'Мало места' = 1 the other is 0 (0 = do not notify)", c.value === "0" && c.max === "0");
    $("reset").click(); await wait(200);
    check("Reset brings both back (5 and 1)", w.value === "5" && c.value === "1" && c.max === "4"); }
  check("log retention fields are among the expert settings", !!$("f_notifications_keep_days") && !!$("f_logs_keep_days") && $("expertForm").contains($("f_logs_keep_days")));
  check("header: manager version and Transmission version, no link in it", /Версия [\d.]+ · Transmission 4\.1\.4 · адрес консоли/.test($("top").textContent) && !$("top").querySelector("a"), $("top").textContent);
  check("the web-UI link is on the right of the Start/Stop row and visible while Transmission runs", $("dmBtn").parentNode.contains($("trLink")) && $("trLink").style.marginLeft === "auto" && $("trLink").style.display !== "none");
  check("the web-UI link opens in a NAMED tab in an ordinary browser (the next click goes to the same tab) and has no noopener (it would force a new tab every time)", $("trLink").target === "ps5tm-transmission-web" && !/noopener/.test($("trLink").rel));
  { const w = dom.window; Object.defineProperty(w.navigator, "userAgent", { value: "Mozilla/5.0 (PlayStation; PlayStation 5/11.20) AppleWebKit/605.1.15", configurable: true }); w.render(w.state);
    check("on the console browser (PlayStation in the User-Agent) the web-UI link opens in the same window (no target)", !$("trLink").hasAttribute("target"));
    delete w.navigator.userAgent; w.render(w.state);
    check("after leaving the console UA the link is the named tab again", $("trLink").target === "ps5tm-transmission-web"); }
  const line = [...d.querySelectorAll("#status .kv")].find((k) => /Торренты/.test(k.textContent));
  check("the torrents line has no zero items and NO total ('всего' is gone)", !!line && /качаются 1, на паузе 1$/.test(line.textContent.replace("Торренты", "").trim()), line && line.textContent);
  check("the Save/Reset bar is hidden while nothing is changed; no 'Нечего сохранять' anywhere", $("saveBar").style.display === "none" && !/Нечего сохранять/.test(d.body.textContent));
  check("layout: Transmission, then the Sleep section (always visible), then the 'Настройки' and 'Расширенные настройки' dropdowns, the red Diagnostics dropdown, the credits dropdown after them and the repository line at the very bottom", pos($("dmBtn"), $("asArm")) && pos($("asArm"), $("accSettingsBtn")) && pos($("accSettingsBtn"), $("form")) && pos($("form"), $("accExpertBtn")) && pos($("accExpertBtn"), $("expertForm")) && pos($("expertForm"), $("expertIcon")) && pos($("expertIcon"), $("accDiagBtn")) && pos($("accDiagBtn"), $("accCreditsBtn")) && pos($("accCreditsBody"), d.querySelector(".repo")));
  check("the sleep section is not inside any dropdown (it is used most often)", !$("accSettingsBody").contains($("asArm")) && !$("accExpertBody").contains($("asArm")) && !$("accSettingsBody").contains($("asBox")));
  check("the old 'Показывать расширенные настройки' checkbox is gone", !d.getElementById("expertToggle") && !/Показывать расширенные настройки/.test(d.body.textContent));
  const heads = [$("accSettingsBtn"), $("accExpertBtn")];
  check("both dropdowns are closed by default: a title with a chevron on the right, bodies hidden", heads.every((h) => h.getAttribute("aria-expanded") === "false" && !h.classList.contains("open") && !!h.querySelector("svg.chev")) && $("accSettingsBody").style.display === "none" && $("accExpertBody").style.display === "none" && /Настройки/.test(heads[0].textContent) && /Расширенные настройки/.test(heads[1].textContent));
  heads[0].click(); await wait(100);
  check("clicking 'Настройки' opens it: the chevron flips up (class 'open'), the notification settings and the web theme are inside", $("accSettingsBody").style.display === "block" && heads[0].classList.contains("open") && heads[0].getAttribute("aria-expanded") === "true" && $("accSettingsBody").contains($("f_notify_startup")) && $("accSettingsBody").contains($("f_web_theme")) && $("accExpertBody").style.display === "none");
  check("the dropdown's state is NOT remembered in the browser (the sections are collapsed at every page load)", dom.window.localStorage.getItem("acc_accSettings") === null && dom.window.localStorage.getItem("acc_accExpert") === null);
  $("accExpertBtn").click(); await wait(100);
  const ids = [...d.querySelectorAll("#expertForm input")].map((i) => i.id);
  check("'Расширенные настройки' opens the 9 expert fields (ports, delays, log retention, daemon incl. the restart after a network change), the Media-tab icon section (the diagnostics are a separate dropdown)", $("accExpertBody").style.display === "block" && heads[1].classList.contains("open") && ids.length === 9 && ids.includes("f_daemon_network_restart_seconds") && $("accExpertBody").contains($("expertIcon")) && !$("accExpertBody").contains($("accDiag")) && $("expertIcon").contains($("iconInstall")) && /Иконка на вкладке «Мультимедиа»/.test($("expertIcon").textContent), ids.join(","));
  heads[0].click(); await wait(100);
  check("a second click closes it again (the chevron flips back down)", $("accSettingsBody").style.display === "none" && !heads[0].classList.contains("open") && heads[0].getAttribute("aria-expanded") === "false");
  heads[0].click(); await wait(100);   // from here on we check the settings fields

  const n = $("f_notify_startup"); const was = n.checked; n.checked = !was; ev(n, "change"); await wait(100);
  check("a change makes the floating bar appear (Save enabled, Reset visible)", $("saveBar").style.display === "flex" && $("save").disabled === false && $("reset").style.display !== "none");
  check("the bar floats over the page next to the bell (fixed position)", dom.window.getComputedStyle($("saveBar")).position === "fixed" && dom.window.getComputedStyle($("bell")).position === "fixed");
  $("reset").click(); await wait(100);
  check("'Сбросить изменения' restores the value and hides the bar", n.checked === was && $("saveBar").style.display === "none");
  const poll = $("f_poll_interval_seconds"); poll.value = "9"; ev(poll, "input"); await wait(100);
  check("typing in a number field also shows the bar", $("saveBar").style.display === "flex" && $("save").disabled === false);
  $("save").click(); await wait(1500);
  check("saving shows 'Сохранено' in the bar, which then goes away by itself", /Сохранено/.test($("saveMsg").textContent) && $("save").disabled === true, $("saveMsg").textContent);
  await wait(4300);
  check("...and the bar disappears after a few seconds", $("saveBar").style.display === "none");
  poll.value = "0"; ev(poll, "input"); $("save").click(); await wait(1200);
  check("an invalid value: red message in the bar, the bar stays for correcting", $("saveMsg").className.indexOf("err") >= 0 && $("saveBar").style.display === "flex" && $("save").disabled === false, $("saveMsg").textContent);
  poll.value = "9"; ev(poll, "input"); await wait(100);

  check("the bell shows the unread count (notif_seen=0)", $("bellBadge").style.display === "block" && parseInt($("bellBadge").textContent, 10) > 0, $("bellBadge").textContent);
  $("bell").click(); await wait(1500);
  const items = [...d.querySelectorAll("#bellPanel .nf")];
  check("the list: newest first, with date and time", $("bellPanel").style.display === "block" && items.length > 0 && /\d\d\.\d\d \d\d:\d\d:\d\d/.test(items[0].textContent), items.length + " items");
  check("notifications switched off in the settings are there too, marked as not sent", items.some((i) => i.classList.contains("off") && /отключено в настройках/.test(i.textContent)));
  check("sleep events are there as 'событие' (not sent to the console, not 'disabled')", items.some((i) => i.classList.contains("ev") && /событие/.test(i.textContent) && /Sleep: auto-sleep activated/.test(i.textContent) && !/отключено/.test(i.textContent)), items.filter((i) => i.classList.contains("ev")).length + " events");
  check("the unread badge is cleared after opening", $("bellBadge").style.display === "none");
  d.body.click(); await wait(100);
  check("a click outside closes the list", $("bellPanel").style.display === "none");
  $("bell").click(); await wait(800); [...d.querySelectorAll("#bellPanel button")].find((b) => b.textContent === "Очистить").click(); await wait(1200);
  check("'Очистить' empties the list", /Пока уведомлений нет/.test($("bellPanel").textContent) && d.querySelectorAll("#bellPanel .nf").length === 0);
  { const real = dom.window.api;
    dom.window.api = (url, body, cb) => (url === "/api/notifications" && !body ? cb(200, { latest: 9999, items: [
      { id: 9997, time: 1760000000, kind: 3, shown: false, text: "Transmission is already running: restarting it (pid 113)" },
      { id: 9998, time: 1760000100, kind: 4, shown: false, text: "Transmission: cannot stop the old instance (pid 5)." },
      { id: 9999, time: 1760000200, kind: 2, shown: false, text: "Download folder added: /mnt/usb0/Torrents" }] }) : real(url, body, cb));
    dom.window.nfSig = ""; dom.window.notifLoad(); await wait(700);
    const rows = [...d.querySelectorAll("#bellPanel .nf")], sys = rows.filter((r) => r.classList.contains("sys")), ev = rows.find((r) => r.classList.contains("ev"));
    check("the wrapper's own messages ('Transmission is already running: restarting it ...') get the red 'системное' next to the time, the way events get the blue 'событие'; an event is not 'системное'",
      sys.length === 2 && sys.every((r) => /системное/.test(r.querySelector("small").textContent) && !!r.querySelector("small b.sysb")) && !!ev && !/системное/.test(ev.textContent) && !ev.classList.contains("sys"), sys.length + " system");
    check("a system message that is switched off in the settings has both labels ('системное' and 'отключено в настройках') and the dimmed look; the other one has only 'системное'",
      sys.filter((r) => r.classList.contains("off") && /отключено в настройках/.test(r.textContent)).length === 1 && sys.filter((r) => !r.classList.contains("off") && !/отключено/.test(r.textContent)).length === 1);
    dom.window.api = real; }
  console.log(`\n${pass}/${pass + fail} settings page checks passed`); dom.window.close(); process.exit(fail ? 1 : 0);
})().catch((e) => { console.log("crashed", e); process.exit(2); });
