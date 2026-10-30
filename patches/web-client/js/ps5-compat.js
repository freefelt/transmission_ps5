/* smooth fade-out and fade-in of dialogs, menus and the inspector: removal of such elements is deferred for the duration of the exit animation (the client code calls remove() as usual) */
(function () {
    var reduce = window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches;
    var OUT_MS = 150, INSPECTOR_MS = 240, rm = Element.prototype.remove;
    function kind(el) {
        if (!el || el.nodeType !== 1 || !el.matches) return '';
        if (el.id === 'inspector') return 'inspector';
        if (el.matches('.context-menu, .overflow-menu')) return 'menu';
        if (el.id === 'prefs-dialog' || el.matches('dialog.dialog-container.popup')) return 'dialog';
        return '';
    }
    Element.prototype.remove = function () {
        var el = this, k = reduce ? '' : kind(el);
        if (!k || el.__ps5out || !el.isConnected) return rm.call(el);
        el.__ps5out = true;
        el.style.pointerEvents = 'none';
        if (k === 'inspector') {
            var w = el.offsetWidth;
            el.style.transition = 'margin-right ' + INSPECTOR_MS + 'ms ease, opacity ' + INSPECTOR_MS + 'ms ease';
            el.style.marginRight = (-w) + 'px';
            el.style.opacity = '0';
            setTimeout(function () { rm.call(el); }, INSPECTOR_MS + 30);
        } else {
            el.classList.add('ps5-out');
            setTimeout(function () { rm.call(el); }, OUT_MS + 30);
        }
    };
    function watch() {
        var area = document.getElementById('mainwin-workarea');
        if (!area || reduce) return;
        new MutationObserver(function (list) {
            list.forEach(function (m) {
                m.addedNodes.forEach(function (n) {
                    if (!n || n.id !== 'inspector' || n.__ps5in) return;
                    n.__ps5in = true;
                    var w = n.offsetWidth;
                    if (!w) return;
                    n.style.transition = 'none';
                    n.style.marginRight = (-w) + 'px';
                    n.style.opacity = '0';
                    void n.offsetWidth;
                    n.style.transition = 'margin-right ' + INSPECTOR_MS + 'ms ease, opacity ' + INSPECTOR_MS + 'ms ease';
                    n.style.marginRight = '0px';
                    n.style.opacity = '1';
                    setTimeout(function () { n.style.transition = ''; n.style.marginRight = ''; n.style.opacity = ''; }, INSPECTOR_MS + 60);
                });
            });
        }).observe(area, { childList: true });
    }
    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', watch); else watch();
})();
/* folder and file icons for the file list in the inspector (inserted by the patched client code) */
window.ps5FileIcon = function (isDir) {
    var NS = 'http://www.w3.org/2000/svg', svg = document.createElementNS(NS, 'svg'), path = document.createElementNS(NS, 'path');
    svg.setAttribute('viewBox', '0 0 24 24'); svg.setAttribute('aria-hidden', 'true'); svg.setAttribute('class', 'ps5-fico ' + (isDir ? 'dir' : 'file'));
    path.setAttribute('fill', 'currentColor');
    path.setAttribute('d', isDir ? 'M3 6.5A2.5 2.5 0 0 1 5.5 4h4l2 2.2h7A2.5 2.5 0 0 1 21 8.7v9.8a2.5 2.5 0 0 1-2.5 2.5h-13A2.5 2.5 0 0 1 3 18.5z'
                                  : 'M7 3h7l5 5v11.5A1.5 1.5 0 0 1 17.5 21h-10A1.5 1.5 0 0 1 6 19.5v-15A1.5 1.5 0 0 1 7.5 3zm6.2 1.8V9h4.2z');
    svg.appendChild(path);
    return svg;
};
/* The line "Downloading from 5 of 5 peers - ▼ 4.15 MB/s ▲ 0 kB/s" in the normal list rows is split in two parts: the caption on the left,
 * the speeds on the right (styles in transmission-app.css). Called from the application instead of setTextContent (see web_patch.pl). */
/* The torrent path (the same field as "Location" in the inspector: getDownloadDir) goes into an attribute of the row with the size; CSS shows it on the right. */
window.ps5Dir = function (t, el) {
    try {
        var dir = t.getDownloadDir();
        if (dir) el.setAttribute('data-ps5-dir', dir); else el.removeAttribute('data-ps5-dir');
    } catch (e) { /* no such field: show nothing */ }
};

/* The path for the "Open folder" item of the torrent menu: for a multi-file torrent its own folder (path + name), for a single-file one the folder that holds the file.
 * Stored in an attribute of the list row (normal and compact); the menu reads it from the row that was clicked. */
window.ps5Row = function (t, root) {
    try {
        var dir = t.getDownloadDir();
        if (!dir) { root.removeAttribute('data-ps5-path'); return; }
        var path = (t.getFileCount() > 1 || t.fields.ps5_in_folder) ? dir.replace(/\/+$/, '') + '/' + t.getName() : dir;
        root.setAttribute('data-ps5-path', path);
    } catch (e) { root.removeAttribute('data-ps5-path'); }
};

window.ps5Peers = function (el, text) {
    if (el.__ps5text === text) return;
    el.__ps5text = text;
    var m = /^(.*?) - ([\u25BC\u25B2].*)$/.exec(text);
    if (!m) { el.textContent = text; return; }
    var left = el.querySelector('.ps5-peers-left'), right = el.querySelector('.ps5-peers-speed');
    if (!left || !right) {
        el.textContent = '';
        left = document.createElement('span'); left.className = 'ps5-peers-left';
        right = document.createElement('span'); right.className = 'ps5-peers-speed';
        el.appendChild(left); el.appendChild(right);
    }
    if (left.textContent !== m[1]) left.textContent = m[1];
    if (right.textContent !== m[2]) right.textContent = m[2];
};

/* A JSON-RPC 2.0 reply from the daemon puts the error text into error.data.error_string, while the web interface reads error.data.errorString and therefore
 * shows only the generic "Invalid params". We add errorString (then the web interface shows our text), and for the actions where it
 * stays silent (settings, move, start) we show the text ourselves. */
(function () {
    if (!window.fetch || !window.Response) return;
    var native = window.fetch;
    var loud = { session_set: 1, 'session-set': 1, torrent_set_location: 1, 'torrent-set-location': 1, torrent_start: 1, 'torrent-start': 1,
                 torrent_start_now: 1, 'torrent-start-now': 1, torrent_verify: 1, 'torrent-verify': 1 };
    window.fetch = function (input, init) {
        var url = typeof input === 'string' ? input : (input && input.url) || '';
        var method = '';
        if (url.indexOf('rpc') >= 0 && init && typeof init.body === 'string') {
            try { method = JSON.parse(init.body).method || ''; } catch (e) { method = ''; }
        }
        if ((method === 'torrent_add' || method === 'torrent-add') && init) {
            /* "Fix for USB drives": the choice made in the add dialog goes to the server (only for folders on a USB drive) */
            try {
                var req = JSON.parse(init.body), prm = req.params || req.arguments;
                if (prm && typeof prm.download_dir === 'string' && /^\/mnt\/usb\d/i.test(prm.download_dir.trim()) && typeof window.ps5UsbFix === 'boolean') {
                    prm.ps5_usb_fix = window.ps5UsbFix;
                    init = Object.assign({}, init, { body: JSON.stringify(req) });
                }
            } catch (e) { /* leave the request untouched */ }
        }
        var p = native.call(this, input, init);
        if (!method) return p;
        return p.then(function (resp) {
            if (!resp || !resp.ok) return resp;
            return resp.clone().text().then(function (text) {
                var j;
                try { j = JSON.parse(text); } catch (e) { return resp; }
                var err = j && j.error, msg = err && err.data && (err.data.error_string || err.data.errorString);
                if (!msg) return resp;
                if (!err.data.errorString) err.data.errorString = msg;
                if (loud[method]) { try { alert(msg); } catch (x) { /* no alert: do nothing */ } }
                return new Response(JSON.stringify(j), { status: resp.status, statusText: resp.statusText, headers: resp.headers });
            });
        });
    };
})();

/* When adding a .torrent file fails, Transmission first calls alert() and then shows its own dialog with the same text:
 * two dialogs in a row. We keep the Transmission dialog and skip the alert that starts with "Error adding". */
(function () {
    var nativeAlert = window.alert;
    window.alert = function (message) {
        if (typeof message === 'string' && /^Error adding "/.test(message)) return;
        return nativeAlert.apply(this, arguments);
    };
})();

/* "Fix for USB drives" in the add-torrent dialog. If the destination folder is on a USB drive (/mnt/usbN), a checkbox appears in the dialog (on by default), and its value
 * goes to torrent_add as ps5_usb_fix (see fetch above). The checkbox can only be cleared after a warning (the "Turn off the fix" button), and "?" explains what it is about. */
(function () {
    var USB = /^\/mnt\/usb\d/i;
    window.ps5UsbFix = true;

    function modal(title, paragraphs, buttons) {
        var overlay = document.createElement('div');
        overlay.className = 'ps5-modal';
        var box = document.createElement('div');
        box.className = 'ps5-modal-box';
        var h = document.createElement('h3');
        h.textContent = title;
        box.appendChild(h);
        paragraphs.forEach(function (t) {
            var p = document.createElement('p');
            p.textContent = t;
            box.appendChild(p);
        });
        var bar = document.createElement('div');
        bar.className = 'ps5-modal-buttons';
        var closed = false;
        function close(cb) {
            if (closed) return;
            closed = true;
            overlay.classList.add('ps5-out');
            setTimeout(function () { overlay.remove(); }, 150);
            if (cb) cb();
        }
        buttons.forEach(function (b) {
            var bt = document.createElement('button');
            bt.type = 'button';
            bt.textContent = b.text;
            if (b.cls) bt.className = b.cls;
            bt.addEventListener('click', function () { close(b.onClick); });
            bar.appendChild(bt);
        });
        box.appendChild(bar);
        overlay.appendChild(box);
        /* a click outside the dialog = the last (safe) button */
        overlay.addEventListener('click', function (ev) { if (ev.target === overlay) close(buttons[buttons.length - 1].onClick); });
        document.body.appendChild(overlay);
        if (bar.lastChild && bar.lastChild.focus) bar.lastChild.focus();
        return overlay;
    }

    var HELP_TITLE = ps5t('Fix for USB drives');
    var HELP = [
        ps5t('The problem. USB drives with the exFAT or FAT32 file system have no “sparse” files. A torrent downloads pieces in a random order, and when the next piece lies far from the current end of the file, the system fills the whole gap before it with zeros. While this goes on, Transmission does not respond, and sometimes the whole console hangs.'),
        ps5t('The solution. The fix downloads pieces in order and no more than 64 MB ahead. The file grows gradually, so there are no large zero fills.'),
        ps5t('The price. The speed is lower, and if nobody has the nearby pieces right now, the download waits for a while.')
    ];
    var OFF_TITLE = ps5t('Turn off the fix for the USB drive?');
    var OFF = [
        ps5t('Without the fix Transmission downloads pieces in any order. When a piece lies far from the end of the file, the system fills the whole gap with zeros. This takes about 10 seconds for every 1 GB (assuming 100 MB/s writing; longer on slow drives). For a 50 GB torrent that is about 8 minutes.'),
        ps5t('All this time Transmission itself does not respond: its web interface freezes (the “Transmission for PS5” settings page keeps working). If the hang is longer than the manager’s wait time (2 minutes by default), it restarts the daemon, and the fill starts again. This time can be increased in the “Advanced settings”.'),
        ps5t('Do not open the torrent folder in file managers (BFpilot and the like) during this time and do not copy files to this drive: they use the same drive and can hang the whole console.'),
        ps5t('If this happens, do not panic: wait for the approximate time from the estimate above. As a last resort the hang stops when the USB drive is physically unplugged.'),
        ps5t('The hang lasts until the torrent files take all their space on the drive (for torrents of many files, as it progresses). After that the download goes without problems and faster than with the fix.')
    ];

    /* smooth expand and collapse of a row (height, bottom margin and opacity) so that the "Add Torrents" dialog resizes smoothly */
    var REDUCE = window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches;
    function slideRow(el, show) {
        if (REDUCE) { el.style.display = show ? 'flex' : 'none'; return; }
        if (el.__ps5t) clearTimeout(el.__ps5t);
        var T = 'height 200ms ease, margin-bottom 200ms ease, opacity 200ms ease';
        if (show) {
            el.style.display = 'flex';
            el.style.overflow = 'hidden';
            var h = el.scrollHeight, mb = getComputedStyle(el).marginBottom;
            el.style.transition = 'none'; el.style.height = '0px'; el.style.marginBottom = '0px'; el.style.opacity = '0';
            void el.offsetHeight;
            el.style.transition = T; el.style.height = h + 'px'; el.style.marginBottom = mb; el.style.opacity = '1';
            el.__ps5t = setTimeout(function () { el.style.transition = ''; el.style.height = ''; el.style.marginBottom = ''; el.style.overflow = ''; el.style.opacity = ''; }, 230);
        } else {
            var h2 = el.offsetHeight;
            el.style.overflow = 'hidden'; el.style.transition = 'none'; el.style.height = h2 + 'px';
            void el.offsetHeight;
            el.style.transition = T; el.style.height = '0px'; el.style.marginBottom = '0px'; el.style.opacity = '0';
            el.__ps5t = setTimeout(function () { el.style.display = 'none'; el.style.transition = ''; el.style.height = ''; el.style.marginBottom = ''; el.style.overflow = ''; el.style.opacity = ''; }, 230);
        }
    }

    function build(root) {
        var folder = root.querySelector('#add-dialog-folder-input');
        var start = root.querySelector('#auto-start-check');
        if (!folder || !start || root.querySelector('#ps5-usbfix-row')) return;
        var anchor = start.parentNode;
        var row = document.createElement('div');
        row.id = 'ps5-usbfix-row';
        row.style.display = 'none';
        var cb = document.createElement('input');
        cb.type = 'checkbox';
        cb.id = 'ps5-usbfix-check';
        cb.checked = true;
        var lb = document.createElement('label');
        lb.id = 'ps5-usbfix-label';
        lb.setAttribute('for', cb.id);
        lb.textContent = ps5t('Fix for USB drives');
        var q = document.createElement('button');
        q.type = 'button';
        q.id = 'ps5-usbfix-help';
        q.className = 'ps5-help-btn';
        q.textContent = '?';
        q.setAttribute('aria-label', ps5t('What is this'));
        row.appendChild(cb);
        row.appendChild(lb);
        row.appendChild(q);
        anchor.parentNode.insertBefore(row, anchor.nextSibling);

        var wasUsb = null;
        function refresh() {
            var usb = USB.test((folder.value || '').trim());
            if (usb === wasUsb) return;
            var first = wasUsb === null;
            wasUsb = usb;
            if (first) row.style.display = usb ? 'flex' : 'none'; else slideRow(row, usb);
            if (usb) { cb.checked = true; window.ps5UsbFix = true; }
        }
        folder.addEventListener('input', refresh);
        folder.addEventListener('change', refresh);
        refresh();
        var timer = setInterval(function () { if (!document.body.contains(root)) clearInterval(timer); else refresh(); }, 400);

        q.addEventListener('click', function () { modal(HELP_TITLE, HELP, [{ text: ps5t('Got it') }]); });
        cb.addEventListener('change', function () {
            if (cb.checked) { window.ps5UsbFix = true; return; }
            modal(OFF_TITLE, OFF, [
                { text: ps5t('Turn off the fix'), cls: 'danger', onClick: function () { cb.checked = false; window.ps5UsbFix = false; } },
                { text: ps5t('Cancel'), onClick: function () { cb.checked = true; window.ps5UsbFix = true; } }
            ]);
        });
    }

    new MutationObserver(function (list) {
        Array.prototype.forEach.call(list, function (m) {
            Array.prototype.forEach.call(m.addedNodes, function (n) {
                if (n.nodeType === 1 && n.querySelector && n.querySelector('#add-dialog-folder-input')) build(n);
            });
        });
    }).observe(document.documentElement, { childList: true, subtree: true });
})();

/* Download folder tiles: the add-torrent dialog, "Set Torrent Location" and the Transmission settings ("Download to"). Instead of the text field (it is hidden; the value
 * still lives in it, so everything else in the interface works as before) tiles of the "Download folders" are shown: path, disk kind, free space; a disk that is not connected
 * is gray and unavailable. The list comes from the ps5_folders RPC method. If the list cannot be fetched, the usual field stays. */
(function () {
    var sid = '';
    function call(method, params, tries) {
        tries = tries || 0;
        return fetch('../rpc', { method: 'POST', headers: { 'Content-Type': 'application/json', 'X-Transmission-Session-Id': sid }, body: JSON.stringify({ jsonrpc: '2.0', method: method, params: params || {}, id: 1 }) }).then(function (r) {
            if (r.status === 409 && tries < 2) { sid = r.headers.get('X-Transmission-Session-Id') || ''; return call(method, params, tries + 1); }
            return r.json();
        });
    }
    function gb(b) { var g = b / 1073741824; return g >= 1 ? ps5t('{1} GB', [g.toFixed(1)]) : ps5t('{1} MB', [Math.round(b / 1048576)]); }
    var KIND = { ssd: 'M.2 SSD', usb: ps5t('USB drive'), int: ps5t('Internal drive') };
    function tile(f) {
        var b = document.createElement('button');
        b.type = 'button';
        b.className = 'ps5-tile' + (f.mounted ? '' : ' off');
        b.setAttribute('data-path', f.path);
        if (!f.mounted) b.disabled = true;
        var ico = document.createElement('span');
        ico.className = 'ps5-disk-ico ' + f.kind;
        ico.setAttribute('aria-hidden', 'true');
        var body = document.createElement('span');
        body.className = 'ps5-tile-body';
        var p = document.createElement('span');
        p.className = 'ps5-tile-path';
        p.textContent = f.path;
        var s = document.createElement('span');
        s.className = 'ps5-tile-sub';
        if (!f.mounted) {
            var bad = document.createElement('span');
            bad.className = 'bad';
            bad.textContent = ps5t('drive not connected');
            s.appendChild(document.createTextNode((KIND[f.kind] || '') + ' · '));
            s.appendChild(bad);
        } else {
            s.appendChild(document.createTextNode(ps5t('{1} · {2} free of {3}', [KIND[f.kind] || '', gb(f.free), gb(f.total)])));
            if (f.kind === 'int' || f.fsbad) {
                var nr = document.createElement('span');
                nr.className = 'bad';
                nr.textContent = ps5t(' · not recommended') + (f.fsbad && f.fs ? ' (' + f.fs + ')' : '');
                s.appendChild(nr);
            }
        }
        body.appendChild(p);
        body.appendChild(s);
        b.appendChild(ico);
        b.appendChild(body);
        return b;
    }
    function attach(input) {
        if (!input || input.getAttribute('data-ps5-tiles')) return;
        input.setAttribute('data-ps5-tiles', '1');
        var box = document.createElement('div');
        box.className = 'ps5-tiles';
        var note = document.createElement('div');
        note.className = 'ps5-tiles-note';
        note.textContent = ps5t('Loading the folder list…');
        box.appendChild(note);
        input.parentNode.insertBefore(box, input);
        input.style.display = 'none';
        function sync() {
            var cur = (input.value || '').trim();
            Array.prototype.forEach.call(box.querySelectorAll('.ps5-tile'), function (t) { t.classList.toggle('sel', t.getAttribute('data-path') === cur); });
        }
        call('ps5_folders').then(function (j) {
            var list = j && j.result && j.result.folders;
            if (!list) throw new Error('no list');
            box.textContent = '';
            if (!list.length) {
                note.textContent = ps5t('There are no download folders. Add them on the “Transmission for PS5” page (port 9092), the “Download folders” section.');
                box.appendChild(note);
                return;
            }
            list.forEach(function (f) {
                var t = tile(f);
                t.addEventListener('click', function () {
                    input.value = f.path;
                    ['input', 'change'].forEach(function (n) { input.dispatchEvent(new Event(n, { bubbles: true })); });
                    sync();
                });
                box.appendChild(t);
            });
            sync();
        }).catch(function () { box.remove(); input.style.display = ''; });
        var timer = setInterval(function () { if (!document.body.contains(input)) clearInterval(timer); else sync(); }, 800);
    }
    function scan(root) {
        if (!root || !root.querySelectorAll) return;
        var fs = root.querySelector('#free-space-text');
        if (fs && root.querySelector('#add-dialog-folder-input')) fs.style.display = 'none';
        Array.prototype.forEach.call(root.querySelectorAll('#add-dialog-folder-input, #torrent-path, input[data-key="download_dir"]'), attach);
    }
    new MutationObserver(function (list) {
        Array.prototype.forEach.call(list, function (m) {
            Array.prototype.forEach.call(m.addedNodes, function (n) { if (n.nodeType === 1) scan(n); });
        });
    }).observe(document.documentElement, { childList: true, subtree: true });
    document.addEventListener('DOMContentLoaded', function () { setTimeout(function () { scan(document); }, 1500); });
})();

/* The "Open folder" item in the torrent context menu (setting on the manager page: PS5_CONFIG.openFolder = off | bfpilot | webfm).
 * BFpilot: http://<console address>:5905/#<path>, PS5 Web File Manager: http://<console address>:8888/#<path>. In a normal browser it opens
 * in a new tab, in the console browser (the User-Agent contains "PlayStation") in the same window. */
(function () {
    var lastRow = null;
    document.addEventListener('contextmenu', function (ev) {
        lastRow = ev.target && ev.target.closest ? ev.target.closest('#torrent-list .torrent') : null;
    }, true);
    window.ps5OpenFolder = function (mode, path) {
        var u = 'http://' + location.hostname + ':' + (mode === 'bfpilot' ? 5905 : 8888) + '/#' + encodeURIComponent(path);
        if (/PlayStation/i.test(navigator.userAgent || '')) location.href = u; else window.open(u, '_blank', 'noopener');
    };
    new MutationObserver(function (list) {
        var mode = (window.PS5_CONFIG && window.PS5_CONFIG.openFolder) || 'off';
        if (mode !== 'bfpilot' && mode !== 'webfm') return;
        Array.prototype.forEach.call(list, function (m) {
            Array.prototype.forEach.call(m.addedNodes, function (n) {
                if (n.nodeType !== 1 || !n.classList || !n.classList.contains('context-menu')) return;
                var path = lastRow && lastRow.getAttribute('data-ps5-path');
                if (!path || n.querySelector('.ps5-openfolder')) return;
                var item = document.createElement('div');
                item.setAttribute('role', 'menuitem');
                item.className = 'context-menuitem ps5-openfolder';
                item.textContent = ps5t('Open folder');
                item.addEventListener('click', function () { window.ps5OpenFolder(mode, path); document.body.click(); });
                var loc = ps5t('Set location…');   // the item has the translated text: find it by that, not by an English word
                var anchor = Array.prototype.find.call(n.querySelectorAll('.context-menuitem'), function (e) { return e.textContent.trim() === loc || /location/i.test(e.textContent); });
                if (anchor && anchor.parentNode) anchor.parentNode.insertBefore(item, anchor.nextSibling); else n.appendChild(item);
            });
        });
    }).observe(document.documentElement, { childList: true, subtree: true });
})();

/* The "Folder" button on the toolbar: after Inspector and a separator. Does the same as the "Open folder" item of the torrent menu, but for the selected torrent; it is needed where
 * the context menu is unavailable (the console browser). Shown only when the "Open folder item in the torrent menu" setting is not "Do not show". */
(function () {
    function selectedPath() {
        var r = document.querySelector('#torrent-list .torrent.selected');
        return r ? r.getAttribute('data-ps5-path') : null;
    }
    function init() {
        var mode = (window.PS5_CONFIG && window.PS5_CONFIG.openFolder) || 'off';
        var insp = document.getElementById('toolbar-inspector');
        if ((mode !== 'bfpilot' && mode !== 'webfm') || !insp || document.getElementById('toolbar-openfolder')) return;
        var sep = document.createElement('div');
        sep.className = 'toolbar-separator';
        var b = document.createElement('button');
        b.className = 'toolbar-button';
        b.id = 'toolbar-openfolder';
        b.type = 'button';
        b.tabIndex = 0;
        b.setAttribute('aria-label', ps5t('Open folder'));
        b.title = ps5t('Open folder');
        var svg = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
        svg.setAttribute('viewBox', '0 0 24 24');
        b.appendChild(svg);
        b.appendChild(document.createTextNode(ps5t('Folder')));
        insp.parentNode.insertBefore(sep, insp.nextSibling);
        insp.parentNode.insertBefore(b, sep.nextSibling);
        function refresh() { b.disabled = !selectedPath(); }
        refresh();
        new MutationObserver(refresh).observe(document.getElementById('torrent-list') || document.body, { attributes: true, attributeFilter: ['class'], childList: true, subtree: true });
        b.addEventListener('click', function () {
            var p = selectedPath();
            if (p) window.ps5OpenFolder(mode, p);
        });
    }
    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', function () { setTimeout(init, 0); }); else setTimeout(init, 0);
})();

/* The page is open in the PS5 console browser (the User-Agent contains "PlayStation"): it has no file access, so in the add dialog
 * we hide the .torrent file chooser (the link and magnet fields stay). The ps5-console class is put on <html>. */
(function () {
    var onConsole = /PlayStation/i.test(navigator.userAgent || '');
    if (onConsole) document.documentElement.className += ' ps5-console';
    function hideChooser(root) {
        if (!onConsole || !root || !root.querySelectorAll) return;
        var inputs = root.querySelectorAll('input[type="file"]');
        for (var i = 0; i < inputs.length; i++) {
            var inp = inputs[i], lab = inp.id ? document.querySelector('label[for="' + inp.id + '"]') : null;
            inp.style.display = 'none';
            if (lab) lab.style.display = 'none';
            var next = inp.nextElementSibling;
            if (next && /^Or enter/i.test(next.textContent)) next.textContent = 'Enter a URL or magnet link:';
        }
    }
    document.addEventListener('DOMContentLoaded', function () {
        if (!onConsole || !window.MutationObserver) return;
        new MutationObserver(function (muts) {
            for (var i = 0; i < muts.length; i++) {
                for (var k = 0; k < muts[i].addedNodes.length; k++) hideChooser(muts[i].addedNodes[k]);
            }
        }).observe(document.body, { childList: true, subtree: true });
    });
})();
/* Compatibility with the built-in PS5 browser (WebKit without some of the new methods). Included before the application. */
(function () {
    var A = Array.prototype;
    function def(obj, name, fn) {
        if (!obj[name]) Object.defineProperty(obj, name, { value: fn, writable: true, configurable: true });
    }
    def(A, 'toSorted', function (cmp) { return this.slice().sort(cmp); });
    def(A, 'toReversed', function () { return this.slice().reverse(); });
    def(A, 'with', function (i, v) { var c = this.slice(); c[i < 0 ? c.length + i : i] = v; return c; });
    def(A, 'findLast', function (f, t) { for (var i = this.length - 1; i >= 0; i--) if (f.call(t, this[i], i, this)) return this[i]; return undefined; });
    def(A, 'findLastIndex', function (f, t) { for (var i = this.length - 1; i >= 0; i--) if (f.call(t, this[i], i, this)) return i; return -1; });
    def(A, 'at', function (i) { i = Math.trunc(i) || 0; if (i < 0) i += this.length; return this[i]; });
    def(String.prototype, 'at', function (i) { i = Math.trunc(i) || 0; if (i < 0) i += this.length; return this.charAt(i); });
    def(String.prototype, 'replaceAll', function (a, b) {
        if (a instanceof RegExp) return this.replace(a, b);
        var re = new RegExp(String(a).replace(/[.*+?^${}()|[\]\\]/g, '\\$&'), 'g');
        return this.replace(re, typeof b === 'function' ? b : String(b).replace(/\$/g, '$$$$'));
    });
    if (!Object.hasOwn) Object.hasOwn = function (o, k) { return Object.prototype.hasOwnProperty.call(o, k); };
})();
