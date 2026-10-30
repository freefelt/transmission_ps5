import os, sys; sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "lib"))
import sys
from harness import *
import harness; harness.AUTO_FOLDER = False   # checks the defaults in config.cfg: needs a config created by the manager
import urllib.request, urllib.error
CFG = "/data/Transmission/config.cfg"; WEBCFG = "/data/Transmission/transmission/web/ps5-config.js"
def get(url):
    try: r = urllib.request.urlopen(url, timeout=10); return r.status, r.read().decode()
    except urllib.error.HTTPError as e: return e.code, ""
def served(): return get("http://127.0.0.1:9091/transmission/web/ps5-config.js")
def sstate(): return json.loads(api("GET", "/api/state")[1])
shutil.rmtree("/user/app", ignore_errors=True); os.makedirs("/user/app")
fresh(); loader = Loader(); m = start_manager(); wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 70); time.sleep(4)
print("== 1) the disk panel setting: the web UI gets it through ps5-config.js")
check("on by default: 'all' (state, config.cfg, and the file the web page loads)", sstate()["config"]["web_diskbar"] == "all" and "web_diskbar=all" in open(CFG).read() and served() == (200, "window.PS5_CONFIG={diskbar:true,diskbarMode:\"all\",openFolder:\"off\"};\n"), served())
c, t = api("POST", "/api/config", {"web_diskbar": "active"}); check("'active' (only disks of running torrents) takes effect at once, without restarting the daemon; the panel stays on", c == 200 and served() == (200, "window.PS5_CONFIG={diskbar:true,diskbarMode:\"active\",openFolder:\"off\"};\n") and "web_diskbar=active" in open(CFG).read() and open("/data/Transmission/transmission/web_diskbar").read().strip() == "active", (c, served()))
c, t = api("POST", "/api/config", {"web_diskbar": "off"}); check("'off' hides the panel: the page gets diskbar:false", c == 200 and served() == (200, "window.PS5_CONFIG={diskbar:false,diskbarMode:\"off\",openFolder:\"off\"};\n") and "web_diskbar=off" in open(CFG).read() and open("/data/Transmission/transmission/web_diskbar").read().strip() == "off", (c, served()))
c, t = api("POST", "/api/config", {"web_diskbar": "bogus"}); check("a wrong value is refused and nothing changes", c != 200 and served() == (200, "window.PS5_CONFIG={diskbar:false,diskbarMode:\"off\",openFolder:\"off\"};\n") and "web_diskbar=off" in open(CFG).read(), (c, served()))
api("POST", "/api/daemon", {"action": "stop"}); wait(lambda: dstate()["state"] == "stopped", 45); os.remove(WEBCFG)
api("POST", "/api/daemon", {"action": "start"}); wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 80); time.sleep(2)
check("after a daemon restart (file removed) the daemon itself writes it again: still off", served() == (200, "window.PS5_CONFIG={diskbar:false,diskbarMode:\"off\",openFolder:\"off\"};\n"), served())
api("POST", "/api/config", {"web_diskbar": "all"}); check("and back on", served()[1] == "window.PS5_CONFIG={diskbar:true,diskbarMode:\"all\",openFolder:\"off\"};\n")
c1, _ = api("POST", "/api/config", {"web_diskbar": False}); a = sstate()["config"]["web_diskbar"]; c2, _ = api("POST", "/api/config", {"web_diskbar": True}); b = sstate()["config"]["web_diskbar"]; check("the old on/off values (a checkbox in earlier versions, 'no' / 'yes' in old config.cfg files) are still understood: off / all", (c1, a, c2, b) == (200, "off", 200, "all"), (c1, a, c2, b))
print("== 2) hidden Sleep warnings: the version is stored in the settings")
check("by default nothing is hidden (empty), the line is in config.cfg", sstate()["config"]["sleep_warnings_hidden"] == "" and "sleep_warnings_hidden=" in open(CFG).read())
ver = sstate()["version"]
c, t = api("POST", "/api/config", {"sleep_warnings_hidden": ver}); cfg = open(CFG).read()
check("the current version is accepted and saved", c == 200 and sstate()["config"]["sleep_warnings_hidden"] == ver and f"sleep_warnings_hidden={ver}" in cfg, (c, ver))
check("values with strange characters or too long are refused, nothing changes", all(api("POST", "/api/config", {"sleep_warnings_hidden": v})[0] == 400 for v in ("a b", "<script>", "x" * 40, "0.0;rm")) and sstate()["config"]["sleep_warnings_hidden"] == ver)
for p in mgr_pids(): os.kill(p, signal.SIGKILL)
time.sleep(1); m = start_manager("mgr2"); wait(lambda: dstate() is not None, 30)
check("it survives a manager restart (and a new version would simply differ from it)", sstate()["config"]["sleep_warnings_hidden"] == ver)
print("== 3) all-or-nothing for string settings (a bug fixed in this version: the rollback used quoted JSON strings)")
c, t = api("POST", "/api/config", {"web_theme": "light", "sleep_warnings_hidden": "0.0.99", "low_disk_warn_gb": 0})
st = sstate()["config"]
check("a request with one invalid value changes NOTHING: the theme and the hidden version are rolled back", c == 400 and st["web_theme"] == "dark" and st["sleep_warnings_hidden"] == ver, (c, st["web_theme"], st["sleep_warnings_hidden"]))
check("...and the theme file the page loads is still the dark one", get("http://127.0.0.1:9091/transmission/web/ps5-theme.css")[1].startswith("/* ps5: dark theme */"))
print("== the 'Open folder' item of the torrent menu: setting off / bfpilot / webfm")
check("default: off (state, config.cfg, ps5-config.js)", sstate()["config"]["web_openfolder"] == "off" and "web_openfolder=off" in open(CFG).read() and 'openFolder:"off"' in served()[1], served())
c, t_ = api("POST", "/api/config", {"web_openfolder": "bfpilot"}); check("'bfpilot' takes effect at once: the page config says openFolder bfpilot, config.cfg and the daemon file too", c == 200 and 'openFolder:"bfpilot"' in served()[1] and "web_openfolder=bfpilot" in open(CFG).read() and open("/data/Transmission/transmission/web_openfolder").read().strip() == "bfpilot", (c, served()))
c, t_ = api("POST", "/api/config", {"web_openfolder": "webfm"}); check("'webfm' too", c == 200 and 'openFolder:"webfm"' in served()[1] and "web_openfolder=webfm" in open(CFG).read(), (c, served()))
c, t_ = api("POST", "/api/config", {"web_openfolder": "nautilus"}); check("a wrong value is refused and nothing changes", c != 200 and 'openFolder:"webfm"' in served()[1], (c, served()))
api("POST", "/api/daemon", {"action": "stop"}); wait(lambda: dstate()["state"] == "stopped", 45); os.remove(WEBCFG)
api("POST", "/api/daemon", {"action": "start"}); wait(lambda: daemon_pids() and rpc_ok() and dstate()["state"] == "running", 80); time.sleep(2)
check("after a daemon restart (file removed) the daemon writes the same text itself, openFolder included", 'openFolder:"webfm"' in served()[1], served())
api("POST", "/api/config", {"web_openfolder": "off"})
kill_all(); loader.close()
sys.exit(summary())
