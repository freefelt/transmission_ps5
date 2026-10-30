// Path of the Chrome/Chromium executable the browser tests use.
// Order: the CHROME_BIN environment variable, then the usual places (Playwright cache, /opt/pw-browsers, system packages).
const fs = require("fs"), os = require("os"), path = require("path");
function find() {
  if (process.env.CHROME_BIN) return process.env.CHROME_BIN;
  const roots = [path.join(os.homedir(), ".cache/ms-playwright"), "/opt/pw-browsers", process.env.PLAYWRIGHT_BROWSERS_PATH].filter(Boolean);
  for (const r of roots) {
    let dirs = [];
    try { dirs = fs.readdirSync(r).filter((d) => d.startsWith("chromium-")).sort().reverse(); } catch (e) { continue; }
    for (const d of dirs) for (const sub of ["chrome-linux/chrome", "chrome-linux64/chrome"]) { const p = path.join(r, d, sub); if (fs.existsSync(p)) return p; }
  }
  for (const p of ["/usr/bin/chromium", "/usr/bin/chromium-browser", "/usr/bin/google-chrome", "/usr/bin/google-chrome-stable"]) if (fs.existsSync(p)) return p;
  throw new Error("Chrome/Chromium not found: install it (see tests/README.md) or set CHROME_BIN");
}
module.exports = find();
