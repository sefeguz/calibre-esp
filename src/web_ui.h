// UI web embebida en flash (PROGMEM) — single page, sin filesystem.
#pragma once

#include <pgmspace.h>

static const char WEB_UI_HTML[] PROGMEM = R"rawliteral(<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Calibre-ESP</title>
<style>
:root { --bg:#14161a; --panel:#1d2026; --fg:#e8e8e8; --dim:#8a8f98; --acc:#ff8b1f; --ok:#3ecf6e; --bad:#e0455a; }
* { box-sizing:border-box; margin:0; padding:0; }
body { background:var(--bg); color:var(--fg); font-family:system-ui,Segoe UI,Roboto,sans-serif; max-width:760px; margin:0 auto; padding:12px; }
header { display:flex; justify-content:space-between; align-items:center; padding:4px 2px 12px; }
header h1 { font-size:1.15rem; color:var(--acc); letter-spacing:.5px; }
#badges span { font-size:.72rem; padding:3px 8px; border-radius:10px; background:var(--panel); color:var(--dim); margin-left:4px; }
#badges span.on { color:var(--ok); }
#badges span.off { color:var(--bad); }
.panel { background:var(--panel); border-radius:12px; padding:14px; margin-bottom:12px; }
#disp { text-align:center; position:relative; }
#val { font-family:Consolas,Menlo,monospace; font-size:clamp(3rem,14vw,5.2rem); font-weight:700; color:var(--acc); line-height:1.1; }
#val.off { color:var(--dim); }
#unit { font-size:1.2rem; color:var(--dim); margin-left:6px; }
#rel { position:absolute; top:8px; left:12px; font-size:.7rem; color:var(--bg); background:var(--acc); border-radius:8px; padding:2px 8px; display:none; }
#hold { position:absolute; top:8px; right:12px; font-size:.7rem; color:var(--bg); background:var(--ok); border-radius:8px; padding:2px 8px; display:none; }
.row { display:flex; gap:8px; flex-wrap:wrap; justify-content:center; margin-top:12px; }
button { background:#2a2e36; color:var(--fg); border:0; border-radius:8px; padding:10px 16px; font-size:.95rem; cursor:pointer; }
button:active { transform:scale(.97); }
button.acc { background:var(--acc); color:#1a1208; font-weight:600; }
.stats { display:flex; justify-content:space-around; margin-top:12px; font-family:Consolas,monospace; font-size:.95rem; }
.stats div { text-align:center; }
.stats .lbl { color:var(--dim); font-size:.7rem; text-transform:uppercase; }
h2 { font-size:.95rem; color:var(--dim); margin-bottom:10px; text-transform:uppercase; letter-spacing:.5px; }
table { width:100%; border-collapse:collapse; font-size:.9rem; }
td,th { padding:6px 8px; text-align:left; border-bottom:1px solid #2a2e36; }
th { color:var(--dim); font-weight:500; font-size:.75rem; text-transform:uppercase; }
td.num { font-family:Consolas,monospace; text-align:right; }
label { display:block; color:var(--dim); font-size:.8rem; margin:10px 0 4px; }
input,select { width:100%; background:#13151a; color:var(--fg); border:1px solid #2a2e36; border-radius:6px; padding:9px 10px; font-size:.95rem; }
.cols { display:grid; grid-template-columns:1fr 1fr; gap:0 14px; }
@media (max-width:520px){ .cols{grid-template-columns:1fr} }
footer { text-align:center; color:var(--dim); font-size:.75rem; padding:10px 0 16px; }
footer a { color:var(--dim); }
#toast { position:fixed; bottom:18px; left:50%; transform:translateX(-50%); background:var(--acc); color:#1a1208; padding:9px 18px; border-radius:8px; font-weight:600; display:none; }
</style>
</head>
<body>
<header>
  <h1>&#128207; Calibre-ESP</h1>
  <div id="badges">
    <span id="bCal" class="off">CALIBRE</span>
    <span id="bBle" class="off">BLE</span>
    <span id="bWs" class="off">LIVE</span>
  </div>
</header>

<div class="panel" id="disp">
  <span id="rel">REL</span><span id="hold">HOLD</span>
  <div><span id="val" class="off">---.--</span><span id="unit">mm</span></div>
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
    <div><button onclick="resetStats()" style="padding:6px 10px;font-size:.8rem">Reset</button></div>
  </div>
</div>

<div class="panel">
  <h2>Capturas <span id="capCount" style="color:var(--acc)"></span></h2>
  <table id="capTable"><thead><tr><th>#</th><th>Hora</th><th style="text-align:right">Valor</th></tr></thead><tbody></tbody></table>
  <div class="row">
    <button onclick="exportCsv()">&#8681; Exportar CSV</button>
    <button onclick="clearCaps()">Borrar</button>
  </div>
</div>

<div class="panel">
  <h2>Configuraci&oacute;n</h2>
  <form id="cfg" onsubmit="saveCfg(event)">
    <div class="cols">
      <div><label>WiFi SSID</label><input name="ssid" autocomplete="off"></div>
      <div><label>WiFi Password</label><input name="pass" type="password" autocomplete="off"></div>
      <div><label>Nombre del dispositivo (mDNS/BLE)</label><input name="name"></div>
      <div><label>Separador decimal (teclado BLE)</label>
        <select name="sep"><option value=",">, (coma — Excel ES)</option><option value=".">. (punto)</option></select></div>
      <div><label>Tecla al final</label>
        <select name="eol"><option value="1">Enter</option><option value="2">Tab</option><option value="0">Ninguna</option></select></div>
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

<footer>
  Calibre-ESP <span id="fwv"></span> &middot; modo lectura: <span id="fMode">?</span> &middot; <a href="/update">OTA</a>
</footer>
<div id="toast"></div>

<script>
let ws, inch=false, hold=false, lastMm=null, on=false;
let stats={n:0,sum:0,min:null,max:null};
let caps=[];

function fmt(mm){
  if(mm===null) return '---.--';
  return inch ? (mm/25.4).toFixed(4) : mm.toFixed(2);
}
function render(){
  const v=document.getElementById('val');
  if(!on){ v.textContent='---.--'; v.className='off'; return; }
  if(!hold){ v.textContent=fmt(lastMm); }
  v.className='';
  document.getElementById('unit').textContent=inch?'in':'mm';
}
function renderStats(){
  document.getElementById('sMin').textContent=stats.min===null?'--':fmt(stats.min);
  document.getElementById('sMax').textContent=stats.max===null?'--':fmt(stats.max);
  document.getElementById('sAvg').textContent=stats.n?fmt(stats.sum/stats.n):'--';
}
function renderCaps(){
  const tb=document.querySelector('#capTable tbody');
  tb.innerHTML='';
  caps.slice().reverse().forEach((c,i)=>{
    const tr=document.createElement('tr');
    const d=new Date(c.ts);
    tr.innerHTML=`<td>${caps.length-i}</td><td>${d.toLocaleTimeString()}</td><td class="num">${fmt(c.v)} ${inch?'in':'mm'}</td>`;
    tb.appendChild(tr);
  });
  document.getElementById('capCount').textContent=caps.length?`(${caps.length})`:'';
}
function badge(id,onState){ const b=document.getElementById(id); b.className=onState?'on':'off'; }

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
      if(caps.length>200)caps.shift(); // espejo del ring del firmware (CAPTURES_SIZE)
      renderCaps(); toast(`Capturado: ${fmt(m.v)} ${inch?'in':'mm'}`);
    } else if(m.t==='st'){
      badge('bBle',m.ble);
      document.getElementById('fMode').textContent=['detectando','digital','ADC'][m.mode]||'?';
      if(m.rel!==undefined)document.getElementById('rel').style.display=m.rel?'inline':'none';
      if(m.on!==undefined){ on=m.on; badge('bCal',m.on); render(); }
    }
  };
}
function toast(msg){
  const t=document.getElementById('toast');
  t.textContent=msg; t.style.display='block';
  clearTimeout(t._h); t._h=setTimeout(()=>t.style.display='none',1800);
}
function capture(){ fetch('/api/capture',{method:'POST'}); }
function toggleZero(){ fetch('/api/zero',{method:'POST'}); }
function toggleHold(){
  hold=!hold;
  document.getElementById('hold').style.display=hold?'inline':'none';
  render();
}
function toggleUnit(){ inch=!inch; render(); renderStats(); renderCaps(); }
function resetStats(){ stats={n:0,sum:0,min:null,max:null}; renderStats(); }
function clearCaps(){ fetch('/api/captures',{method:'DELETE'}).then(()=>{caps=[];renderCaps();}); }
function exportCsv(){ location.href='/api/captures.csv'; }

function loadCaps(){
  // el server reporta la edad en segundos; convertir a hora local del navegador
  fetch('/api/captures').then(r=>r.json()).then(j=>{ caps=j.map(c=>({v:c.v,ts:Date.now()-c.age*1000})); renderCaps(); });
}
function loadCfg(){
  fetch('/api/config').then(r=>r.json()).then(j=>{
    const f=document.getElementById('cfg');
    f.ssid.value=j.ssid; f.name.value=j.name; f.sep.value=j.sep;
    f.eol.value=j.eol; f.ble.value=j.ble?1:0; f.rmode.value=j.rmode; f.inv.value=j.inv?1:0;
    document.getElementById('fwv').textContent='v'+j.fw;
  });
}
function saveCfg(ev){
  ev.preventDefault();
  const f=ev.target;
  const body={ssid:f.ssid.value,pass:f.pass.value,name:f.name.value,sep:f.sep.value,
              eol:+f.eol.value,ble:f.ble.value==='1',rmode:+f.rmode.value,inv:f.inv.value==='1'};
  fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)})
    .then(()=>toast('Guardado. Reiniciá para aplicar WiFi/BLE.'));
}
function reboot(){ fetch('/api/reboot',{method:'POST'}); toast('Reiniciando...'); }
function redetect(){ fetch('/api/redetect',{method:'POST'}); toast('Re-detectando señal...'); }

connect(); loadCaps(); loadCfg();
</script>
</body>
</html>)rawliteral";
