/* The "free space on disks" panel above the Transmission toolbar. Shows only the disks that hold
 * torrent downloads (folders come from torrent-get, free and total space from free-space), and groups the folders by
 * console disk (Data = /data, /mnt/usb1, /mnt/ext1 ...). Needs nothing from the manager: only daemon RPC. */
(function () {
    var POLL_MS = 30000, sid = '', bar = null, timer = 0;
    function rpc(method, args, retried) {
        var h = { 'Content-Type': 'application/json' };
        if (sid) h['X-Transmission-Session-Id'] = sid;
        return fetch('../rpc', { method: 'POST', headers: h, body: JSON.stringify({ method: method, arguments: args || {} }) }).then(function (r) {
            if (r.status === 409 && !retried) { sid = r.headers.get('X-Transmission-Session-Id') || ''; return rpc(method, args, true); }
            if (!r.ok) throw new Error('http ' + r.status);
            return r.json();
        });
    }
    function size(n) {
        var u = ['B', 'KB', 'MB', 'GB', 'TB', 'PB'], i = 0;
        while (n >= 1024 && i < u.length - 1) { n /= 1024; i++; }
        return ps5t('{1} ' + u[i], [n >= 100 || i === 0 ? Math.round(n) : Math.round(n * 10) / 10]);
    }
    var folders = []; // [{path, kind}] from ps5_folders: the daemon decides the disk kind by device (ssd, usb, int), not by the mount point name
    function entryOf(p) {
        var best = null;
        folders.forEach(function (f) { if ((p === f.path || p.indexOf(f.path + '/') === 0) && (!best || f.path.length > best.path.length)) best = f; });
        return best;
    }
    function label(path) {
        var e = entryOf(path);
        if (e && e.kind !== 'int' && /^\/data\//.test(path)) { var m2 = path.match(/^(\/data\/[^\/]+)/); return m2 ? m2[1] : path; } // alias of the external disk (/data/ext1)
        if (/^\/data(\/|$)/.test(path)) return 'Data';
        var m = path.match(/^(\/mnt\/[^\/]+)/) || path.match(/^(\/[^\/]+)/);
        return m ? m[1] : path;
    }
    function kind(label, path) {
        var e = entryOf(path || '');
        if (e) return e.kind === 'ssd' || e.kind === 'usb' ? e.kind : 'int';
        return /^\/mnt\/ext/.test(label) ? 'ssd' : /^\/mnt\/usb/.test(label) ? 'usb' : 'int';
    }
    function render(list) {
        list = list.filter(function (d) { return d && d.free >= 0 && d.total > 0; });
        if (!bar) return;
        bar.textContent = '';
        list.forEach(function (d) {
            var used = Math.max(0, Math.min(100, Math.round((d.total - d.free) * 100 / d.total)));
            var box = document.createElement('div');
            box.className = 'ps5-disk' + (used >= 92 ? ' crit' : used >= 80 ? ' warn' : '');
            var ico = document.createElement('span');
            ico.className = 'ps5-disk-ico ' + kind(d.label, d.path); // ssd: /mnt/extN (M.2), usb: /mnt/usbN (flash drive), int: internal disk
            ico.setAttribute('aria-hidden', 'true');
            var text = document.createElement('div');
            text.className = 'ps5-disk-text';
            text.textContent = ps5t('{1}: {2} free of {3}, {4}% used', [d.label, size(d.free), size(d.total), used]);
            var meter = document.createElement('div'), fill = document.createElement('i'), body = document.createElement('div');
            meter.className = 'ps5-meter'; fill.style.width = used + '%'; meter.appendChild(fill);
            body.className = 'ps5-disk-body'; body.appendChild(text); body.appendChild(meter);
            box.appendChild(ico); box.appendChild(body); bar.appendChild(box);
        });
        bar.className = list.length ? 'has' : '';
    }
    function update() {
        var active = window.PS5_CONFIG && window.PS5_CONFIG.diskbarMode === 'active'; // only disks of running transfers (status is not "stopped")
        rpc('ps5_folders').then(function (j) { folders = (j && j.result && j.result.folders) || []; }, function () { /* no list: kind by mount point name */ }).then(function () {
            return rpc('torrent-get', { fields: ['downloadDir', 'status'] });
        }).then(function (r) {
            var seen = {}, groups = {};
            ((r.arguments && r.arguments.torrents) || []).forEach(function (t) {
                if (active && t.status === 0) return;
                if (t.downloadDir && !seen[t.downloadDir]) { seen[t.downloadDir] = 1; var l = label(t.downloadDir); if (!groups[l]) groups[l] = t.downloadDir; }
            });
            var labels = Object.keys(groups).sort(function (a, b) { return a === 'Data' ? -1 : b === 'Data' ? 1 : (a < b ? -1 : 1); });
            return Promise.all(labels.map(function (l) {
                return rpc('free-space', { path: groups[l] }).then(function (x) {
                    var a = x.arguments || {};
                    return { label: l, path: groups[l], free: a['size-bytes'], total: a['total_size'] };
                }).catch(function () { return null; });
            }));
        }).then(render).catch(function () { /* the daemon does not answer: the panel stays as it was */ });
    }
    function start() {
        var main = document.getElementById('mainwin');
        if (!main) return;
        if (window.PS5_CONFIG && (window.PS5_CONFIG.diskbar === false || window.PS5_CONFIG.diskbarMode === 'off')) return; // the panel is turned off in the settings (ps5-config.js)
        bar = document.createElement('div');
        bar.id = 'ps5-diskbar';
        main.insertBefore(bar, main.firstChild);
        update();
        timer = setInterval(update, POLL_MS);
    }
    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start); else start();
})();
