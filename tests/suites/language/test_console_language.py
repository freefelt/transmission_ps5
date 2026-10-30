import os, sys; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import sys, json, time, os, re, base64, hashlib, urllib.request
from harness import *
import harness
harness.SYS_HELPER = True      # the manager starts the helper itself: the console language and "keep awake" go through it
harness.TEST_MSG = None        # messages in the selected language, as in a real installation
harness.TEST_LANG = None       # the default language is taken from the console language (helper)
CFG = "/data/Transmission/config.cfg"; LD = "/data/Transmission/locales"
def cfg(): return open(CFG, encoding="utf-8").read()
def hj():
    try: return json.loads(api("GET", "/api/state")[1])["stats"]["syssvc"]
    except Exception: return {}
def sstate(): return json.loads(api("GET", "/api/state")[1])
def bell(): c, t = api("GET", "/api/notifications"); return [i["text"] for i in json.loads(t)["items"]] if c == 200 else []
def page(): return urllib.request.urlopen("http://127.0.0.1:9092/", timeout=10).read().decode("utf-8")
def meta(name): return base64.b64encode(bencode({"announce": "http://127.0.0.1:1/announce", "info": {"name": name, "length": 65536, "piece length": 65536, "pieces": hashlib.sha1(name.encode()).digest()}})).decode()
def setcfg(**kw): return api("POST", "/api/config", kw)
def start(lang_code):
    for f in ("/tmp/ps5tm_sys_calls.log",): 
        if os.path.exists(f): os.remove(f)
    open("/tmp/ps5tm_sys_lang", "w").write(f"{lang_code}\n")
    fresh(); ldr = Loader(); mm = start_manager()
    wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 70)
    return mm, ldr, wait(lambda: hj().get("ready") is True, 40)
import shutil; shutil.rmtree(LD, ignore_errors=True)
ldr = None
try:
    m, ldr, up = start(8)
    check("the system service reports the console language code 8 (Russian)", up and hj().get("system_lang") == 8, hj().get("system_lang"))
    p = page()
    check("FIRST START on a Russian console: the settings page is Russian (lang=ru) and the default is stored in config.cfg", 'lang="ru"' in p and "language=ru" in cfg(), cfg()[-150:])
    rpc("torrent-add", metainfo=meta("ka_one"), paused=False)
    check("setup: a download is active, so the auto action can be activated", wait(lambda: sstate()["autosleep"]["can_arm"] is True, 25), sstate()["autosleep"])
    c, t = api("POST", "/api/autosleep", {"action": "arm", "timeout_minutes": 60})
    check("messages follow the language: the bell entry about the activation is Russian", c == 200 and wait(lambda: any("Сон: автосон активирован" in x for x in bell()), 8), [x[:60] for x in bell()][-2:])
    time.sleep(8); h0 = hj()
    check("keep awake is OFF by default: activated auto sleep does not touch the console's own timer (no calls to the system service)", h0["keepawake"] == 0 and h0["ka_calls"] == 0, (h0["keepawake"], h0["ka_calls"]))
    api("POST", "/api/autosleep", {"action": "disarm"})
    check("deactivation is reported in Russian too", wait(lambda: any("Сон: автосон деактивирован" in x for x in bell()), 8), [x[:60] for x in bell()][-2:])
    c, t = setcfg(keep_awake=True)
    time.sleep(7)
    check("keep awake ON but auto sleep NOT activated: the system is left alone (no calls)", c == 200 and "keep_awake=yes" in cfg() and hj()["keepawake"] == 0 and hj()["ka_calls"] == 0, (c, hj()["keepawake"], hj()["ka_calls"]))
    api("POST", "/api/autosleep", {"action": "arm", "timeout_minutes": 60})
    ok = wait(lambda: hj().get("keepawake") == 1 and hj().get("ka_calls", 0) >= 1, 14)
    check("keep awake ON and auto sleep activated: within seconds the system service keeps the console awake (PowerTick right away)", ok, (hj().get("keepawake"), hj().get("ka_calls")))
    api("POST", "/api/autosleep", {"action": "disarm"})
    check("after the deactivation the request is withdrawn", wait(lambda: hj().get("keepawake") == 0, 14), hj().get("keepawake"))
    # --- English language
    setcfg(language="en"); api("POST", "/api/autosleep", {"action": "arm", "timeout_minutes": 60})
    check("switched to English: the new bell entry is English (the earlier Russian ones stay as they were)", wait(lambda: any("Sleep: auto-sleep activated" in x for x in bell()), 8) and any("Сон: автосон активирован" in x for x in bell()), [x[:50] for x in bell()][-3:])
    api("POST", "/api/autosleep", {"action": "disarm"})
    c, t = setcfg(web_theme="blue")
    check("a settings error is English now", c == 400 and "web_theme: dark or light is expected, not 'blue'" in t, t[:140])
    setcfg(language="ru"); c, t = setcfg(web_theme="blue")
    check("and Russian after switching back", c == 400 and "web_theme: нужно dark или light, а не 'blue'" in t, t[:140])
    # --- language file: substitutions and the English fallback
    os.makedirs(LD, exist_ok=True)
    open(f"{LD}/xx.strings", "w", encoding="utf-8").write('"LanguageName" = "XX"\n"me_j_disarmed" = "XX disarmed {9}"\n')
    c, t = setcfg(language="file:xx.strings")
    check("a message translation with a placeholder that the English text does not have is refused (ph:me_j_disarmed)", c == 400 and "LANGERR:ph:me_j_disarmed" in t, t[:120])
    open(f"{LD}/xx.strings", "w", encoding="utf-8").write('"LanguageName" = "XX"\n"me_j_disarmed" = "XX disarmed"\n"me_j_limit" = "XX limit {1} min"\n')
    c, t = setcfg(language="file:xx.strings")
    check("the corrected file is accepted", c == 200 and "language=file:xx.strings" in cfg(), (c, t[:100]))
    api("POST", "/api/autosleep", {"action": "arm", "timeout_minutes": 60}); time.sleep(1.5)
    api("POST", "/api/autosleep", {"action": "disarm"})
    b = bell()
    check("the file's own text is used where it has one (XX disarmed) and English where it has not (the activation message)", any(x == "XX disarmed" for x in b) and sum("Sleep: auto-sleep activated" in x for x in b) >= 2, [x[:50] for x in b][-4:])
    check("a config error under the file language falls back to English too (no key in the file)", "web_theme: dark or light is expected" in setcfg(web_theme="blue")[1], None)
    kill_all(); ldr.close()
    # --- English console: the default language is English
    shutil.rmtree(LD, ignore_errors=True)
    m, ldr, up = start(1)
    check("the system service reports code 1 (English console)", up and hj().get("system_lang") == 1, hj().get("system_lang"))
    p = page()
    check("FIRST START on a console that is not Russian: the page is English and language=en is stored", 'lang="en"' in p and "language=en" in cfg(), cfg()[-150:])
finally:
    kill_all()
    try: ldr.close()
    except Exception: pass
    shutil.rmtree(LD, ignore_errors=True)
    for f in ("/tmp/ps5tm_sys_lang",):
        if os.path.exists(f): os.remove(f)
sys.exit(summary())
