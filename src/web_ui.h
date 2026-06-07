// UI web embebida en flash (PROGMEM) — single page, sin filesystem.
// Sidebar con vistas (En vivo / Medición / Capturas / Configuración),
// modo claro/oscuro, sesiones de medición guiada y diseño responsive.
#pragma once

#include <pgmspace.h>

static const char WEB_UI_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<link rel="icon" href="data:image/svg+xml,<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'><text y='.9em' font-size='90'>%F0%9F%93%8F</text></svg>">
<title>Calibre-ESP</title>
<style>
:root{
  --bg:#14161a; --panel:#1d2026; --panel2:#2a2e36; --fg:#e8e8e8; --dim:#8a8f98;
  --acc:#ff8b1f; --accfg:#1a1208; --ok:#3ecf6e; --bad:#e0455a; --border:#2f343d;
}
body[data-theme="light"]{
  --bg:#eef0f3; --panel:#ffffff; --panel2:#e7eaee; --fg:#1b1e24; --dim:#667085;
  --acc:#e87410; --accfg:#fff; --ok:#179a4a; --bad:#cf3349; --border:#d9dde3;
}
*{box-sizing:border-box;margin:0;padding:0}
html,body{height:100%}
body{background:var(--bg);color:var(--fg);font-family:system-ui,Segoe UI,Roboto,sans-serif;transition:background .2s,color .2s}
#app{display:flex;min-height:100vh}

/* ---------- sidebar ---------- */
#side{width:210px;flex-shrink:0;background:var(--panel);border-right:1px solid var(--border);
  display:flex;flex-direction:column;padding:14px 10px;gap:4px;position:sticky;top:0;height:100vh}
#brand{font-size:1.05rem;font-weight:700;color:var(--acc);padding:6px 10px 16px;letter-spacing:.5px}
.navbtn{display:flex;align-items:center;gap:10px;width:100%;padding:11px 12px;border:0;border-radius:10px;
  background:transparent;color:var(--dim);font-size:.95rem;cursor:pointer;text-align:left}
.navbtn .ic{font-size:1.15rem;width:24px;text-align:center}
.navbtn.active{background:var(--panel2);color:var(--fg);font-weight:600}
.navbtn:hover{color:var(--fg)}
#side .grow{flex:1}
#themeBtn{margin-bottom:4px}
#sideFoot{color:var(--dim);font-size:.7rem;padding:8px 10px}
#sideFoot a{color:var(--dim)}

/* ---------- topbar / badges ---------- */
main{flex:1;min-width:0;padding:16px 18px 24px;max-width:980px;margin:0 auto}
#topbar{display:flex;justify-content:space-between;align-items:center;margin-bottom:14px}
#viewTitle{font-size:1.15rem;font-weight:650}
#badges{display:flex;gap:6px}
#badges span{font-size:.7rem;padding:4px 9px;border-radius:10px;background:var(--panel);
  border:1px solid var(--border);color:var(--dim);font-weight:600}
#badges span.on{color:var(--ok);border-color:var(--ok)}
#badges span.off{color:var(--bad)}

/* ---------- comunes ---------- */
.panel{background:var(--panel);border:1px solid var(--border);border-radius:14px;padding:16px;margin-bottom:14px}
h2{font-size:.85rem;color:var(--dim);margin-bottom:12px;text-transform:uppercase;letter-spacing:.6px}
button{background:var(--panel2);color:var(--fg);border:0;border-radius:9px;padding:11px 16px;font-size:.95rem;cursor:pointer}
button:active{transform:scale(.97)}
button.acc{background:var(--acc);color:var(--accfg);font-weight:650}
button.danger{color:var(--bad)}
button:disabled{opacity:.45;cursor:default}
.row{display:flex;gap:8px;flex-wrap:wrap;justify-content:center;margin-top:12px}
table{width:100%;border-collapse:collapse;font-size:.92rem}
td,th{padding:8px 8px;text-align:left;border-bottom:1px solid var(--border)}
th{color:var(--dim);font-weight:500;font-size:.72rem;text-transform:uppercase}
td.num{font-family:Consolas,Menlo,monospace;text-align:right;white-space:nowrap}
label{display:block;color:var(--dim);font-size:.8rem;margin:10px 0 4px}
input,select,textarea{width:100%;background:var(--bg);color:var(--fg);border:1px solid var(--border);
  border-radius:8px;padding:10px;font-size:.95rem;font-family:inherit}
textarea{font-family:Consolas,Menlo,monospace;min-height:130px;resize:vertical}
.cols{display:grid;grid-template-columns:1fr 1fr;gap:0 16px}
#toast{position:fixed;bottom:84px;left:50%;transform:translateX(-50%);background:var(--acc);color:var(--accfg);
  padding:10px 20px;border-radius:10px;font-weight:600;display:none;z-index:50;max-width:90vw;text-align:center}
.view{display:none}
.view.active{display:block}

/* ---------- vista en vivo ---------- */
#liveWrap{text-align:center;position:relative;padding:26px 10px 20px}
#val{font-family:Consolas,Menlo,monospace;font-size:clamp(4rem,16vw,9.5rem);font-weight:700;color:var(--acc);line-height:1.05}
#val.off{color:var(--dim)}
#unit{font-size:clamp(1.2rem,3vw,2rem);color:var(--dim);margin-left:8px}
.chip{position:absolute;top:10px;font-size:.72rem;border-radius:9px;padding:3px 10px;font-weight:700;display:none}
#rel{left:12px;background:var(--acc);color:var(--accfg)}
#hold{right:12px;background:var(--ok);color:#06250f}
#sesChip{display:none;margin:4px auto 0;font-size:.85rem;color:var(--acc);cursor:pointer}
.stats{display:flex;justify-content:space-around;margin-top:18px;font-family:Consolas,monospace;font-size:1rem;flex-wrap:wrap;gap:8px}
.stats .lbl{color:var(--dim);font-size:.68rem;text-transform:uppercase;text-align:center}
.stats>div{text-align:center}

/* ---------- vista medición (sesión) ---------- */
#sesTable tbody tr{cursor:pointer}
#sesTable tbody tr.cur{outline:2px solid var(--acc);outline-offset:-2px;border-radius:6px}
#sesTable tbody tr.cur td:first-child::before{content:"▶ ";color:var(--acc)}
#sesTable td.st{width:34px;text-align:center}
#sesProgress{font-size:.95rem;color:var(--dim);margin-bottom:10px}
#sesProgress b{color:var(--acc)}
#sesLive{font-family:Consolas,monospace;font-size:1.6rem;color:var(--acc);text-align:center;margin:6px 0 2px}
#sesBanner{display:none;text-align:center;padding:14px;border:1px solid var(--ok);border-radius:10px;color:var(--ok);font-weight:650;margin-bottom:12px}
.hint{color:var(--dim);font-size:.8rem;margin-top:10px;text-align:center}

/* ---------- responsive: bottom-nav en mobile ---------- */
@media (max-width:760px){
  #app{flex-direction:column}
  #side{position:fixed;bottom:0;left:0;right:0;top:auto;height:auto;width:100%;z-index:40;
    flex-direction:row;justify-content:space-around;padding:4px 4px;border-right:0;border-top:1px solid var(--border)}
  #brand,#sideFoot,#side .grow{display:none}
  .navbtn{flex-direction:column;gap:2px;padding:7px 4px;font-size:.62rem;width:auto;flex:1;align-items:center;border-radius:8px}
  .navbtn .ic{font-size:1.25rem;width:auto}
  main{padding:12px 12px 86px}
  .cols{grid-template-columns:1fr}
  #liveWrap{padding:16px 4px 12px}
}
</style>
</head>
<body data-theme="dark">
<div id="app">
<nav id="side">
  <div id="brand">&#128207; Calibre-ESP</div>
  <button class="navbtn active" data-view="live"><span class="ic">&#128200;</span>En vivo</button>
  <button class="navbtn" data-view="session"><span class="ic">&#128221;</span>Medici&oacute;n</button>
  <button class="navbtn" data-view="caps"><span class="ic">&#128229;</span>Capturas</button>
  <button class="navbtn" data-view="cfg"><span class="ic">&#9881;&#65039;</span>Config</button>
  <div class="grow"></div>
  <button class="navbtn" id="themeBtn"><span class="ic" id="themeIc">&#9728;&#65039;</span><span id="themeTxt">Modo claro</span></button>
  <div id="sideFoot">v<span id="fwv">?</span> &middot; <a href="/update">OTA</a></div>
</nav>

<main>
<div id="topbar">
  <div id="viewTitle">En vivo</div>
  <div id="badges">
    <span id="bCal" class="off">CALIBRE</span>
    <span id="bBle" class="off">BLE</span>
    <span id="bWs" class="off">LIVE</span>
  </div>
</div>

<!-- ============ EN VIVO ============ -->
<section class="view active" id="view-live">
  <div class="panel" id="liveWrap">
    <span id="rel" class="chip">REL</span><span id="hold" class="chip">HOLD</span>
    <div><span id="val" class="off">---.--</span><span id="unit">mm</span></div>
    <div id="sesChip" onclick="showView('session')">&#128221; Medici&oacute;n en curso: <b id="sesChipTxt"></b></div>
    <div class="row">
      <button class="acc" onclick="capture()">&#128229; Capturar</button>
      <button onclick="toggleZero()">Zero rel</button>
      <button onclick="toggleHold()">Hold</button>
      <button onclick="toggleUnit()">mm &#8644; in</button>
    </div>
    <div class="stats">
      <div><div class="lbl">Min</div><div id="sMin">--</div></div>
      <div><div class="lbl">Max</div><div id="sMax">--</div></div>
      <div><div class="lbl">Prom</div><div id="sAvg">--</div></div>
      <div><button onclick="resetStats()" style="padding:6px 12px;font-size:.78rem">Reset</button></div>
    </div>
  </div>
</section>

<!-- ============ MEDICIÓN (SESIÓN) ============ -->
<section class="view" id="view-session">
  <div class="panel" id="sesBuilder">
    <h2>Nueva lista de mediciones</h2>
    <label>Una medici&oacute;n por l&iacute;nea (ej.: ancho interior, alto, profundidad...)</label>
    <textarea id="sesNames" placeholder="ancho interior&#10;alto&#10;profundidad"></textarea>
    <div class="row"><button class="acc" onclick="startSession()">&#9654; Iniciar medici&oacute;n</button></div>
    <div class="hint">Claude tambi&eacute;n puede iniciar una lista autom&aacute;ticamente v&iacute;a MCP.</div>
  </div>

  <div class="panel" id="sesActive" style="display:none">
    <div id="sesBanner">&#9989; Confirmado &mdash; esperando que Claude retire las mediciones</div>
    <h2>Medici&oacute;n en curso</h2>
    <div id="sesProgress"></div>
    <div id="sesLive">---.--</div>
    <div class="hint" style="margin:0 0 8px">posicion&aacute; el calibre y apret&aacute; el bot&oacute;n &mdash; toc&aacute; una fila para repetirla</div>
    <table id="sesTable">
      <thead><tr><th>#</th><th>Medici&oacute;n</th><th style="text-align:right">Valor</th><th class="st"></th></tr></thead>
      <tbody></tbody>
    </table>
    <div class="row">
      <button class="acc" id="sesConfirmBtn" onclick="confirmSession()" disabled>&#10003; Confirmar y enviar</button>
      <button onclick="capture()">&#128229; Capturar</button>
      <button onclick="sessionCsv()">&#8681; CSV</button>
      <button class="danger" onclick="cancelSession()">Cancelar</button>
    </div>
  </div>
</section>

<!-- ============ CAPTURAS ============ -->
<section class="view" id="view-caps">
  <div class="panel">
    <h2>Capturas <span id="capCount" style="color:var(--acc)"></span></h2>
    <table id="capTable"><thead><tr><th>#</th><th>Hora</th><th style="text-align:right">Valor</th></tr></thead><tbody></tbody></table>
    <div class="row">
      <button class="acc" onclick="capture()">&#128229; Capturar</button>
      <button onclick="exportCsv()">&#8681; Exportar CSV</button>
      <button class="danger" onclick="clearCaps()">Borrar</button>
    </div>
  </div>
</section>

<!-- ============ CONFIG ============ -->
<section class="view" id="view-cfg">
  <div class="panel">
    <h2>Configuraci&oacute;n</h2>
    <form id="cfg" onsubmit="saveCfg(event)">
      <div class="cols">
        <div><label>WiFi SSID</label><input name="ssid" autocomplete="off"></div>
        <div><label>WiFi Password</label><input name="pass" type="password" autocomplete="off" placeholder="(sin cambios)"></div>
        <div><label>Nombre del dispositivo (mDNS/BLE)</label><input name="name"></div>
        <div><label>Separador decimal (teclado BLE)</label>
          <select name="sep"><option value=",">, (coma &mdash; Excel ES)</option><option value=".">. (punto)</option></select></div>
        <div><label>Tecla al final</label>
          <select name="eol"><option value="1">Enter</option><option value="2">Tab</option><option value="3">Espacio</option><option value="0">Ninguna</option></select></div>
        <div><label>Teclado BLE</label>
          <select name="ble"><option value="1">Activado</option><option value="0">Desactivado</option></select></div>
        <div><label>Modo de lectura</label>
          <select name="rmode"><option value="0">Auto-detectar</option><option value="1">Digital (3V)</option><option value="2">ADC (1.5V)</option></select></div>
        <div><label>Se&ntilde;al invertida (level-shifter NPN)</label>
          <select name="inv"><option value="0">No (conexi&oacute;n directa)</option><option value="1">S&iacute;</option></select></div>
      </div>
      <div class="row">
        <button class="acc" type="submit">Guardar</button>
        <button type="button" onclick="redetect()">Re-detectar calibre</button>
        <button type="button" onclick="reboot()">Reiniciar</button>
      </div>
    </form>
  </div>
</section>
</main>
</div>
<div id="toast"></div>

<script>
let ws, inch=false, hold=false, lastMm=null, on=false;
let stats={n:0,sum:0,min:null,max:null};
let caps=[];
let ses=null, sesPrevActive=false;
let cfgSep=',';

/* ---------- tema ---------- */
function applyTheme(t){
  document.body.dataset.theme=t;
  document.getElementById('themeIc').innerHTML = t==='dark' ? '&#9728;&#65039;' : '&#127769;';
  document.getElementById('themeTxt').textContent = t==='dark' ? 'Modo claro' : 'Modo oscuro';
  localStorage.theme=t;
}
document.getElementById('themeBtn').onclick=()=>applyTheme(document.body.dataset.theme==='dark'?'light':'dark');
applyTheme(localStorage.theme||'dark');

/* ---------- vistas ---------- */
const TITLES={live:'En vivo',session:'Medición',caps:'Capturas',cfg:'Configuración'};
function showView(v){
  document.querySelectorAll('.view').forEach(s=>s.classList.toggle('active',s.id==='view-'+v));
  document.querySelectorAll('.navbtn[data-view]').forEach(b=>b.classList.toggle('active',b.dataset.view===v));
  document.getElementById('viewTitle').textContent=TITLES[v]||v;
  localStorage.view=v;
}
document.querySelectorAll('.navbtn[data-view]').forEach(b=>b.onclick=()=>showView(b.dataset.view));
showView(localStorage.view||'live');

/* ---------- formato ---------- */
function fmt(mm){ if(mm===null||mm===undefined) return '---.--'; return inch?(mm/25.4).toFixed(4):mm.toFixed(2); }
function render(){
  const v=document.getElementById('val');
  if(!on){ v.textContent='---.--'; v.className='off'; }
  else { if(!hold) v.textContent=fmt(lastMm); v.className=''; }
  document.getElementById('unit').textContent=inch?'in':'mm';
  document.getElementById('sesLive').textContent=on?fmt(lastMm)+(inch?' in':' mm'):'---.--';
}
function renderStats(){
  document.getElementById('sMin').textContent=stats.min===null?'--':fmt(stats.min);
  document.getElementById('sMax').textContent=stats.max===null?'--':fmt(stats.max);
  document.getElementById('sAvg').textContent=stats.n?fmt(stats.sum/stats.n):'--';
}
function badge(id,onState){ document.getElementById(id).className=onState?'on':'off'; }
function toast(msg){
  const t=document.getElementById('toast');
  t.textContent=msg; t.style.display='block';
  clearTimeout(t._h); t._h=setTimeout(()=>t.style.display='none',1900);
}

/* ---------- capturas ---------- */
function renderCaps(){
  const tb=document.querySelector('#capTable tbody');
  tb.innerHTML='';
  caps.slice().reverse().forEach((c,i)=>{
    const tr=document.createElement('tr');
    tr.innerHTML=`<td>${caps.length-i}</td><td>${new Date(c.ts).toLocaleTimeString()}</td><td class="num">${fmt(c.v)} ${inch?'in':'mm'}</td>`;
    tb.appendChild(tr);
  });
  document.getElementById('capCount').textContent=caps.length?`(${caps.length})`:'';
}
function loadCaps(){
  fetch('/api/captures').then(r=>r.json()).then(j=>{ caps=j.map(c=>({v:c.v,ts:Date.now()-c.age*1000})); renderCaps(); });
}
function capture(){
  fetch('/api/capture',{method:'POST'}).then(r=>{ if(!r.ok) toast('Sin lectura del calibre'); });
}
function clearCaps(){ fetch('/api/captures',{method:'DELETE'}).then(()=>{caps=[];renderCaps();}); }
function exportCsv(){ location.href='/api/captures.csv'; }

/* ---------- sesión de medición ---------- */
function loadSession(){
  fetch('/api/session').then(r=>r.json()).then(j=>{
    ses=j;
    renderSession();
    if(j.active && !sesPrevActive){ showView('session'); toast('Nueva medición iniciada'); }
    sesPrevActive=!!j.active;
  }).catch(()=>{});
}
function renderSession(){
  const builder=document.getElementById('sesBuilder');
  const act=document.getElementById('sesActive');
  if(!ses||!ses.active){ builder.style.display='block'; act.style.display='none'; return; }
  builder.style.display='none'; act.style.display='block';

  const done=ses.items.filter(i=>i.d).length;
  document.getElementById('sesProgress').innerHTML=`<b>${done}</b> / ${ses.items.length} mediciones`;
  document.getElementById('sesBanner').style.display=ses.confirmed?'block':'none';
  document.getElementById('sesConfirmBtn').disabled=!(ses.allDone&&!ses.confirmed);

  const tb=document.querySelector('#sesTable tbody');
  tb.innerHTML='';
  ses.items.forEach((it,i)=>{
    const tr=document.createElement('tr');
    if(i===ses.current&&!ses.confirmed) tr.className='cur';
    tr.innerHTML=`<td>${i+1}</td><td>${esc(it.n)}</td><td class="num">${it.d?fmt(it.v)+(inch?' in':' mm'):'—'}</td><td class="st">${it.d?'✓':''}</td>`;
    tr.onclick=()=>{ if(!ses.confirmed) fetch('/api/session/select',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({index:i})}); };
    tb.appendChild(tr);
  });
}
function esc(s){ const d=document.createElement('div'); d.textContent=s; return d.innerHTML; }
function startSession(){
  const names=document.getElementById('sesNames').value.split('\n').map(s=>s.trim()).filter(s=>s.length);
  if(!names.length){ toast('Escribí al menos una medición'); return; }
  fetch('/api/session',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({items:names})})
    .then(r=>{ if(!r.ok) toast('No se pudo iniciar'); });
}
function confirmSession(){
  fetch('/api/session/confirm',{method:'POST'}).then(r=>{ if(!r.ok) toast('Faltan mediciones'); });
}
function cancelSession(){ fetch('/api/session',{method:'DELETE'}); }
function sessionCsv(){
  if(!ses||!ses.items) return;
  let csv='medicion;mm\r\n';
  ses.items.forEach(it=>{
    let v=it.d?it.v.toFixed(2):'';
    if(cfgSep===',') v=v.replace('.',',');
    csv+=`${it.n.replace(/;/g,',')};${v}\r\n`;
  });
  const a=document.createElement('a');
  a.href=URL.createObjectURL(new Blob([csv],{type:'text/csv'}));
  a.download='mediciones.csv';
  a.click();
}
function renderSesChip(){
  const chip=document.getElementById('sesChip');
  if(ses&&ses.active&&!ses.confirmed){
    const cur=ses.items[ses.current];
    document.getElementById('sesChipTxt').textContent=`${ses.items.filter(i=>i.d).length}/${ses.items.length} — ${cur?cur.n:''}`;
    chip.style.display='block';
  } else chip.style.display='none';
}

/* ---------- websocket ---------- */
function connect(){
  ws=new WebSocket(`ws://${location.host}/ws`);
  ws.onopen=()=>badge('bWs',true);
  ws.onclose=()=>{ badge('bWs',false); setTimeout(connect,1500); };
  ws.onmessage=e=>{
    const m=JSON.parse(e.data);
    if(m.t==='r'){
      on=m.on;
      if(m.on){
        lastMm=m.v;
        stats.n++; stats.sum+=m.v;
        if(stats.min===null||m.v<stats.min)stats.min=m.v;
        if(stats.max===null||m.v>stats.max)stats.max=m.v;
        renderStats();
      }
      badge('bCal',m.on);
      if(m.rel!==undefined)document.getElementById('rel').style.display=m.rel?'inline':'none';
      render();
    } else if(m.t==='cap'){
      caps.push({v:m.v,ts:Date.now()});
      if(caps.length>200)caps.shift();
      renderCaps(); toast(`Capturado: ${fmt(m.v)} ${inch?'in':'mm'}`);
    } else if(m.t==='ses'){
      loadSession();
    } else if(m.t==='st'){
      badge('bBle',m.ble);
      if(m.rel!==undefined)document.getElementById('rel').style.display=m.rel?'inline':'none';
      if(m.on!==undefined){ on=m.on; badge('bCal',m.on); render(); }
    }
  };
}

/* ---------- acciones live ---------- */
function toggleZero(){ fetch('/api/zero',{method:'POST'}); }
function toggleHold(){
  hold=!hold;
  document.getElementById('hold').style.display=hold?'inline':'none';
  render();
}
function toggleUnit(){ inch=!inch; render(); renderStats(); renderCaps(); renderSession(); }
function resetStats(){ stats={n:0,sum:0,min:null,max:null}; renderStats(); }
function redetect(){ fetch('/api/redetect',{method:'POST'}); toast('Re-detectando señal...'); }
function reboot(){ fetch('/api/reboot',{method:'POST'}); toast('Reiniciando...'); }

/* ---------- config ---------- */
function loadCfg(){
  fetch('/api/config').then(r=>r.json()).then(j=>{
    const f=document.getElementById('cfg');
    f.ssid.value=j.ssid; f.name.value=j.name; f.sep.value=j.sep;
    f.eol.value=j.eol; f.ble.value=j.ble?1:0; f.rmode.value=j.rmode; f.inv.value=j.inv?1:0;
    cfgSep=j.sep;
    document.getElementById('fwv').textContent=j.fw;
  });
}
function saveCfg(ev){
  ev.preventDefault();
  const f=ev.target;
  const body={ssid:f.ssid.value,pass:f.pass.value,name:f.name.value,sep:f.sep.value,
              eol:+f.eol.value,ble:f.ble.value==='1',rmode:+f.rmode.value,inv:f.inv.value==='1'};
  cfgSep=f.sep.value;
  fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)})
    .then(()=>toast('Guardado. Reiniciá para aplicar WiFi/BLE.'));
}

/* ---------- refresco del chip de sesión en vivo ---------- */
const _renderSession=renderSession;
renderSession=function(){ _renderSession(); renderSesChip(); };

connect(); loadCaps(); loadCfg(); loadSession();
</script>
</body>
</html>)rawliteral";
