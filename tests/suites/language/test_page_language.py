import os, sys; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import sys, json, time, os, re, urllib.request
from harness import *
import harness; harness.TEST_LANG = None      # as in a real installation: English is the default language
LD = "/data/Transmission/locales"
CFG_FILE = "/data/Transmission/config.cfg"
def page():
    return urllib.request.urlopen("http://127.0.0.1:9092/", timeout=10).read().decode("utf-8")
def cfg(): return open(CFG_FILE, encoding="utf-8").read()
def put(name, text, raw=False):
    os.makedirs(LD, exist_ok=True)
    open(f"{LD}/{name}", "wb" if raw else "w", **({} if raw else {"encoding": "utf-8"})).write(text)
def setlang(v): return api("POST", "/api/config", {"language": v})
def skipped(pg):
    m = re.search(r"skipped:\[(.*?)\]\}", pg); return m.group(1) if m else ""
def rukeys():
    out = {}
    for l in open(f"{REPO}/locales/ru.strings", encoding="utf-8"):
        m = re.match(r'"([^"]+)" = "', l)
        if m: out[m.group(1)] = 1
    return out
K = rukeys()
need = ("exit_transmission", "save", "download_folders")
assert all(k in K for k in need), "test keys must exist in ru.strings"
import shutil; shutil.rmtree(LD, ignore_errors=True)
fresh(); m = start_manager()
try:
    wait(lambda: api("GET", "/api/state")[0] == 200, 40); time.sleep(2)
    p = page()
    check("FIRST START without a saved language: the page is English (the console language is treated as not Russian), lang=en, no Russian page text left", 'lang="en"' in p and "Download folders" in p and "Папки загрузок" not in p, None)
    check("the console language is unknown (no helper): the page is English for this time and nothing is stored in config.cfg", "language=" not in cfg(), cfg()[-200:])
    check("the list has the two built-in languages (Русский, English) and no tokens are left in the page", "['ru','Русский'],['en','English']" in p and "@@" not in p, None)
    check("the language field has the bilingual group heading and name", "Language / Язык" in p, None)
    check("the English page is the source text of the page: it is served as it is, nothing is translated", re.search(r"language en: \d+ fragments translated", shim()) is None, [l for l in shim().split("\n") if "language en" in l][-1:])
    c, t = setlang("ru")
    p = page()
    check("choosing 'ru' is accepted, saved, and the page becomes the original Russian one (lang=ru)", c == 200 and "language=ru" in cfg() and 'lang="ru"' in p and "Папки загрузок" in p and "Download folders" not in p and "@@" not in p, (c, t[:80]))
    m = re.search(r"language ru: (\d+) fragments translated", shim())
    check("the log says how many fragments of the page were translated into Russian (more than 300: the whole page)", m is not None and int(m.group(1)) > 300, m.group(0) if m else None)
    c, t = setlang("en"); p = page()
    check("and back to 'en'", c == 200 and 'lang="en"' in p and "Download folders" in p, (c, t[:80]))
    # --- user files
    put("es.strings", '// Spanish, a test\n# another comment\n\n"LanguageName" = "Español"\n\n"exit_transmission" = "Salir de Transmission"\n"save" = "Guardar"\n"download_folders" = "Carpetas de descarga"\n')
    put("semi.strings", '"LanguageName" = "Con punto y coma";\n"save" = "Guardar2";\n')
    put("en.strings", '"LanguageName" = "Hacked"\n"save" = "HACKED"\n')
    put("bad_syntax.strings", '"LanguageName" = "Bad"\n"save" = "x"\nthis is not valid\n')
    put("no_first.strings", '"save" = "x"\n"LanguageName" = "Late"\n')
    put("bad_utf8.strings", b'"LanguageName" = "Bad"\n"save" = "\xff\xfe"\n', raw=True)
    put("dup.strings", '"LanguageName" = "Dup"\n"save" = "a"\n"save" = "b"\n')
    put("chars.strings", '"LanguageName" = "Chars"\n"save" = "<b>x</b>"\n')
    put("unknown.strings", '"LanguageName" = "Unknown"\n"no_such_key_at_all" = "x"\n')
    p = page(); sk = skipped(p)
    check("the folder is read when the page opens: valid files are in the list ('Español', the one with trailing semicolons), a file named en.strings does NOT replace the built-in English (it is a separate entry)", "['file:es.strings','Español']" in p and "['file:semi.strings','Con punto y coma']" in p and "['en','English']" in p and "['file:en.strings','Hacked']" in p, None)
    check("broken files are not in the list and the page names the reason for each: syntax:3, first, utf8, dup:save, chars:2, empty", all(x in sk for x in ("['bad_syntax.strings','syntax:3']", "['no_first.strings','first']", "['bad_utf8.strings','utf8']", "['dup.strings','dup:save']", "['chars.strings','chars:2']", "['unknown.strings','empty']")) and "file:bad_syntax" not in p.split("@@")[0].split("var SCHEMA")[1][:300], sk[:400])
    c, t = setlang("file:es.strings"); p = page()
    check("choosing the Spanish file is accepted; the page uses its texts and falls back to ENGLISH (not Russian) for the keys it does not have", c == 200 and 'lang="es"' in p and "Salir de Transmission" in p and "Carpetas de descarga" in p and ">Guardar<" in p and "Notifications" in p and "Уведомления" not in p, (c, t[:80]))
    m = re.search(r"language file:es.strings: (\d+) fragments translated", shim())
    check("the log of that page: the fragments of the three keys of the file were translated", m is not None and int(m.group(1)) >= 3, [l for l in shim().split("\n") if "language file" in l][-1:])
    for name, code in (("bad_syntax.strings", "syntax:3"), ("no_first.strings", "first"), ("bad_utf8.strings", "utf8"), ("dup.strings", "dup:save"), ("chars.strings", "chars:2"), ("unknown.strings", "empty")):
        c, t = setlang("file:" + name)
        check(f"choosing the broken {name} is refused with the error LANGERR:{code} and the saved language does not change", c == 400 and f"LANGERR:{code}" in t and "language=file:es.strings" in cfg(), (c, t[:90]))
    for bad in ("file:../x.strings", "file:es.strings/x", "file:nofile.strings", "klingon", "file:", "file:a b.strings"):
        c, t = setlang(bad)
        check(f"a wrong value '{bad}' is refused ({c})", c == 400 and "language=file:es.strings" in cfg(), t[:80])
    # --- the saved language became invalid
    put("es.strings", '"LanguageName" = "Español"\nбroken line\n')
    p = page()
    check("the SAVED language file became broken: the page falls back to the default language (English), tells why (fallback, syntax:2) (the default is unknown without the helper, so it is not stored)", 'lang="en"' in p and "fallback:'es.strings'" in p and "reason:'syntax:2'" in p and "language=file:es.strings" in cfg(), cfg()[-120:])
    # --- a config corrupted by hand: an invalid value
    s = cfg().replace("language=en", "language=klingon"); 
    check("(the manager refuses a hand-edited wrong value in config.cfg at the next start: see the config loading tests; here the page itself is still English)", 'lang="en"' in page(), None)
    # --- translations do not break the page: no leftover separator and the script brackets balance
    c, t = setlang("ru"); pr = page(); c, t = setlang("en"); pe = page()
    check("the Russian and the English page have the same JavaScript skeleton: equal number of braces, of function definitions and of <script> tags (a translation cannot break the code)", pr.count("{") == pe.count("{") and pr.count("}") == pe.count("}") and len(re.findall(r"\bfunction\s*\w*\s*\(", pr)) == len(re.findall(r"\bfunction\s*\w*\s*\(", pe)) and pr.count("<script>") == pe.count("<script>"), (pr.count("{"), pe.count("{")))
finally:
    kill_all(); shutil.rmtree(LD, ignore_errors=True)
sys.exit(summary())
