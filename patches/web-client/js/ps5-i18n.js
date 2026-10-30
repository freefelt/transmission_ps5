/* ps5: translation of the Transmission web interface. The client code calls ps5t("English text") and ps5t("template with {1}", [values]); the window.PS5_I18N
 * dictionary ({lang, plural, map}) is written by the manager into ps5-lang.js for the selected language. Text missing from the dictionary stays English, as in the code.
 * ps5n("{1} peers", [number], n, fallback English text): words with a number (forms separated by |: one|few|many).
 * Texts from index.html are marked with data-i18n (text) and data-i18n-aria-label / -placeholder / -title (attributes): apply() translates them when the page loads. */
(function () {
    var I = window.PS5_I18N || {}, M = I.map || {}, has = Object.prototype.hasOwnProperty, PR = null;
    try { PR = new Intl.PluralRules(I.plural || 'en'); } catch (e) { PR = null; }
    function sub(t, a) { return a ? t.replace(/\{(\d)\}/g, function (m, d) { var v = a[d - 1]; return v === undefined ? m : v; }) : t; }
    window.ps5t = function (s, a) {
        var m = /^(\s*)([\s\S]*?)(\s*)$/.exec(String(s)), core = m[2];
        return m[1] + sub(has.call(M, core) ? M[core] : core, a) + m[3];
    };
    window.ps5n = function (key, a, n, fallback) {
        if (!has.call(M, key)) return fallback;
        var f = M[key].split('|'), c = PR ? PR.select(n) : (n === 1 ? 'one' : 'other');
        var i = c === 'one' ? 0 : c === 'few' ? 1 : c === 'many' ? 2 : f.length - 1;
        return sub(f[Math.min(i, f.length - 1)], a);
    };
    function apply() {
        var i, e, list = document.querySelectorAll('[data-i18n]');
        for (i = 0; i < list.length; i++) { e = list[i]; e.textContent = window.ps5t(e.getAttribute('data-i18n')); }
        ['aria-label', 'placeholder', 'title'].forEach(function (name) {
            var l = document.querySelectorAll('[data-i18n-' + name + ']');
            for (var k = 0; k < l.length; k++) l[k].setAttribute(name, window.ps5t(l[k].getAttribute('data-i18n-' + name)));
        });
    }
    if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', apply); else apply();
})();
