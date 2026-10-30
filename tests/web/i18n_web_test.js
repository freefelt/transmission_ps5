const { JSDOM } = require("jsdom"); const fs = require("fs");
// Web interface translation against REAL files: the ps5-lang.js dictionary, the ps5-i18n.js translator and index.html from the daemon's web folder.
// node i18n_web_test.js ru|en|xx <ps5-lang.js file> <web folder>
const [lang, langFile, webDir] = process.argv.slice(2);
let pass = 0, fail = 0; const check = (n, ok, e) => { (ok ? pass++ : fail++); console.log((ok ? "PASS " : "FAIL ") + n + (e !== undefined ? "  -> " + String(e).slice(0, 220) : "")); };
(async () => {
  const html = fs.readFileSync(`${webDir}/index.html`, "utf8").replace(/<script[^>]*src=[^>]*><\/script>/g, "");
  const dom = new JSDOM(html, { runScripts: "outside-only" });
  dom.window.eval(fs.readFileSync(langFile, "utf8")); dom.window.eval(fs.readFileSync(`${webDir}/ps5-i18n.js`, "utf8"));
  await new Promise((r) => setTimeout(r, 80));
  const w = dom.window, d = w.document, I = w.PS5_I18N;
  const plural = (key, ns) => ns.map((n) => w.ps5n(key, [n], n, n + (n === 1 ? " peer" : " peers"))).join(", ");
  check("the dictionary was loaded (language " + lang + ")", I && I.lang === (lang === "xx" ? "file:xx.strings" : lang), I && I.lang);
  check("a text that is not in the dictionary stays English (that is the default: the key itself)", w.ps5t("Some text that is not in the dictionary") === "Some text that is not in the dictionary" && w.ps5t("{1} of {2}", ["3", "12"]) !== undefined);
  if (lang === "ru") {
    check("RU: ps5t takes the translation by the English key and puts the values in", w.ps5t("Open torrent…") === "Открыть торрент…" && w.ps5t("{1} ago", ["5 minutes"]) === "5 minutes назад" && w.ps5t("Created by {1} on {2}", ["me", "Mon"]) === "Создал me, Mon", [w.ps5t("Open torrent…"), w.ps5t("{1} ago", ["x"])]);
    check("RU: leading and trailing spaces of the key are kept (a piece of a longer text)", w.ps5t(" · not recommended") === " · не рекомендуется" && w.ps5t("Destination folder: ") === "Папка назначения: ", [w.ps5t(" · not recommended"), w.ps5t("Destination folder: ")]);
    check("RU: words with a number take the Russian plural form (1 пир, 2 пира, 5 пиров, 21 пир)", plural("{1} peers", [1, 2, 5, 21]) === "1 пир, 2 пира, 5 пиров, 21 пир", plural("{1} peers", [1, 2, 5, 21]));
    check("RU: the marked texts of index.html are translated at load (title, button, status bar, placeholder, aria-label)", d.title === "Веб-интерфейс Transmission" && d.getElementById("toolbar-inspector").textContent.trim() === "Инспектор" && d.getElementById("torrent-search").placeholder === "Фильтр" && !!d.querySelector('[aria-label="Открыть торрент"]'), [d.title, d.getElementById("toolbar-inspector").textContent.trim()]);
    check("RU: our own texts (written in English in the code) come out Russian", w.ps5t("Fix for USB drives") === "Фикс для USB дисков" && w.ps5t("Open folder") === "Открыть папку" && w.ps5t("{1} · {2} free of {3}", ["USB-диск", "1 ГБ", "2 ГБ"]) === "USB-диск · свободно 1 ГБ из 2 ГБ", w.ps5t("Fix for USB drives"));
  } else if (lang === "en") {
    check("EN: nothing changes: the English key is the text; the plural fallback is the English one", w.ps5t("Open torrent…") === "Open torrent…" && w.ps5t("{1} ago", ["5 minutes"]) === "5 minutes ago" && plural("{1} peers", [1, 2, 5]) === "1 peer, 2 peers, 5 peers", plural("{1} peers", [1, 2, 5]));
    check("EN: index.html keeps its English texts", d.title === "Transmission Web Interface" && d.getElementById("toolbar-inspector").textContent.trim() === "Inspector" && d.getElementById("torrent-search").placeholder === "Filter");
    check("EN: our own texts are English", w.ps5t("Fix for USB drives") === "Fix for USB drives" && w.ps5t("{1} · {2} free of {3}", ["USB drive", "1 GB", "2 GB"]) === "USB drive · 1 GB free of 2 GB");
  } else {
    check("FILE: the word and the template from the file are used, the value is put in", w.ps5t("Open") === "Abrir" && w.ps5t("{1} ago", ["5 minutes"]) === "hace 5 minutes", [w.ps5t("Open"), w.ps5t("{1} ago", ["x"])]);
    check("FILE: keys that the file does not have fall back to the English key (also plurals and index.html)", w.ps5t("Close") === "Close" && plural("{1} peers", [1, 2]) === "1 peer, 2 peers" && d.title === "Transmission Web Interface", [w.ps5t("Close"), plural("{1} peers", [1, 2])]);
  }
  console.log(`\n${pass}/${pass + fail} web translation checks passed`); process.exit(fail ? 1 : 0);
})();
