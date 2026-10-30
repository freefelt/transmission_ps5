#!/usr/bin/perl
# Patches to the Transmission web interface for the built-in PS5 browser (WebKit).
#   perl web_patch.pl <source public_html> <where to put the patched copy> [<folder with favicon.png and apple-touch-icon.png>]
# 1. The PS5 browser does not support @media (prefers-color-scheme): both color themes, together with the "High contrast UI"
#    mode, live inside such blocks in Transmission, so the color variables are not set: no icons, the progress bars of
#    finished torrents are invisible, and the "High contrast UI" switch changes nothing. We put the light theme (and its
#    high-contrast mode) in as unconditional rules BEFORE the @media blocks: modern browsers still
#    override them with their own theme, while a browser without @media gets a working one.
# 2. In high contrast mode the fill of a finished torrent is black (invisible on a black background): we make it green.
# 3. ps5-compat.js: polyfills for new methods that old WebKit may lack (toSorted and others).
# 4. Themes: ps5-theme-dark.css and ps5-theme-light.css (palette + rounded "cards", tidy fields and menus).
#    index.html includes ps5-theme.css: the daemon puts it there at start (and the manager on a settings change) as a copy
#    of one of the two themes. No file: the page stays stock.
# 5. Our own tab icon (favicon) and apple-touch-icon instead of the stock ones.
# 5a. Icon sets (ps5-icons-rounded|lucide|bold|color.css, see patches/web-client/icons/README.md) replace the icons of the buttons, the burger and
#    the transfers via CSS (Transmission JS untouched). index.html includes ps5-icons.css: the daemon puts it there at start (and the manager
#    on a settings change) as a copy of the selected set.
# 6a. Status filter list: width by the longest item, height like the Filter field; the tracker list is hidden; speeds on the right.
# 6. A small top margin of the page and the "free space on disks" panel (ps5-panel.js): shows only the disks
#    that hold torrent downloads (folders from torrent-get, free and total space from free-space).
use strict; use warnings;
use FindBin;
my $wcdir = $FindBin::Bin;   # the folder of this script: css/ and js/ next to it hold the files that are added to the web client
my ($src, $dst, $assets) = @ARGV; die "usage: web_patch.pl SRC DST\n" unless $src && $dst;
system("rm -rf '$dst' && mkdir -p '$dst' && cp -a '$src/.' '$dst/'") == 0 or die "copy failed\n";

sub slurp { my $p = shift; open(my $f, '<:raw', $p) or die "open $p: $!"; local $/; my $d = <$f>; close $f; return $d }
sub spit { my ($p, $d) = @_; open(my $f, '>:raw', $p) or die "write $p: $!"; print $f $d; close $f }

# --- CSS ---
my $css = slurp("$dst/transmission-app.css");
my $start = index($css, '@media(prefers-color-scheme:light){');
die "no \@media(prefers-color-scheme:light) block\n" if $start < 0;
my $dark = index($css, '@media(prefers-color-scheme:dark){');
die "no \@media(prefers-color-scheme:dark) block\n" if $dark < 0;
# content of the light block: from the first { to the matching }
my $open = index($css, '{', $start); my $depth = 0; my $end = -1;
for (my $i = $open; $i < length($css); $i++) {
    my $c = substr($css, $i, 1);
    if ($c eq '{') { $depth++ } elsif ($c eq '}') { $depth--; if ($depth == 0) { $end = $i; last } }
}
die "no end of the light block found\n" if $end < 0;
my $light = substr($css, $open + 1, $end - $open - 1);
# content of the dark block (for the "dark" theme)
my $dopen = index($css, '{', $dark); my $ddepth = 0; my $dend = -1;
for (my $i = $dopen; $i < length($css); $i++) {
    my $c = substr($css, $i, 1);
    if ($c eq '{') { $ddepth++ } elsif ($c eq '}') { $ddepth--; if ($ddepth == 0) { $dend = $i; last } }
}
die "no end of the dark block found\n" if $dend < 0;
my $darkinner = substr($css, $dopen + 1, $dend - $dopen - 1);
die "the dark block does not look like a theme\n" unless $darkinner =~ /^:root\{--color-bg-hover/ && $darkinner =~ /:root \.contrast-more\{/;
die "the light block does not look like a theme\n" unless $light =~ /^:root\{--color-bg-hover/ && $light =~ /:root \.contrast-more\{/;
my $fallback = "/* ps5: default theme for browsers without \@media(prefers-color-scheme) */" . $light;
substr($css, $dark, 0) = $fallback;
$css .= "\n/* ps5: the fill of a finished torrent in high contrast mode is green (it was black and disappeared on the dark background) */\n"
      . ":root .contrast-more{--color-progressbar-seed-1:#1e8a45;--color-progressbar-seed-2:#fff;--progress-bar-shadow-2:0 0 0 #000}\n"
      . slurp("$wcdir/css/app-additions.css");
spit("$dst/transmission-app.css", $css);

# --- polyfills ---
spit("$dst/ps5-compat.js", slurp("$wcdir/js/ps5-compat.js"));

# --- themes ---
my $common = slurp("$wcdir/css/theme-common.css");
my $dark_pal = slurp("$wcdir/css/theme-dark-palette.css");
my $light_pal = slurp("$wcdir/css/theme-light-palette.css");
spit("$dst/ps5-theme-dark.css", "/* ps5: dark theme */\n" . $darkinner . "\n" . $dark_pal . $common);
spit("$dst/ps5-theme-light.css", "/* ps5: light theme */\n" . $light . "\n" . $light_pal . $common);

# --- our own tab icons ---
if (defined $assets && -d $assets) {
    for my $f (qw(favicon.png apple-touch-icon.png)) { die "no $assets/$f\n" unless -f "$assets/$f"; system("cp '$assets/$f' '$dst/images/$f'") == 0 or die "cp $f\n" }
    unlink("$dst/images/favicon.ico", "$dst/images/favicon.svg");
    for my $f (glob("$assets/ps5-icons-*.css")) { system("cp '$f' '$dst/'") == 0 or die "cp $f\n" }
}

# --- free space on disks panel ---
spit("$dst/ps5-panel.js", slurp("$wcdir/js/ps5-panel.js"));

# --- index.html: include the polyfills before the application ---
# --- interface translation: the ps5-lang.js dictionary (the manager writes it for the selected language) and the ps5-i18n.js translator ---
spit("$dst/ps5-lang.js", "window.PS5_I18N=null;\n");   # empty stub: the manager replaces it at start and on a language change
spit("$dst/ps5-i18n.js", slurp("$wcdir/js/ps5-i18n.js"));

my $appjs = slurp("$dst/transmission-app.js");
my $n_down = ($appjs =~ s/,b\(t,c\.join\(" "\)\)\}else if\(e\.isSeeding\(\)\)/,ps5Peers(t,c.join(" "))}else if(e.isSeeding())/);
my $n_seed = ($appjs =~ s/\.join\(" "\);b\(t,i\)\}else if\(e\.isChecking\(\)\)/.join(" ");ps5Peers(t,i)}else if(e.isChecking())/);
my $n_dir = ($appjs =~ s/n\.renderProgressDetails\(e,t,l\),X\.renderProgressbar\(e,t,c\)/n.renderProgressDetails(e,t,l),ps5Dir(t,l),X.renderProgressbar(e,t,c)/);
my $n_fld = ($appjs =~ s/"file_count","name","primary_mime_type"/"file_count","name","ps5_in_folder","primary_mime_type"/);
my $n_ico = ($appjs =~ s/n\.dataset\.iconMultifile=e\.getFileCount\(\)>1\?"true":"false"/n.dataset.iconMultifile=(e.getFileCount()>1||e.fields.ps5_in_folder)?"true":"false"/);
($n_fld == 1 && $n_ico == 1) or die "web_patch: no torrent field list or folder icon in transmission-app.js (fields: $n_fld, icon: $n_ico)\n";
my $n_row1 = ($appjs =~ s/render\(e,t,r\)\{let o=t\.isStopped\(\);r\.classList\.toggle\("paused",o\);/render(e,t,r){ps5Row(t,r);let o=t.isStopped();r.classList.toggle("paused",o);/);
my $n_row2 = ($appjs =~ s/render\(e,t,r\)\{r\.classList\.toggle\("paused",t\.isStopped\(\)\);let\{labels:o,name:s/render(e,t,r){ps5Row(t,r);r.classList.toggle("paused",t.isStopped());let{labels:o,name:s/);
($n_row1 == 1 && $n_row2 == 1) or die "web_patch: no list row render in transmission-app.js (normal: $n_row1, compact: $n_row2)\n";
($n_down == 1 && $n_seed == 1 && $n_dir == 1) or die "web_patch: no speed lines in transmission-app.js (download: $n_down, seed: $n_seed)\n";
# ---- translation inside the client code (THIS BLOCK WAS ONCE PRODUCED by the web_i18n_data generator (removed, the dictionary now lives in locales/*.strings; check: tools/web-i18n-check.js) and by a /tmp rule script; for the list of texts and templates see locales/*.strings) ----
# 1) exact replacements in the client code: texts with insertions become ps5t("template {1}", [values]) calls; every replacement must match EXACTLY once
{
    my @rules = (
        [ 'function Oe(n,e,t,r,o){for(let s of n.children)', 'function Oe(n,e,t,r,o){{let p=[...t.children].findIndex(c=>!c.classList.contains("hidden")),q=[...t.children].indexOf(r);t.dataset.ps5dir=q>=p?"next":"prev"}for(let s of n.children)' ],
        [ '`ETA: ${G.timeInterval(e,1)}`', 'ps5t("ETA: {1}",[G.timeInterval(e,1)])' ],
        [ '`${o.size(d)} of ${o.size(c)} (${l}%)`', 'ps5t("{1} of {2} ({3}%)",[o.size(d),o.size(c),l])' ],
        [ '`${o.size(t)} of ${o.size(e)} (${o.percentString(r,1)}%)`', 'ps5t("{1} of {2} ({3}%)",[o.size(t),o.size(e),o.percentString(r,1)])' ],
        [ 'r.subtree=t,this.elements.root=r;', 'r.subtree=t,t.children&&r.classList.add("ps5-dir"),this.elements.root=r;' ],
        [ 'b(s,t.name),r.append(s),r.name_container=s', 'b(s,t.name),s.insertBefore(window.ps5FileIcon(!!t.children),s.firstChild),r.append(s),r.name_container=s' ],
        [ 'r.style.paddingLeft=`${t.depth*20}px`', 'r.style.paddingLeft=`${t.depth*26}px`' ],
        [ '"Destination folder: "', 'ps5t("Destination folder: ")' ],
        [ '`Append "part" to incomplete files\' names`', 'ps5t(\'Append "part" to incomplete files\\\' names\')' ],
        [ 'countString(n,e,t){return`${this.number(t)} ${this.ngettext(n,e,t)}`}', 'countString(n,e,t){return ps5n("{1} "+e,[this.number(t)],t,`${this.number(t)} ${t===1?n:e}`)}' ],
        [ '`${r.slice(0,-1).join(", ")} and ${r.slice(-1)}`', 'ps5t("{1} and {2}",[r.slice(0,-1).join(", "),r.slice(-1)])' ],
        [ 'i&&(c.push(e.getPeersSendingToUs(),"of",r.countString("peer","peers",i)),a&&c.push("and")),a&&c.push(r.countString("web seed","web seeds",a))', 'i&&c.push(ps5t("{1} of {2}",[e.getPeersSendingToUs(),r.countString("peer","peers",i)])),a&&c.push(i?ps5t("and {1}",[r.countString("web seed","web seeds",a)]):r.countString("web seed","web seeds",a))' ],
        [ '["Seeding to",e.getPeersGettingFromUs(),"of",r.countString("peer","peers",e.getPeersConnected()),"-",', '["Seeding to",ps5t("{1} of {2}",[e.getPeersGettingFromUs(),r.countString("peer","peers",e.getPeersConnected())]),"-",' ],
        [ '["Verifying local data (",r.percentString(100*e.getRecheckProgress(),1),"% tested)"].join("")', 'ps5t("Verifying local data ({1}% tested)",[r.percentString(100*e.getRecheckProgress(),1)])' ],
        [ 'd=["Magnetized transfer - ",l," metadata (",o.percentString(m,1),"%)"].join("")', 'd=ps5t("Magnetized transfer - "+l+" metadata ({1}%)",[o.percentString(m,1)])' ],
        [ 'c.push(o.size(s)," of ",o.size(t.getTotalSize())," (",t.getPercentDoneStr(),"%)")', 'c.push(ps5t("{1} of {2} ({3}%)",[o.size(s),o.size(t.getTotalSize()),t.getPercentDoneStr()]))' ],
        [ 'c.push(", uploaded ",o.size(t.getUploadedEver())," (Ratio: ",o.ratioString(t.getUploadRatio()),")")', 'c.push(ps5t(", uploaded {1} (Ratio: {2})",[o.size(t.getUploadedEver()),o.ratioString(t.getUploadRatio())]))' ],
        [ 'c.push(o.size(s-t.getLeftUntilDone())," of ",o.size(s)," (",t.getPercentDoneStr(),"%)")', 'c.push(ps5t("{1} of {2} ({3}%)",[o.size(s-t.getLeftUntilDone()),o.size(s),t.getPercentDoneStr()]))' ],
        [ 'c.push(o.timeInterval(t.getETA(),1)," remaining")', 'c.push(ps5t("{1} remaining",[o.timeInterval(t.getETA(),1)]))' ],
        [ '`${e?"Disable":"Enable"} Notifications`', 'ps5t(e?"Disable Notifications":"Enable Notifications")' ],
        [ '`Tracker returned a warning: ${e}`', 'ps5t("Tracker returned a warning: {1}",[e])' ],
        [ '`Tracker returned an error: ${e}`', 'ps5t("Tracker returned an error: {1}",[e])' ],
        [ '`Error: ${e}`', 'ps5t("Error: {1}",[e])' ],
        [ '`${o.size(d)} of ${o.size(c)} (${l}%), ${o.size(g)} Unverified`', 'ps5t("{1} of {2} ({3}%), {4} Unverified",[o.size(d),o.size(c),l,o.size(g)])' ],
        [ '`${o.size(d)} (+${o.size(g)} discarded after failed checksum)`', 'ps5t("{1} (+{2} discarded after failed checksum)",[o.size(d),o.size(g)])' ],
        [ '`${o.size(d)} (Ratio: ${o.ratioString(S.ratio(d,g))})`', 'ps5t("{1} (Ratio: {2})",[o.size(d),o.ratioString(S.ratio(d,g))])' ],
        [ '`${o.timeInterval(u)} ago`', 'ps5t("{1} ago",[o.timeInterval(u)])' ],
        [ '`${o.size(d)} (${h} pieces @ ${o.mem(x)})`', 'ps5t("{1} ({2} pieces @ {3})",[o.size(d),h,o.mem(x)])' ],
        [ '`${o.size(d)} (${h} pieces)`', 'ps5t("{1} ({2} pieces)",[o.size(d),h])' ],
        [ '`Created by ${g}`', 'ps5t("Created by {1}",[g])' ],
        [ '`Created on ${new Date(h*1e3).toDateString()}`', 'ps5t("Created on {1}",[new Date(h*1e3).toDateString()])' ],
        [ '`Created by ${g} on ${new Date(h*1e3).toDateString()}`', 'ps5t("Created by {1} on {2}",[g,new Date(h*1e3).toDateString()])' ],
        [ '`Next announce in ${G.timeInterval(t)}`', 'ps5t("Next announce in {1}",[G.timeInterval(t)])' ],
        [ '`${g} - tier ${i.tier+1}`', 'ps5t("{1} - tier {2}",[g,i.tier+1])' ],
        [ '`Seeders: ${i.seeder_count>-1?i.seeder_count:"N/A"}`', 'ps5t("Seeders: {1}",[i.seeder_count>-1?i.seeder_count:ps5t("N/A")])' ],
        [ '`Leechers: ${i.leecher_count>-1?i.leecher_count:"N/A"}`', 'ps5t("Leechers: {1}",[i.leecher_count>-1?i.leecher_count:ps5t("N/A")])' ],
        [ '`Downloads: ${i.download_count>-1?i.download_count:"N/A"}`', 'ps5t("Downloads: {1}",[i.download_count>-1?i.download_count:ps5t("N/A")])' ],
        [ '`${G.size(r)} Free`', 'ps5t("{1} Free",[G.size(r)])' ],
        [ '`Error adding "${d.name}": ${B}`', 'ps5t(\'Error adding "{1}": {2}\',[d.name,B])' ],
        [ '`Error adding "${d.name}"`', 'ps5t(\'Error adding "{1}"\',[d.name])' ],
        [ '`Error adding "${m}"`', 'ps5t(\'Error adding "{1}"\',[m])' ],
        [ '`Error renaming "${r}"`', 'ps5t(\'Error renaming "{1}"\',[r])' ],
        [ '`(${U} up, ${J} down)`', 'ps5t("({1} up, {2} down)",[U,J])' ],
        [ '`Blocklist has <span class="blocklist-size-number">${o}</span> rules`', 'ps5t("Blocklist has {1} rules",[\'<span class="blocklist-size-number">\'+o+"</span>"])' ],
        [ '`Remove ${i.getName()} and delete data?`', 'ps5t("Remove {1} and delete data?",[i.getName()])' ],
        [ '`Remove ${o.length} transfers and delete data?`', 'ps5t("Remove {1} transfers and delete data?",[o.length])' ],
        [ '`Remove ${i.getName()}?`', 'ps5t("Remove {1}?",[i.getName()])' ],
        [ '`Remove ${o.length} transfers?`', 'ps5t("Remove {1} transfers?",[o.length])' ],
    );
    for my $r (@rules) {
        my ($old, $new) = @$r;
        my $n = () = $appjs =~ /\Q$old\E/g;
        die "i18n: rule matched $n time(s), exactly one needed: $old\n" unless $n == 1;
        my $at = index($appjs, $old);
        my $prev = $at > 0 ? substr($appjs, $at - 1, 1) : '';
        $new = " $new" if $prev =~ /[\w\$]/ && $new =~ /^ps5t\(/;   # return`...` -> return ps5t(...), not returnps5t(...)
        substr($appjs, $at, length($old)) = $new;
    }
}
# 2) plain texts: every occurrence of "text" is wrapped into ps5t("text"), except logical uses (===, includes, case) and already wrapped ones
{
    my @missing;
    my @static = (
        '"Close"',
        '"A fast and easy bitTorrent client"',
        '"About transmission"',
        '"Copy name"',
        '"Deselect all"',
        '"Bottom"',
        '"Down"',
        '"Top"',
        '"Open torrent\\u2026"',
        '"Pause all"',
        '"Pause"',
        '"Ask tracker for more peers"',
        '"Remove from list\\u2026"',
        '"Resume"',
        '"Resume now"',
        '"Select all"',
        '"About"',
        '"Torrent Inspector"',
        '"Edit Labels\\u2026"',
        '"Set location\\u2026"',
        '"More options\\u2026"',
        '"Edit preferences"',
        '"Rename\\u2026"',
        '"Keyboard shortcuts"',
        '"Statistics"',
        '"Start all"',
        '"Compact rows"',
        '"High contrast UI"',
        '"Trash data and remove from list\\u2026"',
        '"Verify local data"',
        '"Move in the queue"',
        '"Select operation"',
        '"Download file"',
        '"Low Priority"',
        '"Normal Priority"',
        '"High Priority"',
        '"Unknown"',
        '"None"',
        '"Today"',
        '"Yesterday"',
        '"Tomorrow"',
        '"N/A"',
        '"Activity"',
        '"Have:"',
        '"Availability:"',
        '"Uploaded:"',
        '"Downloaded:"',
        '"State:"',
        '"Running time:"',
        '"Remaining:"',
        '"Last activity:"',
        '"Error:"',
        '"Details"',
        '"Name:"',
        '"Size:"',
        '"Location:"',
        '"Hash:"',
        '"Privacy:"',
        '"Origin:"',
        '"Date added:"',
        '"Comment:"',
        '"Labels:"',
        '"Done"',
        '"Status"',
        '"Address"',
        '"Client"',
        '"Info"',
        '"Peers"',
        '"Tiers"',
        '"Files"',
        '"Mixed"',
        '"Finished"',
        '"Paused"',
        '"Active now"',
        '"Private to this tracker -- DHT and PEX disabled"',
        '"Public torrent"',
        '"Downloading from this peer"',
        '"Encrypted Connection"',
        '"Peer was discovered through Distributed Hash Table (DHT)"',
        '"Peer is an incoming connection"',
        '"Optimistic unchoke"',
        '"Peer is connected via uTP"',
        '"Uploading to peer"',
        '"Peer was discovered through Peer Exchange (PEX)"',
        '"Announce in progress"',
        '"Announce is queued"',
        '"Tracker will be used as a backup"',
        '"Announce not scheduled"',
        '"Last Announce"',
        '"Announce error"',
        '"Last Scrape"',
        '"Scrape error"',
        '"We unchoked this peer, but they\'re not interested"',
        '"Peer has unchoked us, but we\'re not interested"',
        '"We would download from this peer if they\'d let us"',
        '"We would upload to this peer if they\'d ask"',
        '"At least one selected torrent expected."',
        '"Edit Labels"',
        '"Edit Labels:"',
        '"Save"',
        '"Move Torrent"',
        '"Set Torrent Location"',
        '"Apply"',
        '"Disable Notifications"',
        '"Enable Notifications"',
                '"Seeding complete"',
                '"Seeding"',
        '"Add Torrents"',
        '"Add"',
        '"Please select torrent files to add:"',
        '"Or enter a URL:"',
        '"Start when added"',
        '"Display"',
        '"Sort by"',
        '"Age"',
        '"Name"',
        '"Progress"',
        '"Queue order"',
        '"Ratio"',
        '"Size"',
        '"State"',
        '"Reverse sort"',
        '"Fullscreen"',
        '"Speed Limit"',
        '"Upload:"',
        '"Unlimited"',
        '"Download:"',
        '"Use Temp limits"',
        '"Actions"',
        '"Help"',
        '"Donate"',
        '"Everyday"',
        '"Weekdays"',
        '"Weekends"',
        '"Sunday"',
        '"Monday"',
        '"Tuesday"',
        '"Wednesday"',
        '"Thursday"',
        '"Friday"',
        '"Saturday"',
        '"Checking..."',
        '"Closed"',
        '"Error"',
        '"Update"',
        '"Updating..."',
        '"Remove Browser Handler"',
        '"Your browser does not support removing protocol handlers. This button only allows you to re-register a handler."',
        '"Add Browser Handler"',
        '"Your browser does not support protocol handlers"',
        '"Transmission Web"',
        '"Downloading"',
        '"Download to:"',
        '"Use temporary folder:"',
        '"Separate folder to temporarily store downloads until they are complete."',
        '"Download queue size:"',
        '"Stop seeding at ratio:"',
        '"Stop seeding if idle for N mins:"',
        '"Magnet Protocol Handler"',
        '"Speed Limits"',
        '"Upload (kB/s):"',
        '"Download (kB/s):"',
        '"Alternative Speed Limits"',
        '"Override normal speed limits manually or at scheduled times"',
        '"Scheduled times"',
        '"From:"',
        '"To:"',
        '"On days:"',
        '"Connections"',
        '"Max peers per torrent:"',
        '"Max peers overall:"',
        '"Options"',
        '"Encryption mode:"',
        '"Prefer encryption"',
        '"Allow encryption"',
        '"Require encryption"',
        '"Use PEX to find more peers"',
        '"Use DHT to find more peers"',
        '"DHT is a tool for finding peers without a tracker."',
        '"Use LPD to find more peers"',
        '"LPD is a tool for finding peers on your local network."',
        '"PEX is a tool for exchanging peer lists with the peers you\'re connected to."',
        '"Blocklist"',
        '"Enable blocklist:"',
        '"Blocklist has {n} rules"',
        '"Listening Port"',
        '"Peer listening port:"',
        '"IPv4 port is"',
        '"IPv6 port is"',
        '"Randomize port on launch"',
        '"Use port forwarding from my router"',
        '"Enable uTP for peer communication"',
        '"Default Public Trackers"',
        '"Trackers to use on all public torrents."',
        '"To add a backup URL, add it on the next line after a primary URL."',
        '"To add a new primary URL, add it after a blank line."',
        '"Torrents"',
        '"Speed"',
        '"Network"',
        '"Updating blocklist..."',
        '"Connection failed"',
        '"Could not connect to the server. You may need to reload the page to reconnect."',
        '"Remove"',
        '"All data downloaded for this torrent will be deleted. Are you sure you want to remove it?"',
        '"All data downloaded for these torrents will be deleted. Are you sure you want to remove them?"',
        '"Once removed, continuing the transfer will require the torrent file. Are you sure you want to remove it?"',
        '"Once removed, continuing the transfers will require the torrent files. Are you sure you want to remove them?"',
        '"Rename Torrent"',
        '"Enter new name:"',
        '"Rename"',
        '"Keyboard Shortcuts"',
        '"Key"',
        '"Action"',
        '"Ratio:"',
        '"Current session"',
        '"Total"',
        '"Idle"',
        '"Queued for verification"',
        '"Verifying local data"',
        '"Queued for download"',
        '"Queued for seeding"',
        '"All"',
        '"Active"',
        '"Private torrents"',
        '"Public torrents"',
        '"Select all then copy"',
        '"Cancel"',
                '"remaining time unknown"',
        '"Downloading from"',
        '"Seeding to"',
    );
    for my $t (@static) {
        my $q = $t;   # already in double quotes, as in the client code
        my $cnt = 0;
        $appjs =~ s{(?<=[\w\$])(?<!===)(?<!==)(?<!!=)(?<!\.includes\()(?<!case)(?<!ps5t\()\Q$q\E}{ $cnt++; " ps5t($q)" }ge;   # return"x" -> return ps5t("x")
        $appjs =~ s{(?<![\w\$])(?<!===)(?<!==)(?<!!=)(?<!\.includes\()(?<!ps5t\()\Q$q\E}{ $cnt++; "ps5t($q)" }ge;
        push @missing, $t unless $cnt;
    }
    print "i18n: not found in the client as plain text (" . scalar(@missing) . "): " . join(" | ", @missing) . "\n" if @missing;
}
# ---- end of the translation block ----
spit("$dst/transmission-app.js", $appjs);
my $html = slurp("$dst/index.html");
$html =~ s{(<script\b[^>]*\bsrc="(?:\./)?transmission-app\.js")}{<script src="ps5-lang.js"></script>\n    <script src="ps5-i18n.js"></script>\n    <script src="ps5-compat.js"></script>\n    $1} or die "index.html does not include transmission-app.js\n";
$html =~ s{[ \t]*<link href="\./images/favicon\.ico"[^>]*/>\n}{} or die "index.html has no favicon.ico\n";
$html =~ s{<link href="\./images/favicon\.svg"[^>]*/>}{<link href="./images/favicon.png" rel="icon" type="image/png" sizes="64x64" />} or die "index.html has no favicon.svg\n";
$html =~ s{(<script type="text/javascript" src="\./transmission-app\.js"></script>)}{$1\n    <script src="ps5-config.js"></script>\n    <script src="ps5-panel.js"></script>} or die "index.html does not include the panel application\n";
$html =~ s{(<link href="\./transmission-app\.css" rel="stylesheet" />)}{$1\n    <link href="./ps5-theme.css" rel="stylesheet" />\n    <link href="./ps5-icons.css" rel="stylesheet" />} or die "index.html does not include the css\n";
# 3) index.html: texts and attributes get data-i18n (ps5-i18n.js translates them when the page loads)
{
    my @text = ('Drag your torrent files here to add them to Transmission.', 'Inspector', 'Delete', 'Open', 'Show', 'Start', 'Stop', 'Transmission Web Interface');
    my @attr = ('Filter', 'Open torrent', 'Pause selected torrents', 'Remove selected torrents', 'Resume selected torrents', 'Show sidebar menu', 'Toggle inspector');
    for my $t (@text) {
        my $a = $t; $a =~ s/&/&amp;/g; $a =~ s/"/&quot;/g;
        ($html =~ s{>(\s*)\Q$t\E(\s*)<}{ data-i18n="$a">$1$t$2<}) or die "i18n: no such text in index.html: $t\n";
    }
    for my $t (@attr) {
        my $a = $t; $a =~ s/&/&amp;/g; $a =~ s/"/&quot;/g;
        my $done = 0;
        for my $name ('aria-label', 'placeholder', 'title') {
            $done += ($html =~ s{($name="\Q$a\E")}{$1 data-i18n-$name="$a"}g);
        }
        die "i18n: no attribute with this text in index.html: $t\n" unless $done;
    }
}
spit("$dst/index.html", $html);
print "web_patch: ok (" . length($light) . " bytes of the default theme)\n";
