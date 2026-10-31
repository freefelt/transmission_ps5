var LANG_INFO=@@LANG_INFO@@;
function langErr(c){var p=String(c).split(':'),k=p[0],a=p.slice(1).join(':'),E={read:'could not read the file',size:'the file is too large',utf8:'the file is not UTF-8',syntax:'error in line ',first:'the first entry must be the LanguageName line with the language name',name:'invalid language name',dup:'duplicate key ',chars:'invalid characters in line ',empty:'the file has no known strings',notfound:'file not found',bad:'invalid file name'};return (E[k]||k)+((k==='syntax'||k==='chars'||k==='dup')?a:'')}
function langNotice(){var t='';if(LANG_INFO.fallback)t+=' The saved language “'+LANG_INFO.fallback+'” is unavailable ('+langErr(LANG_INFO.reason)+'): the default language was switched on.';LANG_INFO.skipped.forEach(function(x){t+=' Skipped file '+x[0]+': '+langErr(x[1])+'.'});return t}
var SCHEMA=[
 {h:'Language / Язык'},
 {k:'language',t:'sel',l:'Interface language',d:'Russian and English are built in; other languages come from .strings files in the locales folder (/data/Transmission/locales). The list is read when the page opens. After saving, the page reloads by itself.'+langNotice(),o:@@LANG_OPTS@@},
 {h:'Notifications'},
 {k:'notify_startup',t:'bool',l:'Start, stop and restart of Transmission'},
 {k:'notify_torrent_started',t:'bool',l:'Torrent started'},
 {k:'notify_torrent_finished',t:'bool',l:'Torrent finished'},
 {k:'notify_torrent_error',t:'bool',l:'Torrent error',d:'No space, no access, disk error.'},
 {k:'notify_tracker_error',t:'bool',l:'Tracker error',d:'Often happens and is temporary.'},
 {k:'notify_low_disk_space',t:'bool',l:'Low disk space'},
 {k:'low_disk_warn_gb',t:'int',l:'“Low space”: threshold (GB)',d:'A notification when the free space on the disk is below this (1–100000).',m:['The notification is also sent when the free space is less than what is left to download.']},
 {k:'low_disk_critical_gb',t:'int',l:'“Disk almost full”: threshold (GB)',d:'A second, more urgent notification. Smaller than the first; 0 = do not send.'},
 {k:'notify_game_pause',t:'bool',l:'Pausing and resuming downloads during a game',d:'A console message when downloads are paused or resumed because of a game.',m:['For example: “Game on screen: downloads paused: N”. Works together with the “Pause during a game” setting.','These events always go to the log (the bell).']},
 {h:'Games'},
 {k:'game_pause',t:'sel',l:'Pause during a game',d:'Active downloads and seeds are paused while a game is running and resumed after it.',m:['“When the game is on screen”: paused while the game is on screen; if it is minimised, downloading continues. “When the game is running”: paused also while minimised, until the game is closed.','The manager sets the pause: torrents you stopped yourself are not started by it. A torrent started or added during a game stays as it is.','If the game state cannot be determined, no pause is set, and a pause that is already set is lifted after a minute.','Downloads resume 30 seconds after the game is closed (or minimised, depending on the setting), so that they do not jump back and forth when you visit the settings or start another game.'],o:[['off','Do not pause'],['screen','When the game is on screen'],['running','When the game is running']]},
 {h:'Transmission web interface (applies after the page is refreshed)'},
 {k:'web_openfolder',t:'sel',l:'“Open folder” item',d:'Opens the folder of a torrent in a file manager from its menu.',m:['The item appears in the torrent context menu (right click or long press) on the Transmission page and opens the folder of that torrent in BFpilot (port 5905) or PS5 Web File Manager (port 8888).','In a desktop browser the page opens in a new tab, on the console in the same window.'],o:[['off','Do not show'],['bfpilot','BFpilot'],['webfm','PS5 Web File Manager']]},
 {k:'web_diskbar',t:'sel',l:'Show disk status',d:'A panel above the download list.',m:['“Show used”: disks that hold torrents and downloads. “Show active”: only disks that have running (not stopped) torrents.'],o:[['off','Do not show'],['all','Show used'],['active','Show active']]},
 {k:'web_icons',t:'sel',l:'Icons',d:'Toolbar buttons, menus and torrent icons.',o:[['color','Color'],['mono','Monochrome']]},
 {k:'web_theme',t:'sel',l:'Theme',o:[['dark','Dark'],['light','Light']]}
];
var EXPERT_SCHEMA=[
 {h:'Ports'},
 {k:'settings_port',t:'int',l:'Port of this page',d:'1024–65535. If the port is busy, the next free one is used.',m:['The first free port starting from the one given is used; the address arrives in a notification. After saving, the page opens on the new port.']},
 {k:'daemon_port',t:'int',l:'Port of the Transmission web interface',d:'1024–65535. Changing it restarts Transmission: torrents are paused and resumed.'},
 {h:'Delays (seconds)'},
 {k:'wake_delay_seconds',t:'int',l:'Pause after waking from sleep',d:'Wait after the console wakes up before starting Transmission (0–300).'},
 {k:'poll_interval_seconds',t:'int',l:'Daemon polling period',d:'Torrent notifications arrive with this delay (1–600).'},
 {h:'Rest mode'},
 {k:'sleep_method',t:'sel',l:'Rest mode method',d:'How the console goes to rest mode.',m:['“System request”: the console goes to sleep the same way as if you chose it in the menu yourself (with an animation); if the system request is not available, the direct method is used.','“Direct”: the system state flag, as in ps5-linux-loader, the screen just goes dark.'],o:[['native','System request (recommended)'],['direct','Direct (system flag)']]},
 {h:'Log retention'},
 {k:'notifications_keep_days',t:'int',l:'Notifications (bell), days',d:'Entries older than this are deleted. 1–365.'},
 {k:'logs_keep_days',t:'int',l:'Debug logs, days',d:'The logs of the manager, daemon and Transmission, kept by day (1–90).'},
 {h:'Transmission (daemon)'},
 {k:'daemon_restart_after_seconds',t:'int',l:'Restart Transmission if it does not respond (s)',d:'Only when the network is working. 0 = do not restart (0–3600).'},
 {k:'daemon_network_restart_seconds',t:'int',l:'Restart Transmission after a network change (s)',d:'If Transmission does not respond after the network changed (0 = do not restart, 0–3600).',m:['A network change is: the network dropped and came back, the IP address changed or the console woke from rest mode.','If Transmission does not respond on the console’s current address, it is restarted after this many seconds (instead of waiting for the time above). If it responds, nothing is done.']},
 {k:'daemon_stop_grace_seconds',t:'int',l:'Wait for a graceful shutdown (s)',d:'How long to wait for Transmission to close before it is terminated by force (5–120).'}
];
var ALL=SCHEMA.concat(EXPERT_SCHEMA);
var SLEEP_SCHEMA=[
 {k:'autosleep_action',t:'sel',l:'Action',d:'What to do when all downloads are finished (or the timeout expired): go to rest mode or turn the console off.',o:[['sleep','Go to rest mode'],['poweroff','Turn the console off']]},
 {k:'autosleep_timeout_minutes',t:'int',l:'Timeout (minutes)',d:'The action is performed after this time in any case (1–1440).',m:['The maximum time: after it, following the activation, the action is performed even if the downloads have not finished (for example, no seeders).']},
 {k:'sleep_notify_before',t:'bool',l:'Show a notification with a delay before the action',d:'Off: the action happens at once, without a notification.'},
 {k:'sleep_countdown_seconds',t:'int',l:'Delay (seconds)',d:'0 = at once. You can cancel on this page during the delay.'},
 {k:'keep_awake',t:'bool',l:'Do not let the console fall asleep by itself',d:'Keeps the console awake while an action is activated.',m:['Works only after you press Activate (auto sleep or auto power off): while it is active, Transmission tells the system every 10 minutes that the console is in use, so it does not fall asleep on its own timer. Without activation the system sleeps as usual.','The console settings may specify an automatic sleep time (at least 20 minutes), and without this option it can fire before everything is downloaded.']}
];
var state=null,formBuilt=false,orig={};
function $(id){return document.getElementById(id)}
function api(path,body,cb){var x=new XMLHttpRequest();x.open(body?'POST':'GET',path);if(body)x.setRequestHeader('Content-Type','application/json');
 x.onload=function(){var j=null;try{j=JSON.parse(x.responseText)}catch(e){}cb(x.status,j)};x.onerror=function(){cb(0,null)};x.send(body?JSON.stringify(body):null)}
function buildFields(f,schema,c,pre){f.innerHTML='';
 schema.forEach(function(s){
  if(s.h){var h=document.createElement('div');h.className='help';h.style.cssText='margin:10px 0 2px;font-weight:600;color:#8fb7ff';h.textContent=s.h;f.appendChild(h);return}
  var r=document.createElement('div');r.className='row';r.id=pre+'row_'+s.k;var l=document.createElement('label');l.setAttribute('for',pre+s.k);l.textContent=s.l;
  if(s.m)helpBtn(l,s.l,s.m);
  if(s.d){var d=document.createElement('span');d.className='help';d.textContent=s.d;l.appendChild(d)}
  var i=s.t==='sel'?document.createElement('select'):document.createElement('input');i.id=pre+s.k;
  if(s.t==='sel'){s.o.forEach(function(o){var op=document.createElement('option');op.value=o[0];op.textContent=o[1];i.appendChild(op)});i.value=c[s.k]}
  else if(s.t==='bool'){i.type='checkbox';i.checked=!!c[s.k]}else if(s.t==='text'){i.type='text';i.value=c[s.k];i.style.width='240px'}else{i.type='number';i.value=c[s.k]}
  i.addEventListener(s.t==='bool'||s.t==='sel'?'change':'input',refreshButtons);
  r.appendChild(l);r.appendChild(i);f.appendChild(r);orig[s.k]=c[s.k]})}
helpBtn($('asHead'),'When all downloads finish',['If all downloads are paused or removed, the action deactivates itself and the console does nothing.','The activation is not saved: after sleep, power off or a manager restart it is off. If the console woke up (including when it went to sleep by itself on its own timer), the activation is cleared as well: turn it on again.']);
helpBtn($('iconHead'),'Icon on the “Media” tab',['The app registers a separate short process, transmission_i.elf, with the system (the Payload Manager method): if it hangs, the manager and Transmission are not affected, and there will be no automatic retries.','Installation takes a few seconds.']);
helpBtn($('dgInfo'),'Diagnostics',['“Check Transmission” shows whether it accepts a local connection and responds.','“Threads”: where the process threads stand (the wait column shows what a thread is waiting for).','“Manager log”: the memory of the running manager. “Manager log file”: shim.log from the disk; it also has the starts of other instances of the manager, for example a new one that could not replace the old one.','“Daemon log”, “Transmission log”: the last lines of the logs.','“Drives”: which of /mnt/ext*, /mnt/usb* are connected (press with the drive and without it).','“Mount points”: all mount points the manager sees (USB and M.2 first).']);
function diskClamp(){var w=$('f_low_disk_warn_gb'),c=$('f_low_disk_critical_gb');if(!w||!c)return;
 var wv=parseInt(w.value,10),cv=parseInt(c.value,10);if(isNaN(wv))return;
 var top=Math.max(0,wv-1);c.min=0;c.max=top;w.min=1;
 if(!isNaN(cv)&&cv>top){c.value=top;refreshButtons()}}
function diskClampInit(){var w=$('f_low_disk_warn_gb'),c=$('f_low_disk_critical_gb');if(!w||!c)return;
 c.addEventListener('input',diskClamp);w.addEventListener('change',diskClamp);
 w.addEventListener('input',function(){var wv=parseInt(w.value,10);if(!isNaN(wv))c.max=Math.max(0,wv-1)});diskClamp()}
/* Smooth show and hide of a section: height, paddings and opacity over PS5_ANIM_MS ms (0 = no animation, tests need that). The first 2 s after the page
 loads (state restore) are without animation. The state is remembered in el._ps5v; a repeated call with the same state does nothing. */
if(window.PS5_ANIM_MS===undefined)window.PS5_ANIM_MS=200;
var animReady=false;setTimeout(function(){animReady=true},2000);
function isShown(el){return el._ps5v!==undefined?el._ps5v:(el.style.display!=='none')}
function slideShow(el,show,disp){
 if(!el)return;var d=disp||'',ms=window.PS5_ANIM_MS|0;show=!!show;
 var vis=el._ps5v!==undefined?el._ps5v:(window.getComputedStyle(el).display!=='none');
 if(vis===show){el._ps5v=show;return}
 el._ps5v=show;var seq=el._ps5seq=(el._ps5seq|0)+1;clearTimeout(el._ps5t);
 var P=['height','paddingTop','paddingBottom','marginTop','marginBottom','opacity','overflow','transition','boxSizing'];
 function reset(){P.forEach(function(p){el.style[p]=''})}
 if(!ms||!animReady){el.style.display=show?d:'none';reset();return}
 var tr='height '+ms+'ms ease,padding '+ms+'ms ease,margin '+ms+'ms ease,opacity '+ms+'ms ease';
 if(show){
  el.style.display=d;var cs=window.getComputedStyle(el),full=el.offsetHeight,pt=cs.paddingTop,pb=cs.paddingBottom,mt=cs.marginTop,mb=cs.marginBottom;
  el.style.boxSizing='border-box';el.style.overflow='hidden';el.style.height='0px';el.style.paddingTop='0px';el.style.paddingBottom='0px';el.style.marginTop='0px';el.style.marginBottom='0px';el.style.opacity='0';
  void el.offsetHeight;el.style.transition=tr;
  el.style.height=full+'px';el.style.paddingTop=pt;el.style.paddingBottom=pb;el.style.marginTop=mt;el.style.marginBottom=mb;el.style.opacity='1';
  el._ps5t=setTimeout(function(){if(el._ps5seq===seq)reset()},ms+40)
 }else{
  var h=el.offsetHeight;el.style.boxSizing='border-box';el.style.overflow='hidden';el.style.height=h+'px';void el.offsetHeight;el.style.transition=tr;
  el.style.height='0px';el.style.paddingTop='0px';el.style.paddingBottom='0px';el.style.marginTop='0px';el.style.marginBottom='0px';el.style.opacity='0';
  el._ps5t=setTimeout(function(){if(el._ps5seq===seq){el.style.display='none';reset()}},ms+40)
 }
}
/* Smooth appearance of pop-up elements (notification bell panel, warning dialogs): opacity and a small shift over PS5_ANIM_MS ms. Appearance only; 0 = no animation. */
document.documentElement.style.setProperty('--ps5-ms',(window.PS5_ANIM_MS|0)+'ms');
function popClose(el){
 var ms=window.PS5_ANIM_MS|0;
 if(!ms||!animReady||el.style.display==='none'||el.style.display===''){el.style.display='none';return}
 clearTimeout(el._ps5p);el.classList.remove('ps5in');el.classList.add('ps5out');
 clearTimeout(el._ps5c);el._ps5c=setTimeout(function(){el.style.display='none';el.classList.remove('ps5out')},ms+40)
}
function popOpen(el,disp){
 clearTimeout(el._ps5c);el.classList.remove('ps5out');
 el.style.display=disp;var ms=window.PS5_ANIM_MS|0;if(!ms||!animReady)return;
 el.classList.remove('ps5in');void el.offsetWidth;el.classList.add('ps5in');clearTimeout(el._ps5p);el._ps5p=setTimeout(function(){el.classList.remove('ps5in')},ms+60)
}
function diskDeps(){var n=$('f_notify_low_disk_space');if(!n)return;['f_row_low_disk_warn_gb','f_row_low_disk_critical_gb'].forEach(function(id){var r=$(id);if(r){r.classList.add('sub');slideShow(r,n.checked,'')}})}
function sleepDeps(){var n=$('sf_sleep_notify_before'),d=$('sf_sleep_countdown_seconds'),r=$('sf_row_sleep_countdown_seconds');
 if(n&&d){d.disabled=!n.checked;r.classList.add('sub');slideShow(r,n.checked,'')}}
var msgUntil=0;
function valOf(s,pre){var e=$(pre+s.k);if(!e)return orig[s.k];var v=s.t==='bool'?e.checked:((s.t==='text'||s.t==='sel')?e.value:parseInt(e.value,10));if(s.t==='int'&&isNaN(v))v=e.value;return v}
function dirtyCount(schema,pre){var n=0;schema.forEach(function(s){if(s.k&&valOf(s,pre)!==orig[s.k])n++});return n}
function refreshButtons(){if(!formBuilt)return;var m=dirtyCount(ALL,'f_')+dirtyCount(SLEEP_SCHEMA,'sf_'),showMsg=Date.now()<msgUntil;
 $('saveBar').style.display=(m>0||showMsg)?'flex':'none';$('save').disabled=!m;$('reset').style.display=m?'inline-block':'none';$('saveMsg').style.display=showMsg?'inline':'none';
 if(state)sleepRender(state)}
function resetSchema(schema,pre){schema.forEach(function(s){if(!s.k)return;var e=$(pre+s.k);if(s.t==='bool')e.checked=!!orig[s.k];else e.value=orig[s.k]});sleepDeps();diskDeps();diskClamp();refreshButtons()}
function buildForm(c){buildFields($('form'),SCHEMA,c,'f_');buildFields($('expertForm'),EXPERT_SCHEMA,c,'f_');buildFields($('sleepForm'),SLEEP_SCHEMA,c,'sf_');
 $('sf_sleep_notify_before').addEventListener('change',sleepDeps);sleepDeps();
 $('f_notify_low_disk_space').addEventListener('change',diskDeps);diskDeps();diskClampInit();formBuilt=true;refreshButtons()}
function saveAll(){var ch={},n=0;[[ALL,'f_'],[SLEEP_SCHEMA,'sf_']].forEach(function(p){p[0].forEach(function(s){if(!s.k)return;var v=valOf(s,p[1]);if(v!==orig[s.k]){ch[s.k]=v;n++}})});
 if(!n)return;var m=$('saveMsg');
 api('/api/config',ch,function(c,j){if(c===200&&j&&j.ok){m.className='msg';var t=j.saved?'Saved':'Applied, but config.cfg was not written';
   if(ch.daemon_port)t+='. Transmission is restarting with the new port.';
   if(ch.settings_port)t+='. The page will open on the first free port starting from '+ch.settings_port+' (the address will arrive in a notification).';
   m.textContent=t;for(var k in ch)orig[k]=ch[k];msgUntil=Date.now()+4000;refreshButtons();setTimeout(refreshButtons,4100);
   if(ch.language){setTimeout(function(){location.reload()},900)}
    if(ch.settings_port){setTimeout(function(){location.port=ch.settings_port},3000)}}
  else{m.className='msg err';m.textContent=(j&&j.errors?j.errors.map(function(e){return e.indexOf('LANGERR:')===0?langErr(e.slice(8)):e}).join('; '):'Connection error');msgUntil=Date.now()+10000;refreshButtons();setTimeout(refreshButtons,10100)}})}

function kv(a,b){return '<div class="kv"><span>'+a+'</span><span>'+b+'</span></div>'}
function fmt(t){var h=Math.floor(t/3600),m=Math.floor(t%3600/60);return h+' h '+m+' min'}
var DM={running:'Running',stopped:'Stopped',starting:'starting…',stopping:'stopping…',not_responding:'busy, not responding',restarting:'restarting…'};
function fmtMem(k){var m=k/1024;return m>=1024?(m/1024).toFixed(2)+' GiB':m.toFixed(1)+' MiB'}
function dmRender(d){var st=DM[d.state]||d.state;if(d.state==='running'&&d.pid)st+=' (pid: '+d.pid+(d.mem_kib>0?', memory: '+fmtMem(d.mem_kib):'')+')';if((d.state==='starting'||d.state==='restarting'||d.state==='stopping')&&d.since_age>=0)st+=' '+d.since_age+' s';if(d.state==='stopped'&&!d.desired_running)st='stopped manually';
 if(d.state==='not_responding'&&d.unresponsive_for>=0)st+=' '+d.unresponsive_for+' s';
 $('dmState').textContent=st;$('trLink').style.display=d.state==='running'?'inline':'none';var b=$('dmBtn'),run=(d.state==='running'||d.state==='not_responding');
 b.textContent=run?'Stop Transmission':'Start Transmission';b.className=run?'danger':'';b.disabled=!!d.busy;
 var n=d.state==='stopped'&&!d.desired_running?'Stopped with the button: it will not start by itself until you start it.':(d.busy?'Please wait: an operation is in progress (starting may take up to a minute).':'');
 if(d.last_error&&d.state!=='running')n=(n?n+' ':'')+(d.busy?'The previous attempt failed: ':'The last start failed: ')+d.last_error+(d.busy?'':'. If starting does not work, restart Transmission.elf through Payload Manager.');
 if(d.state==='not_responding'){var bn='Transmission is busy: usually long disk work (for example, writing large files to a USB drive).';
  if(d.stuck)bn+=' The process cannot be stopped: it is probably waiting for the disk. If the console has frozen, disconnect the USB drive.';
  else if(d.restart_in>=0)bn+=' If there is no answer, a forced restart in '+d.restart_in+' s.';
  n=(n?n+' ':'')+bn}
 $('dmNote').textContent=n}
/* Shared confirmation and message dialog instead of the system confirm() and alert() */
var cfmCb=null;
function cfmAsk(title,text,okLabel,cb,danger){$('cfmTitle').textContent=title;$('cfmText').textContent=text;$('cfmOk').textContent=okLabel;$('cfmOk').className=danger===false?'':'danger';$('cfmCancel').style.display='';cfmCb=cb;popOpen($('cfmModal'),'flex');$('cfmCancel').focus()}
function cfmInfo(title,text){cfmAsk(title,text,'OK',null,false);$('cfmCancel').style.display='none';$('cfmOk').focus()}
$('cfmCancel').onclick=function(){popClose($('cfmModal'));cfmCb=null};
$('cfmOk').onclick=function(){var cb=cfmCb;popClose($('cfmModal'));cfmCb=null;if(cb)cb()};
function dmDo(run){var m=$('dmMsg');m.className='msg';m.textContent='…';api('/api/daemon',{action:run?'stop':'start'},function(c,j){if(c===200){m.textContent=''}else{m.className='msg err';m.textContent=(j&&j.message)||'Error'}refresh()})}
$('dmBtn').onclick=function(){var run=(state&&(state.daemon.state==='running'||state.daemon.state==='not_responding'));
 if(run){cfmAsk('Stop Transmission?','It will receive the termination signal: active torrents will be paused and resumed at the next start, and the ones you paused yourself will stay paused.','Stop',function(){dmDo(true)});return}
 dmDo(false)};
function ssRender(st){var ss=st&&st.syssvc,e=$('ssState');if(!ss){e.textContent='—';return}
if(ss.hung){e.textContent='hung: '+ss.error;return}if(ss.disabled){e.textContent='off: '+ss.error;return}
if(!ss.ready){e.textContent='loading or not available'+(ss.error?': '+ss.error:'');return}
e.textContent='running; game: '+(ss.game_state==='none'?'None':ss.game_state==='on_screen'?'on screen':ss.game_state==='minimised'?'running but minimised':'Launched')+(ss.title?' ('+ss.title+')':'')+'; console language: '+(ss.system_lang<0?'Unknown':(ss.system_lang===8?'Russian':'code '+ss.system_lang))+'; keep awake: '+(ss.keepawake?'On':'Off')}
/* A newer release: the browser (not the console) asks GitHub once in a few hours, only to show a link next to the version. Nothing is sent from the console, nothing is
   installed. Automated browsers (jsdom, headless Chrome) do not ask unless a test sets window.PS5_UPDATE_URL. */
var UPD_URL=window.PS5_UPDATE_URL||'https://api.github.com/repos/freefelt/transmission_ps5/releases/latest',UPD_PAGE='https://github.com/freefelt/transmission_ps5/releases/latest',updTag='';
function verParts(v){return String(v).replace(/^v/i,'').split('.').map(function(x){return parseInt(x,10)||0})}
function verNewer(a,b){var x=verParts(a),y=verParts(b);for(var i=0;i<Math.max(x.length,y.length);i++){var p=x[i]||0,q=y[i]||0;if(p!==q)return p>q}return false}
function updStore(tag){updTag=tag;try{localStorage.setItem('ps5tm_upd',JSON.stringify({t:Date.now(),tag:tag}))}catch(e){}}
function updCheck(){if(!window.PS5_UPDATE_URL&&/jsdom|HeadlessChrome/i.test(navigator.userAgent||''))return;
 try{var c=JSON.parse(localStorage.getItem('ps5tm_upd')||'null');if(c&&c.t<=Date.now()&&Date.now()-c.t<(c.tag?21600000:3600000)){updTag=c.tag||'';return}}catch(e){}
 try{var x=new XMLHttpRequest();x.open('GET',UPD_URL);x.timeout=10000;
  x.onload=function(){var tag='';if(x.status===200){try{tag=String(JSON.parse(x.responseText).tag_name||'').replace(/^v/i,'')}catch(e){}}updStore(/^\d+(\.\d+)*$/.test(tag)?tag:'')};
  x.onerror=x.ontimeout=function(){updStore('')};x.send()}catch(e){}}
function topRender(s){var upd=updTag&&verNewer(updTag,s.version)?updTag:'',key=s.version+'|'+s.tr_version+'|'+s.ip+'|'+upd,t=$('top');if(t._key===key)return;t._key=key;t.textContent='';
 t.appendChild(document.createTextNode('Version '+s.version));
 if(upd){t.appendChild(document.createTextNode(' ('));var a=document.createElement('a');a.href=UPD_PAGE;a.target='_blank';a.rel='noopener';a.textContent='update available'+' '+upd;t.appendChild(a);t.appendChild(document.createTextNode(')'))}
 t.appendChild(document.createTextNode(' · Transmission '+s.tr_version+' · console address '+s.ip))}
updCheck(); // after the definitions above: it reads UPD_URL
function render(s){state=s;ssRender(s.stats);$('fdCard').classList.toggle('fd-mono',s.config.web_icons==='mono');$('fdCard').classList.toggle('fd-open-on',s.config.web_openfolder==='bfpilot'||s.config.web_openfolder==='webfm');if(!formBuilt)buildForm(s.config);dmRender(s.daemon);notifState(s.notif_latest); var tr='http://'+location.hostname+':'+s.rpc_port+'/';
 topRender(s);$('bdState').textContent='SDK '+s.sdk+' · built '+s.built;
 $('trLink').href=tr;if(/PlayStation/i.test(navigator.userAgent||''))$('trLink').removeAttribute('target');else $('trLink').target='ps5tm-transmission-web'; /* a named tab: the next click goes to the same tab instead of opening a new one (no noopener: it would always open a new one) */
 var st=s.stats,t='',run=s.daemon.state==='running';
 t+=kv('Manager is running',fmt(s.uptime));
 if(!run||!st.rpc_ok||st.last_poll_age<0)t+=kv('Torrents','-')+kv('Speed','-');
 else t+=kv('Torrents',torrentsLine(st))+kv('Speed',speedLine(st))+kv('Last poll',st.last_poll_age+' s ago');
 $('status').innerHTML=t;
 if($('iconState')){var ic=s.icon;$('iconState').textContent=ic.busy?'in progress…':(ic.installed?'Installed':'not installed');
  $('iconInstall').disabled=!!ic.busy;$('iconRemove').disabled=!!ic.busy;var im=$('iconMsg');
  if(ic.error&&!ic.busy){im.className='msg err';im.textContent=ic.error}else if(!ic.busy&&im.className.indexOf('err')>=0){im.className='msg';im.textContent=''}}
 sleepRender(s)}
function hm(t){var h=Math.floor(t/3600),m=Math.ceil(t%3600/60);if(m===60){h++;m=0}return h>0?(h+' h '+m+' min'):(m+' min')}
var asLastArmed=null;
function torrentsLine(st){var p=[];if(st.downloading>0)p.push('downloading '+st.downloading);if(st.seeding>0)p.push('seeding '+st.seeding);if(st.paused>0)p.push('paused '+st.paused+(st.game_pause_active?' (game running)':''));if(!p.length)p.push(st.total>0?'none active':'no torrents');return p.join(', ')}
function fmtRate(b){var u=['kB/s','MB/s','GB/s'],v=b/1000,i=0;while(v>=1000&&i<2){v/=1000;i++}return (v===0?'0':v>=100?String(Math.round(v)):v>=10?v.toFixed(1):v.toFixed(2))+' '+u[i]}
function speedLine(st){return '▼ '+fmtRate(st.down_bps||0)+' ▲ '+fmtRate(st.up_bps||0)}
function sleepRender(s){var a=s.autosleep,sl=s.sleep,busy=sl.phase!=='idle';
 if(asLastArmed!==a.armed){var first=asLastArmed===null;asLastArmed=a.armed;
  if(a.armed||first){$('asArm').checked=!!a.armed;slideShow($('asBox'),a.armed,'block')}} // the checkbox and the section adjust on page load and on activation; after deactivation they stay as they are (the section is hidden when the page is refreshed)
 $('asArm').disabled=!!a.armed; // the checkbox only opens the section; while auto-sleep is activated, the button turns it off
 $('asNote').textContent=a.armed?'Activated. To turn it off, press “Deactivate”.':'The checkbox opens the settings; the “Activate” button turns it on.';
 var dirty=dirtyCount(SLEEP_SCHEMA,'sf_')+dirtyCount(EXPERT_SCHEMA.filter(function(s){return s.k==='sleep_method'}),'f_')>0,b=$('asBtn');
 b.textContent=a.armed?'Deactivate':'Activate';b.className=a.armed?'danger':'ok';b.disabled=busy||(!a.armed&&(!a.can_arm||dirty));
 $('asHint').textContent=a.armed?('Active downloads now: '+a.downloading+'. Time left until the forced action: '+hm(a.timeout_remaining)+'.'):(!a.can_arm?'There are no active downloads: it cannot be activated.':(dirty?'First save the changed settings (the “Save” button at the top right).':'Active downloads: '+a.downloading+'.'));
 $('sleepCount').style.display=busy?'block':'none';
 var po=!!(s.config&&s.config.autosleep_action==='poweroff'&&!sl.manual);
  $('sleepLine').textContent=sl.phase==='countdown'?((po?'Power off in ':'Sleep in ')+sl.remaining+' s.'):(sl.phase==='stopping'?(po?'Stopping Transmission before power off…':'Stopping Transmission before sleep…'):(po?'Turning the console off…':'Going to sleep…'));
 $('sleepCancel').style.display=sl.phase==='countdown'?'inline-block':'none';
}
var quitting=false;
$('quitBtn').onclick=function(ev){ev.stopPropagation();popOpen($('quitModal'),'flex');$('quitCancel').focus()};
$('quitCancel').onclick=function(){popClose($('quitModal'))};
$('quitConfirm').onclick=function(){popClose($('quitModal'));
 api('/api/quit',{},function(c,j){if(c===200&&j&&j.ok){quitting=true;popOpen($('quitDone'),'flex')}else{cfmInfo('Could not exit',(j&&j.message)||'Could not start the exit')}})};
$('asArm').onchange=function(){slideShow($('asBox'),this.checked,'block')};
/* Sleep and power-off warnings are shown in a dialog when “Activate” is pressed (they used to sit on the page) */
var ACT_SLEEP='These warnings are not a formality. Putting the console into rest mode from a payload was not tested on all firmware versions and uses an undocumented method.';
var ACT_KSTUFF='kstuff is running now. According to the ps5-linux-loader description, with etaHEN/kstuff enabled the console may hang instead of sleeping.';
var ACT_OFF='Power off: the console is turned off by the same system call as in PS5-Power-Payloads-Project (a reboot call with the HALT and POWEROFF flags), without the normal system shutdown procedure. Before that Transmission is stopped cleanly (files are closed), and the kernel itself flushes the disk buffers at power off, but other applications and system services will not have time to close. If your data matters (disk writes, game saves), use rest mode or turn the console off normally.';
function actPoweroff(){return !!(state&&state.config&&state.config.autosleep_action==='poweroff')}
function actOpen(){var po=actPoweroff(),body=$('actBody');body.innerHTML='';
 function p(t){var e=document.createElement('p');e.textContent=t;body.appendChild(e)}
 $('actTitle').textContent=po?'Powering off the console: what you need to know':'Going to sleep: what you need to know';
 if(po)p(ACT_OFF);else{p(ACT_SLEEP);if(state&&state.kstuff)p(ACT_KSTUFF)}
 $('actHideRow').style.display=po?'none':'flex';$('actHide').checked=false;popOpen($('actModal'),'flex');$('actGo').focus()}
function actArm(){api('/api/autosleep',{action:'arm'},function(c,j){if(c!==200&&j&&j.message)cfmInfo('Failed',j.message);refresh()})}
$('actCancel').onclick=function(){popClose($('actModal'))};
$('actGo').onclick=function(){var hide=$('actHide').checked&&$('actHideRow').style.display!=='none';popClose($('actModal'));
 if(hide&&state&&state.version)api('/api/config',{sleep_warnings_hidden:state.version},function(){actArm()});else actArm()};
$('asBtn').onclick=function(){
 if(asLastArmed){api('/api/autosleep',{action:'disarm'},function(c,j){if(c!==200&&j&&j.message)cfmInfo('Failed',j.message);refresh()});return}
 var hidden=!!(state&&state.version&&state.config&&state.config.sleep_warnings_hidden===state.version);
 if(!actPoweroff()&&hidden)actArm();else actOpen()};
function refresh(){if(quitting)return;api('/api/state',null,function(c,j){if(c===200&&j)render(j)})}
$('save').onclick=saveAll;
$('reset').onclick=function(){resetSchema(ALL,'f_');resetSchema(SLEEP_SCHEMA,'sf_')};
function accSet(id,open){var body=$(id+'Body'),head=$(id+'Btn');slideShow(body,open,'block');head.setAttribute('aria-expanded',open?'true':'false');head.classList.toggle('open',open)}
['accSettings','accExpert','accDiag','accCredits'].forEach(function(id){accSet(id,false);
 $(id+'Btn').onclick=function(){accSet(id,!isShown($(id+'Body')))}});
var seenId=null,notifLatest=0,notifOpen=false;
try{var sv=localStorage.getItem('notif_seen');if(sv!==null)seenId=parseInt(sv,10)||0}catch(e){}
function saveSeen(){try{localStorage.setItem('notif_seen',String(seenId))}catch(e){}}
function bellRender(){if(seenId===null)return;var n=Math.max(0,notifLatest-seenId),b=$('bellBadge');b.textContent=n>99?'99+':String(n);b.style.display=(n>0&&!notifOpen)?'block':'none'}
function notifState(latest){if(latest===undefined)return;if(seenId===null){seenId=latest;saveSeen()}
 var changed=latest!==notifLatest;notifLatest=latest;if(changed&&notifOpen)notifLoad();bellRender()}
function pad(n){return n<10?'0'+n:''+n}
var nfSeen=null; /* keys of the notifications already shown: new ones appear smoothly on the next render */
var nfSig=''; /* signature of the last rendered panel: a repeated request with the same data does not re-render it (otherwise a re-render in the first fractions of a second cut off the smooth appearance of a new notification) */
function notifLoad(){api('/api/notifications',null,function(c,j){if(c!==200||!j)return;notifLatest=j.latest;if(notifOpen){seenId=j.latest;saveSeen()}
 var sig=j.latest+'|'+j.items.length;if(sig===nfSig&&$('bellPanel').children.length>1){bellRender();return}nfSig=sig;
 var p=$('bellPanel');p.innerHTML='';var nfNext={},nfAnim=(window.PS5_ANIM_MS|0)>0&&animReady;var h=document.createElement('div');h.className='nfh';h.appendChild(document.createTextNode('Notifications'));
 var cl=document.createElement('button');cl.className='alt';cl.textContent='Clear';cl.onclick=function(ev){ev.stopPropagation();var doClear=function(){api('/api/notifications',{action:'clear'},function(){nfSeen=null;nfSig='';notifLoad()})};var items=document.querySelectorAll('#bellPanel .nf'),ms=window.PS5_ANIM_MS|0;if(ms&&animReady&&items.length){Array.prototype.forEach.call(items,function(e){e.classList.add('ps5gone')});setTimeout(doClear,ms+30)}else doClear()};h.appendChild(cl);p.appendChild(h);
 if(!j.items.length){var e=document.createElement('div');e.className='help';e.textContent='No notifications yet.';p.appendChild(e)}
 j.items.slice().reverse().forEach(function(it,idx){var d=document.createElement('div');var kind=it.kind===undefined?(it.shown?1:0):it.kind;d.className='nf'+(idx===0?' first':'')+(kind===0||kind===4?' off':'')+(kind===2?' ev':'')+(kind===3||kind===4?' sys':'');var nfKey=it.time+'|'+it.text;nfNext[nfKey]=1;if(nfAnim&&nfSeen&&!nfSeen[nfKey])d.className+=' ps5new';
  var t=new Date(it.time*1000),sm=document.createElement('small');sm.textContent=pad(t.getDate())+'.'+pad(t.getMonth()+1)+' '+pad(t.getHours())+':'+pad(t.getMinutes())+':'+pad(t.getSeconds());
  if(kind===3||kind===4){var b3=document.createElement('b');b3.className='sysb';b3.textContent=' · system';sm.appendChild(b3)}
  if(kind===0||kind===4){var b=document.createElement('b');b.textContent=' · disabled in the settings: not sent to the console';sm.appendChild(b)}
  if(kind===2){var b2=document.createElement('b');b2.textContent=' · event';sm.appendChild(b2)}
  d.appendChild(sm);d.appendChild(document.createTextNode(it.text));p.appendChild(d)});
 nfSeen=nfNext;bellRender()})}
$('bell').onclick=function(ev){ev.stopPropagation();notifOpen=!notifOpen;if(notifOpen){popOpen($('bellPanel'),'block');notifLoad()}else{popClose($('bellPanel'));bellRender()}};
$('bellPanel').onclick=function(ev){ev.stopPropagation()};
document.addEventListener('click',function(){if(notifOpen){notifOpen=false;popClose($('bellPanel'));bellRender()}});
function iconAct(a){var m=$('iconMsg');m.className='msg';m.textContent='';api('/api/icon',{action:a},function(c,j){if(c!==200){m.className='msg err';m.textContent=(j&&j.message)||'Error'}refresh()})}
if($('iconInstall')){$('iconInstall').onclick=function(){iconAct('install')};$('iconRemove').onclick=function(){cfmAsk('Remove the icon?','The Transmission icon on the “Media” tab will be removed.','Remove',function(){iconAct('remove')})}}
$('sleepCancel').onclick=function(){api('/api/sleep',{action:'cancel'},function(){refresh()})};
function dgShow(t){$('dgOut').textContent=t}
function dgGet(url,label){dgShow(label+'…');var x=new XMLHttpRequest();x.open('GET',url);x.onload=function(){var t=x.responseText;try{var j=JSON.parse(t);if(j&&j.text!==undefined)t=j.text}catch(e){}dgShow(t)};x.onerror=function(){dgShow('Connection error')};x.send()}
$('dgAll').onclick=function(){dgGet('/api/diag/all','Snapshot')};
$('dgSelect').onclick=function(){var r=document.createRange();r.selectNodeContents($('dgOut'));var s=window.getSelection();s.removeAllRanges();s.addRange(r)};
$('dgProbe').onclick=function(){dgGet('/api/diag/probe','Check')};
$('dgDisks').onclick=function(){dgGet('/api/diag/disks','Drives')};
$('dgMounts').onclick=function(){dgGet('/api/diag/mounts','Mount points')};
var DEL_ICON='<svg viewBox="0 0 24 24" width="22" height="22" aria-hidden="true"><path d="M10 11v6m4-6v6m5-11v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6M3 6h18M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/></svg>';
var OPEN_ICON='<svg viewBox="0 0 24 24" width="22" height="22" aria-hidden="true"><path d="m6 14l1.5-2.9A2 2 0 0 1 9.24 10H20a2 2 0 0 1 1.94 2.5l-1.54 6a2 2 0 0 1-1.95 1.5H4a2 2 0 0 1-2-2V5a2 2 0 0 1 2-2h3.9a2 2 0 0 1 1.69.9l.81 1.2a2 2 0 0 0 1.67.9H18a2 2 0 0 1 2 2v2"/></svg>';
var EJECT_ICON='<svg viewBox="0 0 24 24" width="22" height="22" aria-hidden="true"><path d="M12 4 4.5 14h15z"/><rect x="5" y="16.5" width="14" height="3" rx="1"/></svg>';
var fdPending=null,fdFull=false,fdCand=[],FD_KIND={ssd:'M.2 SSD',usb:'USB drive',int:'Internal drive',other:'Other place'};
function fdSize(b){var g=b/1073741824;return g>=1?g.toFixed(1)+' GB':(b/1048576).toFixed(0)+' MB'}
var fdMsgTimer=0;
function fdMsg(t,bad){var m=$('fdMsg');m.textContent=t;m.className='msg'+(bad?' bad':'');clearTimeout(fdMsgTimer);if(t)fdMsgTimer=setTimeout(function(){m.textContent=''},bad?7000:3000)}
/* The block changes its content: its height goes from the old to the new one smoothly (the things below it slide, nothing jumps). No change of height: nothing happens. */
function morphHeight(el,change){var ms=window.PS5_ANIM_MS|0;
 if(!ms||!animReady){change();return}
 var h0=el.getBoundingClientRect().height;clearTimeout(el._ps5mt);el.style.transition='';el.style.height='';el.style.overflow='';
 change();var h1=el.getBoundingClientRect().height;
 if(Math.abs(h1-h0)<1)return;
 el.style.overflow='hidden';el.style.height=h0+'px';void el.offsetHeight;el.style.transition='height '+ms+'ms ease';el.style.height=h1+'px';
 el._ps5mt=setTimeout(function(){el.style.transition='';el.style.height='';el.style.overflow=''},ms+40)}
function fdRender(j){morphHeight($('fdList'),function(){fdRenderNow(j)})}
function fdRenderNow(j){var list=$('fdList');list.innerHTML='';
 j.folders.forEach(function(f){var row=document.createElement('div');row.className='fd-row';
  if(!f.mounted)row.className+=' off';
  var ic=document.createElement('span');ic.className='fd-ico '+(f.kind==='other'?'int':f.kind);ic.setAttribute('aria-hidden','true');
  var info=document.createElement('div');info.className='fd-info';
  var path=document.createElement('div');path.className='fd-path';path.textContent=f.path;
  var meta=document.createElement('div');meta.className='fd-meta'+(f.mounted?'':' bad');
  meta.textContent=FD_KIND[f.kind]+' · '+(f.mounted?'free '+fdSize(f.free)+' of '+fdSize(f.total):'drive not connected');
  if(f.kind==='int'||f.fsbad){meta.appendChild(document.createTextNode(' · '));var wb=document.createElement('span');wb.className='fd-warnrow';var w=document.createElement('span');w.className='fd-red';w.textContent='not recommended'+(f.fsbad&&f.fs?' ('+f.fs+')':'');wb.appendChild(w);meta.appendChild(wb);
   var q=document.createElement('button');q.type='button';q.className='fd-q';q.textContent='?';q.setAttribute('aria-label','More details');q.onclick=function(){fdWarn(f.fsbad?'fs':'int',f.fs,false)};wb.appendChild(q)}
  var isExt=!!(f.device&&f.device.indexOf('/dev/es')===0);
  if(isExt){meta.appendChild(document.createTextNode(' · the console’s extended storage'));var qe=document.createElement('button');qe.type='button';qe.className='fd-q';qe.textContent='?';qe.setAttribute('aria-label','More details');qe.onclick=function(){fdWarn('es',f.fs,false)};meta.appendChild(qe)}
  info.appendChild(path);info.appendChild(meta);
  var del=document.createElement('button');del.type='button';del.className='alt fd-btn fd-del stroke';del.setAttribute('aria-label','Remove');del.title='Remove';del.innerHTML=DEL_ICON;del.onclick=function(){fdRemove(f.path)};
  var op=document.createElement('button');op.type='button';op.className='alt fd-btn fd-open stroke';op.setAttribute('aria-label','Open folder');op.title='Open folder';op.innerHTML=OPEN_ICON;op.disabled=!f.mounted;op.onclick=function(){fdOpen(f.path)};
  row.appendChild(ic);row.appendChild(info);
  if(f.kind==='usb'&&f.mounted){var um=document.createElement('button');um.type='button';um.className='alt fd-btn fd-eject';um.setAttribute('aria-label','Unmount the drive');um.title='Unmount the drive';um.innerHTML=EJECT_ICON;um.onclick=function(){umOpen(f)};row.appendChild(um)}
  row.appendChild(op);row.appendChild(del);
  list.appendChild(row)});
 slideShow($('fdEmpty'),!j.folders.length,'block');
 fdFull=j.folders.length>=j.limit;fdLayout();
 fdCand=j.candidates||[]}

/* “Open folder” next to a download folder: the same file managers as the item in the torrent menu of the Transmission page (the saved setting), in the same way: a new tab, on the console the same window. */
function fdOpen(path){var m=state&&state.config.web_openfolder;if(m!=='bfpilot'&&m!=='webfm')return;
 var u='http://'+location.hostname+':'+(m==='bfpilot'?5905:8888)+'/#'+encodeURIComponent(path);
 if(/PlayStation/i.test(navigator.userAgent||''))location.href=u;else window.open(u,'_blank','noopener')}
function fdLayout(){$('fdShowAdd').style.display=fdFull?'none':'';slideShow($('fdLimit'),fdFull,'block')}
function fdRefresh(){api('/api/folders',null,function(c,j){if(c===200&&j)fdRender(j)})}
/* A “?” at the end of a short caption that opens the details in the same window. */
function helpBtn(el,title,paras){var q=document.createElement('button');q.type='button';q.className='fd-q hq';q.textContent='?';q.setAttribute('aria-label','More details');q.onclick=function(e){e.preventDefault();e.stopPropagation();helpOpen(title,paras)};el.appendChild(q)}
/* The full description of a setting (the “?” next to its short one), in the same window as the folder warnings. */
function helpOpen(title,paras){var body=$('fdWarnBody');body.innerHTML='';$('fdWarnTitle').textContent=title;
 paras.forEach(function(t){var p=document.createElement('p');p.textContent=t;body.appendChild(p)});
 $('fdConfirm').style.display='none';$('fdCancel').textContent='Got it';popOpen($('fdModal'),'flex');$('fdCancel').focus()}
/* Folder warnings: internal drive and a file system without sparse files (exFAT, FAT32). confirm=false: help from the “?” icon in the list (no add button). */
function fdWarn(reason,fs,confirm){var fsw=reason==='fs',body=$('fdWarnBody');body.innerHTML='';
 if(reason==='es'){$('fdWarnTitle').textContent='The console’s extended storage';
  var e1=document.createElement('p');e1.textContent='This drive is connected as the console’s extended storage (encrypted, managed by the system itself). Unmounting asks the system to disconnect it, the same as “Safely remove from PS5”. Close the games and apps that use it first; what is stored on it is unavailable until you connect the drive again.';body.appendChild(e1);
  var e2=document.createElement('p');e2.textContent='You can also disconnect such a drive with the console itself: Settings → Storage → your drive → “Safely remove from PS5”.';body.appendChild(e2);
  $('fdConfirm').style.display='none';$('fdCancel').textContent='Got it';popOpen($('fdModal'),'flex');$('fdCancel').focus();return}
 function p(text,boldFirst){var e=document.createElement('p');if(boldFirst){var b=document.createElement('b');b.textContent=boldFirst;e.appendChild(b);e.appendChild(document.createTextNode(' '+text))}else e.textContent=text;body.appendChild(e)}
 if(fsw){$('fdWarnTitle').textContent='A drive with the {1} file system: freezes are possible'.replace('{1}',fs||'exFAT');
  p('Drives with the exFAT and FAT32 file systems have no “sparse” files. When a torrent downloads pieces in a random order, the system fills the whole gap before the next piece with zeros; during that time Transmission does not respond, and sometimes the whole console hangs. We soften this with the “Fix for USB drives” (pieces are downloaded in order), but the speed is lower.');
  p('Format the drive with the console itself for use by the PS5 system (Settings → Storage): then it gets the UFS file system and there will be no problems. Warning: formatting deletes all data on the drive.','Best of all:');
  if(confirm)p('Add the folder on this drive anyway?')}
 else{$('fdWarnTitle').textContent='Internal drive: better not to do this';
  p('Using the console’s internal drive for downloads is not advisable: torrents quickly fill it up, and constant writing wears out the system SSD.');
  p(confirm?'Add the folder on the internal drive anyway?':'','It is better to use an external USB drive or an M.2 SSD.')}
 $('fdConfirm').style.display=confirm?'':'none';$('fdCancel').textContent=confirm?'Cancel':'Got it';
 popOpen($('fdModal'),'flex');$('fdCancel').focus()}
function fdAdd(p,confirm){fdMsg('');
 api('/api/folders',{action:'add',path:p,confirm:!!confirm},function(c,j){
  if(c===200){fdMsg('Folder added');fdRefresh();return}
  if(c===409&&j&&j.need_confirm){fdPending=p;fdWarn(j.reason==='fs'?'fs':'int',j.fs,true);return}
  fdMsg((j&&j.error)||'Could not add the folder',true)})}
/* Unmounting a USB drive: a dialog with an explanation, then (if the drive is busy) who holds it and a forced eject button */
var umFolder=null;
function umText(t,cls){var e=document.createElement('p');if(cls)e.className=cls;e.textContent=t;$('umBody').appendChild(e)}
function umState(state,data){var body=$('umBody');body.innerHTML='';$('umHolders').style.display='none';$('umHolders').innerHTML='';
 var go=$('umGo'),force=$('umForce'),cancel=$('umCancel');go.style.display='none';force.style.display='none';cancel.style.display='';cancel.textContent='Cancel';
 if(state==='ask'){$('umTitle').textContent='Unmount the USB drive?';umText('The torrents on this drive will be stopped and the drive will be unmounted together with the other partitions of the same USB drive. After that the drive can be removed. To use it again you may have to reconnect it.');
  go.style.display='';go.textContent='Unmount'}
 else if(state==='work'){$('umTitle').textContent='Unmounting the drive…';umText('Stopping the torrents on this drive and unmounting it. This can take up to 20 seconds.');cancel.style.display='none'}
 else if(state==='busy'){$('umTitle').textContent='The drive is busy';umText('Could not unmount: someone is using the drive right now.');
  var h=data.holders||[],box=$('umHolders');box.style.display='block';
  var hd=document.createElement('div');hd.className='um-hd';hd.textContent=h.length?'Who is holding the drive:':'Could not find out who is holding the drive (most likely the system).';box.appendChild(hd);
  var KIND={system:'System',app:'Application',ours:'Transmission'};
  h.forEach(function(x){var r=document.createElement('div');r.className='um-row';var k=document.createElement('b');k.textContent=KIND[x.kind]||KIND.app;r.appendChild(k);
   r.appendChild(document.createTextNode(': '));r.appendChild(document.createTextNode('{1} (pid {2}), open files: {3}'.replace('{1}',x.name).replace('{2}',x.pid).replace('{3}',x.count)));if(x.sample){var s=document.createElement('div');s.className='um-sample';s.textContent=x.sample;r.appendChild(s)}box.appendChild(r)});
  umText('Forced eject first flushes the data to the drive and then detaches it. It is safe if nothing is being copied from the drive right now; if some operation was still running, it will be interrupted.');
  force.style.display=''}
 else if(state==='ok'){$('umTitle').textContent='The drive is unmounted';umText((data&&data.message)||'The drive can be removed.');cancel.textContent='Done'}
 else{$('umTitle').textContent='Could not unmount the drive';umText((data&&data.error)||'Unknown error.','fd-red');cancel.textContent='Close'}}
function umOpen(f){umFolder=f;$('umPath').textContent=f.path;umState('ask');popOpen($('fdUmModal'),'flex');$('umCancel').focus()}
function umRun(force){umState('work');api('/api/folders',{action:'unmount',path:umFolder.path,force:force},function(c,j){
 if(c===200&&j&&j.ok)umState('ok',j);else if(j&&j.busy)umState('busy',j);else umState('err',j);fdRefresh()})}
$('umGo').onclick=function(){umRun(false)};
$('umForce').onclick=function(){umRun(true)};
$('umCancel').onclick=function(){popClose($('fdUmModal'));umFolder=null};
/* Download folder picker: a dialog with drive selection, drive contents and folder creation */
var pkRoot='',pkPath='',pkSeq=0;
var PK_FOLDER='<svg class="pk-ico" viewBox="0 0 24 24" aria-hidden="true"><path d="M3 6.5A2.5 2.5 0 0 1 5.5 4h4l2 2.2h7A2.5 2.5 0 0 1 21 8.7v9.8a2.5 2.5 0 0 1-2.5 2.5h-13A2.5 2.5 0 0 1 3 18.5z"/></svg>';
var PK_UP='<svg class="pk-ico" viewBox="0 0 24 24" aria-hidden="true"><path d="M12 5l-7 7h4v7h6v-7h4z"/></svg>';
function pkMsg(t,bad){var m=$('pkMsg');m.textContent=t||'';m.className='msg'+(bad?' bad':'')}
function pkDisks(){var a=[];(fdCand||[]).forEach(function(c){a.push({root:c.root,kind:c.kind,fs:c.fs,fsbad:c.fsbad,free:c.free,total:c.total})});a.push({root:'/data',kind:'int',free:0,total:0});return a}
function pkOpen(){var d=pkDisks(),sel=$('pkDisk');sel.innerHTML='';
 d.forEach(function(x){var o=document.createElement('option');o.value=x.root;o.textContent=x.root+' · '+FD_KIND[x.kind]+(x.fs?' ('+x.fs+')':'')+(x.total?', free '+fdSize(x.free)+' of '+fdSize(x.total):'')+(x.fsbad||x.kind==='int'?', not recommended':'');sel.appendChild(o)});
 var pick=d.filter(function(x){return x.kind==='ssd'&&x.root==='/mnt/ext1'})[0]||d.filter(function(x){return x.kind==='ssd'})[0]||d.filter(function(x){return x.kind==='usb'})[0]||d[d.length-1];
 sel.value=pick.root;pkRoot=pick.root;$('pkNew').style.display='none';pkMsg('');popOpen($('fdPickModal'),'flex');pkLoad(pick.root)}
function pkLoad(path){var seq=++pkSeq;pkMsg('');
 api('/api/fs',{action:'list',path:path},function(c,j){if(seq!==pkSeq)return;
  if(c!==200||!j||!j.dirs){pkPath='';$('pkList').innerHTML='';$('pkCrumbs').innerHTML='';$('pkChoose').disabled=true;pkMsg((j&&j.error)||'Could not open the folder',true);return}
  pkPath=j.path;pkRender(j);
  var bare=pkPath==='/data';$('pkChoose').disabled=bare;if(bare)pkMsg('You cannot use /data itself: go into a folder or create a new one.')})}
function pkRender(j){var cr=$('pkCrumbs');cr.innerHTML='';
 var rel=pkPath.substring(pkRoot.length).split('/').filter(Boolean),acc=pkRoot;
 function crumb(label,path,cur){var b=document.createElement('button');b.type='button';b.className='pk-crumb'+(cur?' cur':'');b.textContent=label;if(!cur)b.onclick=function(){pkLoad(path)};cr.appendChild(b)}
 function sep(){var s=document.createElement('span');s.className='pk-sep';s.textContent='›';cr.appendChild(s)}
 crumb(pkRoot,pkRoot,rel.length===0);rel.forEach(function(seg,i){sep();acc+='/'+seg;crumb(seg,acc,i===rel.length-1)});
 var list=$('pkList');list.innerHTML='';
 function row(cls,icon,label,go){var r=document.createElement('div');r.className='pk-row '+cls;r.tabIndex=0;r.setAttribute('role','option');r.innerHTML=icon;var n=document.createElement('span');n.className='pk-name';n.textContent=label;r.appendChild(n);r.onclick=go;r.onkeydown=function(e){if(e.key==='Enter'||e.keyCode===13){e.preventDefault();go()}};list.appendChild(r)}
 if(pkPath!==pkRoot)row('up',PK_UP,'Back',function(){pkLoad(pkPath.substring(0,pkPath.lastIndexOf('/'))||pkRoot)});
 j.dirs.forEach(function(name){row('',PK_FOLDER,name,function(){pkLoad(pkPath+'/'+name)})});
 if(!j.dirs.length){var e=document.createElement('div');e.className='pk-empty';e.textContent='No subfolders';list.appendChild(e)}
 if(j.truncated){var t=document.createElement('div');t.className='pk-empty';t.textContent='The first 500 folders are shown';list.appendChild(t)}
 list.scrollTop=0}
$('pkDisk').onchange=function(){pkRoot=this.value;$('pkNew').style.display='none';pkLoad(pkRoot)};
$('pkNewBtn').onclick=function(){$('pkNew').style.display='flex';$('pkName').value='';$('pkName').focus()};
$('pkMakeCancel').onclick=function(){$('pkNew').style.display='none';pkMsg('')};
function pkMake(){pkMsg('');api('/api/fs',{action:'mkdir',path:pkPath,name:$('pkName').value},function(c,j){
 if(c===200&&j&&j.ok){$('pkNew').style.display='none';pkLoad(j.path);pkMsg('Folder created')}else pkMsg((j&&j.error)||'Could not create the folder',true)})}
$('pkMake').onclick=pkMake;
$('pkName').onkeydown=function(e){if(e.key==='Enter'||e.keyCode===13){e.preventDefault();pkMake()}};
$('pkCancel').onclick=function(){popClose($('fdPickModal'))};
$('pkChoose').onclick=function(){var p=pkPath;if(!p||p==='/data')return;popClose($('fdPickModal'));if(p)fdAdd(p,false)};
$('fdShowAdd').onclick=function(){fdMsg('');pkOpen()};
$('fdCancel').onclick=function(){popClose($('fdModal'));fdPending=null};
$('fdConfirm').onclick=function(){popClose($('fdModal'));if(fdPending!==null){var p=fdPending;fdPending=null;fdAdd(p,true)}};
var fdDelPending=null;
function fdRemove(p){fdDelPending=p;$('fdDelPath').textContent=p;popOpen($('fdDelModal'),'flex');$('fdDelCancel').focus()}
$('fdDelCancel').onclick=function(){popClose($('fdDelModal'));fdDelPending=null};
$('fdDelConfirm').onclick=function(){var p=fdDelPending;popClose($('fdDelModal'));fdDelPending=null;if(p===null)return;
 api('/api/folders',{action:'remove',path:p},function(c,j){if(c!==200)fdMsg((j&&j.error)||'Could not remove',true);else fdMsg('Folder removed');fdRefresh()})};
document.addEventListener('keydown',function(e){if(e.key==='Escape'||e.keyCode===27){$('fdCancel').onclick();$('fdDelCancel').onclick();$('pkCancel').onclick();$('umCancel').onclick();$('actCancel').onclick();$('cfmCancel').onclick()}});
fdRefresh();setInterval(fdRefresh,10000);
$('dgThreads').onclick=function(){dgGet('/api/diag/threads','Threads')};
$('dgLog').onclick=function(){dgGet('/api/diag/log','Log')};
$('dgLogFile').onclick=function(){dgGet('/api/diag/logfile','Manager log file')};
$('dgTlog').onclick=function(){dgGet('/api/diag/tlog','Transmission log')};
$('dgDlog').onclick=function(){dgGet('/api/diag/dlog','Daemon log')};
refresh();setInterval(refresh,1500);
