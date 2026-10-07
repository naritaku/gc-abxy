(function(){
var BASE=document.documentElement.getAttribute('data-base')||'';
function data(id){var e=document.getElementById(id);try{return e?JSON.parse(e.textContent):{};}catch(x){return{};}}
var MEDIA=data('gc-media');
var WEBM=(function(){try{var t=document.createElement('video');return!!(t.canPlayType&&t.canPlayType('video/webm; codecs="vp9"'));}catch(e){return false;}})();
function clipSrc(n){var m=MEDIA[n];return BASE+(m?((WEBM&&m.w)?m.w:m.m):'anim/'+n+'.mp4');}
function clipPoster(n){var m=MEDIA[n];return BASE+((m&&m.p)?m.p:'anim/'+n+'.png');}
(function(){
var id=location.hash.slice(1),map=data('gc-ids');
if(!id||document.getElementById(id)||!map[id])return;
location.replace(map[id]);
})();
(function(){
var nav=(performance.getEntriesByType&&performance.getEntriesByType('navigation')[0])||{};
if(location.hash||nav.type==='back_forward'||nav.type==='reload')return;
if(window.self===window.top)return;
window.addEventListener('load',function(){var t=document.getElementById('top')||document.body;window.scrollTo(0,0);if(t.scrollIntoView)t.scrollIntoView({block:'start'});});
})();
[].forEach.call(document.querySelectorAll('.check label a'),function(a){
a.addEventListener('click',function(e){e.stopPropagation();});
});
(function(){
var bar=document.getElementById('tabbar');
var sbar=document.querySelector('main > .stepbar');
var hb=58,hs=0;
function stick(){var r=document.documentElement.style;r.setProperty('--tabh',hb+'px');r.setProperty('--stick',(hb+hs)+'px');}
if(bar&&window.ResizeObserver){
new ResizeObserver(function(es){es.forEach(function(e){var b=e.borderBoxSize&&e.borderBoxSize[0],h=b?b.blockSize:e.target.offsetHeight;
if(e.target===bar)hb=h;else hs=h;});stick();}).observe(bar);
if(sbar)new ResizeObserver(function(es){var b=es[0].borderBoxSize&&es[0].borderBoxSize[0];hs=b?b.blockSize:sbar.offsetHeight;stick();}).observe(sbar);
}else if(bar){requestAnimationFrame(function(){hb=bar.offsetHeight;hs=sbar?sbar.offsetHeight:0;stick();});}
function Asm(svg){
var els={};[].forEach.call(svg.querySelectorAll('[data-el]'),function(g){els[g.getAttribute('data-el')]=g;});
var asm=svg.querySelector('.asm'),labs=svg.querySelectorAll('.lab'),timers=[];
function pos(k,s){
switch(k){
case'xiao':return s<1?[-40,0]:[0,1];
case'holder':return s<2?[-40,0]:[0,1];
case'slide':return s<3?[-40,0]:[0,1];
case'tact':return s<4?[-40,0]:[0,1];
case'face':case'rubber':return s<5?[40,0]:[0,1];
case'oldscrews':return s<5?[0,0]:s<6?[0,1]:[-60,0];
case'oldback':return s<5?[0,0]:s<7?[0,1]:[-80,0];
case'geta':return s<5?[0,0]:s<8?[0,1]:[-70,0];
case'board':return s<5?[-30,1]:s<10?[-140,0]:[0,1];
case'knob':return s<11?[-40,0]:[0,1];
case'cap':return s<12?[-40,0]:[0,1];
case'back':return s<13?[-70,0]:[0,1];
case'screws':return s<14?[-40,0]:[0,1];
case'batt':case'door':return s<15?[-50,0]:[0,1];
}
}
function apply(s){
Object.keys(els).forEach(function(k){var p=pos(k,s),g=els[k];
g.style.transform='translateY('+p[0]+'px)';g.style.opacity=p[1];
g.style.transitionDelay=(k==='door'&&s===15)?'.4s':'0s';
g.classList.remove('hl');});
if(s===9){void els.rubber.getBoundingClientRect();els.rubber.classList.add('hl');}
asm.style.transform=s===16?'scaleY(-1)':'none';
[].forEach.call(labs,function(t){t.style.opacity=s===16?0:1;});
}
var snaps=0;
function snap(s){var n=++snaps;svg.classList.add('snap');apply(s);
requestAnimationFrame(function(){requestAnimationFrame(function(){if(n===snaps)svg.classList.remove('snap');});});}
function stop(){timers.forEach(clearTimeout);timers=[];}
function play(a,b){
stop();var start=a===0?0:a-1;snap(start);
var t=a===0?600:350,seq=[];for(var i=Math.max(a,start+1);i<=b;i++)seq.push(i);
if(a===0&&b===0)seq=[];
seq.forEach(function(st){timers.push(setTimeout(function(){apply(st);},t));t+=1050;});
timers.push(setTimeout(function(){play(a,b);},t+1900));
}
snap(16);
return{play:play,stop:stop};
}
function marks(svg){
var cs=[].slice.call(svg.querySelectorAll('circle[fill="#574c9b"]'));
cs.forEach(function(c){
var t=c.nextElementSibling,o=c.previousElementSibling,g=document.createElementNS('http://www.w3.org/2000/svg','g');
g.setAttribute('class','mk');g.setAttribute('data-n',t?t.textContent:'');
c.parentNode.insertBefore(g,c);
if(o&&o.getAttribute('stroke-dasharray')&&o.getAttribute('fill')==='none')g.appendChild(o);
g.appendChild(c);if(t)g.appendChild(t);
});
}
var RM=!!(window.matchMedia&&matchMedia('(prefers-reduced-motion: reduce)').matches);
function initClip(sc){
var st=sc._stage,inn=st.querySelector('.stage-in'),d=document.createElement('div'),v=document.createElement('video');
d.className='vid';v.muted=true;v.loop=true;v.playsInline=true;v.setAttribute('playsinline','');v.preload='none';v.autoplay=!RM;
d.appendChild(v);inn.insertBefore(d,inn.querySelector('.stage-n'));
sc._vid=v;sc._clip=null;
if(st.classList.contains('anim'))st.classList.add('clips');
}
function clipTo(sc,name){
if(!sc._vid)return;
sc._stage.classList.toggle('has-vid',!!name);
if(!name){sc._vid.pause();return;}
if(sc._clip!==name){
sc._clip=name;sc._vid.poster=clipPoster(name);
if(RM){sc._vid.removeAttribute('src');}else{sc._vid.src=clipSrc(name);}
}
if(!RM){var p=sc._vid.play();if(p&&p.catch)p.catch(function(){});}
}
var scs=[].slice.call(document.querySelectorAll('.scrolly'));
scs.forEach(function(sc){
sc.classList.add('live');
sc._lis=[].slice.call(sc.querySelectorAll('ol.steps>li'));
sc._stage=sc.querySelector('.stage');sc._n=sc.querySelector('.stage-n');
if(sc.querySelector('ol.steps>li[data-clip]'))initClip(sc);
var a=sc.querySelector('.asmv');if(a)sc._anim=Asm(a);
[].forEach.call(sc.querySelectorAll('.stage .render-img svg'),marks);
sc._idx=-1;
});
function activate(sc,idx){
sc._idx=idx;var li=sc._lis[idx];
sc._lis.forEach(function(l,i){l.classList.toggle('now',i===idx);});
sc._n.textContent='手順 '+(idx+1);
var fr=li.getAttribute('data-frame');
if(fr){[].forEach.call(sc.querySelectorAll('.stage [data-frame]'),function(f){f.classList.toggle('on',f.getAttribute('data-frame')===fr);});}
var mk=li.getAttribute('data-mark');
if(mk!==null){[].forEach.call(sc.querySelectorAll('.stage .render-img svg'),function(svg){
svg.classList.toggle('focus',mk!=='0');
[].forEach.call(svg.querySelectorAll('.mk'),function(g){g.classList.toggle('on',g.getAttribute('data-n')===mk);});});}
var cl=li.getAttribute('data-clip');clipTo(sc,cl);
sc._stage.classList.toggle('nocaps',!!cl&&mk==='0');
if(cl&&sc._anim)sc._anim.stop();
var sn=li.getAttribute('data-scene');
if(sn&&sc._anim&&!cl){var r=sn.split('-').map(Number);sc._anim.play(r[0],r.length>1?r[1]:r[0]);}
}
var ticking=false;
function update(){
ticking=false;var vh=window.innerHeight;
var end=window.scrollY+vh>=document.documentElement.scrollHeight-4;
var plans=scs.map(function(sc){
var r=sc.getBoundingClientRect();
if(!sc.offsetParent||r.bottom<0||r.top>vh)return{sc:sc,out:true};
var st=sc._stage,tops=sc._lis.map(function(li){return li.getBoundingClientRect().top;});
var natural=st.getBoundingClientRect().bottom-(sc._off||0);
var over=natural-tops[tops.length-1]+8,off=over>0?-over:0;
var sb=natural+off,line=sb+(vh-sb)*0.3,idx=0;
tops.forEach(function(t,i){if(t<=line)idx=i;});
if(end)tops.forEach(function(t,i){if(i>idx&&t<vh)idx=i;});
return{sc:sc,off:off,idx:idx};
});
plans.forEach(function(p){
var sc=p.sc;
if(p.out){if(sc._idx!==-1){sc._idx=-1;if(sc._anim)sc._anim.stop();if(sc._vid)sc._vid.pause();}return;}
if(p.off!==(sc._off||0)){sc._off=p.off;sc._stage.style.transform=p.off?'translateY('+p.off+'px)':'';}
if(p.idx!==sc._idx)activate(sc,p.idx);
});
}
function req(){if(!ticking){ticking=true;requestAnimationFrame(update);}}
window.addEventListener('scroll',req,{passive:true});window.addEventListener('resize',req);
ticking=true;requestAnimationFrame(function(){setTimeout(update,0);});
})();
(function(){
var vs=[].slice.call(document.querySelectorAll('figure.clips video, .hero-fig video'));
if(!vs.length)return;
var RM=!!(window.matchMedia&&matchMedia('(prefers-reduced-motion: reduce)').matches);
if(RM||!('IntersectionObserver'in window))return;
var io=new IntersectionObserver(function(es){es.forEach(function(e){
var v=e.target;
if(e.isIntersecting){if(!v.getAttribute('src')){v.autoplay=true;var d=v.getAttribute('data-src'),n=(d.match(/anim\/([^\/?]+)\.mp4/)||[])[1];v.src=(n&&MEDIA[n])?clipSrc(n):d;}var p=v.play();if(p&&p.catch)p.catch(function(){});}
else v.pause();
});},{threshold:0.2});
vs.forEach(function(v){io.observe(v);});
})();
var boxes=[].slice.call(document.querySelectorAll('.check input'));
boxes.forEach(function(b){
try{if(localStorage.getItem('gcabxy-'+b.id)==='1')b.checked=true;}catch(e){}
b.addEventListener('change',function(){
try{localStorage.setItem('gcabxy-'+b.id,b.checked?'1':'0');}catch(e){}
});
});
})();
function btFail(e,verb){
if(e.name==='NotFoundError')return'選ばれませんでした。白く点滅する番号になっているか確かめて、もう一度押してください。';
if(e.name==='SecurityError'||/permissions policy/i.test(e.message||''))
return'このページはほかのページの中に埋め込んで表示されているため、ブラウザが Bluetooth を使わせません。https://gc-abxy.pages.dev/help/ を直接開いて、もう一度押してください。';
return verb+': '+e.message;
}
(function(){
var $=function(id){return document.getElementById(id);};
var cbtn=$('diag-connect');if(!cbtn)return;
var U=function(n){return'd8bd'+n+'-0b7f-4dc6-bc76-8c605103e9f1';};
var SVC=U('0001'),ST=U('0002'),CTL=U('0003'),CNT=U('0005'),BAT=U('0006');
var KEYS=['A','B','X','Y','PROFILE'],WANT=5,MIN_GAP=30,MAX_HOLD=2000;
var dev=null,ch={},fw='',name='',st=null,keys=null,bat=null,timer=null,pendingSince=0,lastDiag=-1;
var q=Promise.resolve();
function gatt(fn){var p=q.then(fn);q=p.catch(function(){});return p;}
function say(t){$('diag-msg').textContent=t;}
function set(id,t){$(id).textContent=t;}
if(!navigator.bluetooth){cbtn.disabled=true;say('このブラウザでは使えません。PC か Android の Chrome / Edge で開いてください。');return;}
var RC_WATCHDOG=0x08;
var CAUSES=[[0x01,'電源を入れた（電池の電圧が下がって落ちたときもこれになります）'],[0x02,'リセット'],[0x04,'プログラムからの再起動（書き込み・初期化など）'],[0x08,'ウォッチドッグ（プログラムが止まって自動で再起動）'],
[0x20,'省電力からの復帰'],[0x40,'デバッグ'],[0x80,'その他']];
function bootText(s){var b=s.boots;return'電源 '+b[0]+' ・ リセットボタン '+b[1]+' ・ 書き込み・初期化 '+b[2]+' ・ 異常 '+b[3]+' 回';}
function causeText(c){var a=CAUSES.filter(function(x){return c&x[0];}).map(function(x){return x[1];});return a.length?a.join('・'):'不明';}
function dur(ms){var s=Math.floor(ms/1000),d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);
return d?d+' 日 '+h+' 時間':h?h+' 時間 '+m+' 分':m?m+' 分 '+(s%60)+' 秒':s+' 秒';}
function hex(n,w){var t=n.toString(16).toUpperCase();while(t.length<w)t='0'+t;return t;}
function parseStatus(v){
var full=v.byteLength>=36;
var boots=full?[0,1,2,3].map(function(i){return v.getUint16(20+2*i,true);}):(st&&st.boots)||[0,0,0,0];
return{diag:v.getUint8(1),left:v.getUint16(2,true),up:v.getUint32(4,true),cause:v.getUint8(8),
prof:v.getUint8(9),bonded:v.getUint8(10),link:v.getUint8(11),mgmt:v.getUint8(12),bflags:v.getUint8(13),
mv:v.getUint16(14,true),pct:v.getUint8(16),boots:boots,boot:boots[0]+boots[1]+boots[2]+boots[3],
id:full?hex(v.getUint32(32,true),8)+hex(v.getUint32(28,true),8):(st&&st.id)||''};
}
function parseCounters(v){
var a=[];for(var i=0;i<5;i++){var o=2+8*i;
a.push({n:v.getUint16(o,true),gap:v.getUint16(o+2,true),last:v.getUint16(o+4,true),hold:v.getUint16(o+6,true)});}
return a;
}
function judge(k,i){
var want=WANT,gapOk=k.n<2||k.gap===0xFFFF||k.gap>=MIN_GAP,holdOk=k.hold<=MAX_HOLD;
if(k.n<want)return{cls:'',t:'あと '+(want-k.n)+' 回',gapOk:gapOk,holdOk:holdOk,done:false};
var ok=k.n===want&&gapOk&&holdOk;
return{cls:ok?'ok':'ng',t:ok?'OK':'要確認',gapOk:gapOk,holdOk:holdOk,done:true,ok:ok};
}
function gapText(k){return k.n<2||k.gap===0xFFFF?'—':k.gap+' ms';}
function holdText(k){return k.n<1?'—':(k.hold>=10000?(k.hold/1000).toFixed(0)+' s':k.hold+' ms');}
function showStatus(){
if(!st)return;
$('diag-state').hidden=false;
set('diag-name',name);set('diag-fw',fw||'読めませんでした');
set('diag-batt',(st.bflags&1)?st.mv+' mV（'+st.pct+' %）'+((st.bflags&2)?' ・ 残り少ない':''):'まだ測っていません');
set('diag-boot',bootText(st));set('diag-reset',causeText(st.cause));set('diag-up',dur(st.up));
var b=[];for(var i=0;i<3;i++)if(st.bonded&(1<<i))b.push(i+1);
set('diag-prof',(st.prof+1)+' 番'+((st.link&1)?'（つながっている）':'')+' ・ 登録済み: '+(b.length?b.join('・')+' 番':'なし'));
set('diag-out',(st.link&2)?'USB':'ワイヤレス');
var adv=null;
if(st.boots[3]>0&&!(st.cause&RC_WATCHDOG))adv=['プログラムが止まって自動で再起動した記録があります（'+st.boots[3]+' 回）','電池を新しい CR2450 に替えて様子を見てください。回数が増えるときは、プログラムを入れ直し、この結果をコピーして問い合わせてください。'];
else if(st.cause&RC_WATCHDOG)adv=['プログラムが止まって自動で再起動しています','電池を新しい CR2450 に替えて様子を見てください。何度も起きるときは、プログラムを入れ直し、この結果をコピーして問い合わせてください。'];
$('diag-advice').hidden=!adv;if(adv){set('diag-advice-h',adv[0]);set('diag-advice-p',adv[1]);}
showDiag();
}
function prompt(t){var p=$('diag-prompt');p.hidden=!t;p.firstChild.textContent=t||'';}
function showDiag(){
var d=st.diag,sb=$('diag-start');
sb.disabled=!(dev&&dev.gatt.connected)||d!==0;
$('diag-end').disabled=!(dev&&dev.gatt.connected);
if(d===1)prompt('本体の PROFILE と A を同時に 3 秒押し続けてください（LED がマゼンタで点滅中）。残り '+st.left+' 秒');
else if(d===2)prompt('記録中です（残り '+Math.floor(st.left/60)+' 分 '+(st.left%60)+' 秒）。A・B・X・Y・PROFILE を 5 回ずつ押してください。');
else if(lastDiag===1)prompt('確認の時間（30 秒）が過ぎました。もう一度「キーの確認を始める」を押してください。');
else if(lastDiag===2)prompt('記録を終えました（5 分たったか、終了しました）。表の値は残っています。');
if(d===2&&!timer){$('diag-keys').hidden=false;$('diag-legend').hidden=false;timer=setInterval(pollKeys,500);pollKeys();}
if(d!==2&&timer){clearInterval(timer);timer=null;}
lastDiag=d;
}
function showKeys(){
var tb=$('diag-keys-body');tb.textContent='';
keys.forEach(function(k,i){
var j=judge(k,i),tr=document.createElement('tr');
for(var c=0;c<5;c++)tr.appendChild(document.createElement('td'));
tr.children[4].appendChild(document.createElement('span'));
var td=tr.children;
td[0].textContent=KEYS[i];td[1].textContent=k.n+' / '+WANT;if(k.n>WANT)td[1].className='ng';
td[2].textContent=gapText(k);if(!j.gapOk)td[2].className='ng';
td[3].textContent=holdText(k);if(!j.holdOk)td[3].className='ng';
td[4].firstChild.textContent=j.t;td[4].firstChild.className='judge '+j.cls;
tb.appendChild(tr);
});
}
async function readStatus(){st=parseStatus(await gatt(function(){return ch.st.readValue();}));showStatus();}
async function pollKeys(){
try{keys=parseCounters(await gatt(function(){return ch.cnt.readValue();}));showKeys();}catch(e){}
}
async function readBattery(){
try{var v=await gatt(function(){return ch.bat.readValue();}),o=v.byteLength>=13?1:0;
bat={sample:v.getUint16(o,true),est:v.getUint16(o+2,true)};}catch(e){bat=null;}
}
var poll=null;
function gone(){
if(timer){clearInterval(timer);timer=null;}if(poll){clearInterval(poll);poll=null;}
cbtn.disabled=false;$('diag-start').disabled=true;$('diag-end').disabled=true;
}
cbtn.addEventListener('click',async function(){
cbtn.disabled=true;say('一覧から GC-ABXY-… を選んでください');
try{
dev=await navigator.bluetooth.requestDevice({filters:[{namePrefix:'GC-ABXY'}],
optionalServices:[SVC,'device_information','battery_service']});
dev.addEventListener('gattserverdisconnected',function(){gone();say('接続が切れました。記録中なら 5 分の間は続いています。もう一度「つなぐ」を押してください。');});
say('つないでいます…');
var g=await dev.gatt.connect();name=dev.name||'';
try{fw=new TextDecoder().decode(await(await(await g.getPrimaryService('device_information')).getCharacteristic('firmware_revision_string')).readValue());}catch(e){fw='';}
var s;
try{s=await g.getPrimaryService(SVC);}catch(e){say('診断に対応していないプログラムです。最新のプログラムを書き込むか、バージョンを確認して問い合わせてください。');dev.gatt.disconnect();return;}
ch.st=await s.getCharacteristic(ST);ch.ctl=await s.getCharacteristic(CTL);ch.cnt=await s.getCharacteristic(CNT);
try{ch.bat=await s.getCharacteristic(BAT);}catch(e){ch.bat=null;}
try{ch.st.addEventListener('characteristicvaluechanged',function(e){st=parseStatus(e.target.value);showStatus();});
await gatt(function(){return ch.st.startNotifications();});}catch(e){}
await readStatus();if(ch.bat)await readBattery();
if(st.diag===2)pollKeys();
poll=setInterval(function(){if(st&&st.diag!==0)readStatus().catch(function(){});},1000);
$('diag-copy').disabled=false;
say('つながりました');
}catch(e){
say(btFail(e,'つなげませんでした'));
cbtn.disabled=false;
}
});
async function control(bytes){
var b=new Uint8Array(bytes);
return gatt(function(){return ch.ctl.writeValueWithResponse?ch.ctl.writeValueWithResponse(b):ch.ctl.writeValue(b);});
}
$('diag-start').addEventListener('click',async function(){
try{keys=null;$('diag-keys-body').textContent='';await control([0x01]);await readStatus();}
catch(e){say('始められませんでした: '+e.message);}
});
$('diag-end').addEventListener('click',async function(){
try{if(st&&st.diag!==0)await control([0x02]);}catch(e){}
lastDiag=-1;prompt('');
try{if(dev&&dev.gatt.connected)dev.gatt.disconnect();}catch(e){}
gone();say('終了しました。記録は本体から消えました。');
});
function report(){
var L=['GC ABXY 診断結果（'+new Date().toLocaleDateString()+'）','名前: '+name,'バージョン: '+(fw||'?')];
if(st){
var b=[];for(var i=0;i<3;i++)if(st.bonded&(1<<i))b.push(i+1);
L.push('個体 ID: '+st.id,'起動回数: '+bootText(st),'直前の再起動: '+causeText(st.cause)+'（0x'+hex(st.cause,2)+'）',
'起動からの時間: '+dur(st.up),'接続先: '+(st.prof+1)+' 番（'+((st.link&1)?'つながっている':'つながっていない')+'）、登録済み: '+(b.length?b.join('・'):'なし'),
'出力: '+((st.link&2)?'USB':'ワイヤレス'),
'電池: '+((st.bflags&1)?st.mv+' mV / '+st.pct+' %'+((st.bflags&2)?'（残り少ない）':''):'未測定')+(bat?'（サンプル '+bat.sample+' mV、推定 '+bat.est+' mV）':''));
}
if(keys){
L.push('キー（回数 / 間隔の最短 / 押した長さの最長 / 判定）');
keys.forEach(function(k,i){L.push(KEYS[i]+': '+k.n+' / '+gapText(k)+' / '+holdText(k)+' / '+judge(k,i).t);});
}else L.push('キー: 確認していません');
return L.join('\n');
}
$('diag-copy').addEventListener('click',async function(){
if(dev&&dev.gatt.connected){try{await readStatus();if(ch.bat)await readBattery();}catch(e){}}
var t=report(),ta=$('diag-text');ta.value=t;ta.hidden=false;
try{await navigator.clipboard.writeText(t);say('結果をコピーしました。問い合わせに貼ってください。');}
catch(e){ta.select();say('コピーできませんでした。下の枠の中を選んでコピーしてください。');}
});
})();
