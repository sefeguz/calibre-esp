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
  --acc:#4c8dff; --accfg:#ffffff; --ok:#3ecf6e; --bad:#e0455a; --border:#2f343d;
}
body[data-theme="light"]{
  --bg:#eef0f3; --panel:#ffffff; --panel2:#e7eaee; --fg:#1b1e24; --dim:#667085;
  --acc:#2563eb; --accfg:#ffffff; --ok:#179a4a; --bad:#cf3349; --border:#d9dde3;
}
*{box-sizing:border-box;margin:0;padding:0}
html,body{height:100%}
body{background:var(--bg);color:var(--fg);font-family:system-ui,Segoe UI,Roboto,sans-serif;transition:background .2s,color .2s}
#app{display:flex;min-height:100vh}

/* ---------- sidebar ---------- */
#side{width:210px;flex-shrink:0;background:var(--panel);border-right:1px solid var(--border);
  display:flex;flex-direction:column;padding:14px 10px;gap:4px;position:sticky;top:0;height:100vh}
#brand{font-size:1.05rem;font-weight:700;color:var(--acc);padding:6px 10px 16px;letter-spacing:.5px;display:flex;align-items:center;gap:8px}
/* íconos Lucide inline: trazos que siguen el color del texto/tema */
svg.lic{width:1.1em;height:1.1em;fill:none;stroke:currentColor;stroke-width:2;
  stroke-linecap:round;stroke-linejoin:round;vertical-align:-.18em;flex-shrink:0}
#brand svg.lic{width:1.25em;height:1.25em}
.navbtn{display:flex;align-items:center;gap:10px;width:100%;padding:11px 12px;border:0;border-radius:10px;
  background:transparent;color:var(--dim);font-size:.95rem;cursor:pointer;text-align:left}
.navbtn svg.ic{width:20px;height:20px}
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
  .navbtn svg.ic{width:22px;height:22px}
  main{padding:12px 12px 86px}
  .cols{grid-template-columns:1fr}
  #liveWrap{padding:16px 4px 12px}
}
</style>
</head>
<body data-theme="dark">
<svg style="display:none" xmlns="http://www.w3.org/2000/svg">
  <symbol id="i-ruler" viewBox="0 0 24 24"><path d="M21.3 15.3a2.4 2.4 0 0 1 0 3.4l-2.6 2.6a2.4 2.4 0 0 1-3.4 0L2.7 8.7a2.4 2.4 0 0 1 0-3.4l2.6-2.6a2.4 2.4 0 0 1 3.4 0Z"/><path d="m14.5 12.5 2-2"/><path d="m11.5 9.5 2-2"/><path d="m8.5 6.5 2-2"/><path d="m17.5 15.5 2-2"/></symbol>
  <symbol id="i-activity" viewBox="0 0 24 24"><path d="M22 12h-4l-3 9L9 3l-3 9H2"/></symbol>
  <symbol id="i-clip" viewBox="0 0 24 24"><rect x="8" y="2" width="8" height="4" rx="1"/><path d="M16 4h2a2 2 0 0 1 2 2v14a2 2 0 0 1-2 2H6a2 2 0 0 1-2-2V6a2 2 0 0 1 2-2h2"/><path d="M12 11h4"/><path d="M12 16h4"/><path d="M8 11h.01"/><path d="M8 16h.01"/></symbol>
  <symbol id="i-download" viewBox="0 0 24 24"><path d="M21 15v4a2 2 0 0 1-2 2H5a2 2 0 0 1-2-2v-4"/><path d="m7 10 5 5 5-5"/><path d="M12 15V3"/></symbol>
  <symbol id="i-sliders" viewBox="0 0 24 24"><path d="M21 4h-7"/><path d="M10 4H3"/><path d="M21 12h-9"/><path d="M8 12H3"/><path d="M21 20h-5"/><path d="M12 20H3"/><path d="M14 2v4"/><path d="M8 10v4"/><path d="M16 18v4"/></symbol>
  <symbol id="i-sun" viewBox="0 0 24 24"><circle cx="12" cy="12" r="4"/><path d="M12 2v2"/><path d="M12 20v2"/><path d="m4.93 4.93 1.41 1.41"/><path d="m17.66 17.66 1.41 1.41"/><path d="M2 12h2"/><path d="M20 12h2"/><path d="m6.34 17.66-1.41 1.41"/><path d="m19.07 4.93-1.41 1.41"/></symbol>
  <symbol id="i-moon" viewBox="0 0 24 24"><path d="M12 3a6 6 0 0 0 9 9 9 9 0 1 1-9-9Z"/></symbol>
  <symbol id="i-capture" viewBox="0 0 24 24"><circle cx="12" cy="12" r="10"/><path d="M8 12h8"/><path d="M12 8v8"/></symbol>
  <symbol id="i-check" viewBox="0 0 24 24"><path d="M20 6 9 17l-5-5"/></symbol>
  <symbol id="i-play" viewBox="0 0 24 24"><path d="M6 3v18l14-9z"/></symbol>
  <symbol id="i-trash" viewBox="0 0 24 24"><path d="M3 6h18"/><path d="M19 6v14a2 2 0 0 1-2 2H7a2 2 0 0 1-2-2V6"/><path d="M8 6V4a2 2 0 0 1 2-2h4a2 2 0 0 1 2 2v2"/><path d="M10 11v6"/><path d="M14 11v6"/></symbol>
  <symbol id="i-x" viewBox="0 0 24 24"><path d="M18 6 6 18"/><path d="m6 6 12 12"/></symbol>
  <symbol id="i-help" viewBox="0 0 24 24"><circle cx="12" cy="12" r="10"/><path d="M9.09 9a3 3 0 0 1 5.83 1c0 2-3 3-3 3"/><path d="M12 17h.01"/></symbol>
  <symbol id="i-copy" viewBox="0 0 24 24"><rect x="9" y="9" width="13" height="13" rx="2"/><path d="M5 15H4a2 2 0 0 1-2-2V4a2 2 0 0 1 2-2h9a2 2 0 0 1 2 2v1"/></symbol>
  <symbol id="i-lock" viewBox="0 0 24 24"><rect x="3" y="11" width="18" height="11" rx="2"/><path d="M7 11V7a5 5 0 0 1 10 0v4"/></symbol>
  <symbol id="i-wifi" viewBox="0 0 24 24"><path d="M12 20h.01"/><path d="M8.5 16.5a5 5 0 0 1 7 0"/><path d="M5 13a10 10 0 0 1 14 0"/><path d="M2 9.5a15 15 0 0 1 20 0"/></symbol>
  <symbol id="i-eye" viewBox="0 0 24 24"><path d="M2 12s3-7 10-7 10 7 10 7-3 7-10 7-10-7-10-7Z"/><circle cx="12" cy="12" r="3"/></symbol>
  <symbol id="i-eyeoff" viewBox="0 0 24 24"><path d="M9.88 9.88a3 3 0 0 0 4.24 4.24"/><path d="M10.73 5.08A10.43 10.43 0 0 1 12 5c7 0 10 7 10 7a13.16 13.16 0 0 1-1.67 2.68"/><path d="M6.61 6.61A13.526 13.526 0 0 0 2 12s3 7 10 7a9.74 9.74 0 0 0 5.39-1.61"/><path d="m2 2 20 20"/></symbol>
  <symbol id="i-pencil" viewBox="0 0 24 24"><path d="M12 20h9"/><path d="M16.5 3.5a2.12 2.12 0 0 1 3 3L7 19l-4 1 1-4Z"/></symbol>
</svg>
<div id="app">
<nav id="side">
  <div id="brand"><svg class="lic"><use href="#i-ruler"/></svg>Calibre-ESP</div>
  <button class="navbtn active" data-view="live"><svg class="ic lic"><use href="#i-activity"/></svg>En vivo</button>
  <button class="navbtn" data-view="session"><svg class="ic lic"><use href="#i-clip"/></svg>Medici&oacute;n</button>
  <button class="navbtn" data-view="caps"><svg class="ic lic"><use href="#i-download"/></svg>Capturas</button>
  <button class="navbtn" data-view="cfg"><svg class="ic lic"><use href="#i-sliders"/></svg>Config</button>
  <button class="navbtn" data-view="help"><svg class="ic lic"><use href="#i-help"/></svg>Ayuda</button>
  <div class="grow"></div>
  <button class="navbtn" id="themeBtn"><svg class="ic lic"><use id="themeUse" href="#i-sun"/></svg><span id="themeTxt">Modo claro</span></button>
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
    <div id="sesChip" onclick="showView('session')"><svg class="lic"><use href="#i-clip"/></svg> Medici&oacute;n en curso: <b id="sesChipTxt"></b></div>
    <div class="row">
      <button class="acc" onclick="capture()"><svg class="lic"><use href="#i-capture"/></svg> Capturar</button>
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
    <div class="row"><button class="acc" onclick="startSession()"><svg class="lic"><use href="#i-play"/></svg> Iniciar medici&oacute;n</button></div>
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
      <button class="acc" id="sesConfirmBtn" onclick="confirmSession()" disabled><svg class="lic"><use href="#i-check"/></svg> Confirmar y enviar</button>
      <button onclick="capture()"><svg class="lic"><use href="#i-capture"/></svg> Capturar</button>
      <button onclick="sessionCsv()"><svg class="lic"><use href="#i-download"/></svg> CSV</button>
      <button class="danger" onclick="cancelSession()"><svg class="lic"><use href="#i-x"/></svg> Cancelar</button>
    </div>
  </div>
</section>

<!-- ============ CAPTURAS ============ -->
<section class="view" id="view-caps">
  <div class="panel">
    <h2>Capturas <span id="capCount" style="color:var(--acc)"></span></h2>
    <table id="capTable"><thead><tr><th>#</th><th>Hora</th><th style="text-align:right">Valor</th></tr></thead><tbody></tbody></table>
    <div class="row">
      <button class="acc" onclick="capture()"><svg class="lic"><use href="#i-capture"/></svg> Capturar</button>
      <button onclick="exportCsv()"><svg class="lic"><use href="#i-download"/></svg> Exportar CSV</button>
      <button class="danger" onclick="clearCaps()"><svg class="lic"><use href="#i-trash"/></svg> Borrar</button>
    </div>
  </div>
</section>

<!-- ============ CONFIG ============ -->
<section class="view" id="view-cfg">
  <div class="panel">
    <h2>Configuraci&oacute;n</h2>
    <form id="cfg" onsubmit="saveCfg(event)">
      <label>Redes WiFi guardadas (m&aacute;x 10) &mdash; se conecta a la de mejor se&ntilde;al que encuentre</label>
      <table id="wifiTable"><tbody></tbody></table>
      <div class="row" style="justify-content:flex-start;margin-top:8px">
        <button type="button" onclick="scanWifi()" id="scanBtn"><svg class="lic"><use href="#i-wifi"/></svg> Buscar redes</button>
      </div>
      <div id="scanList" style="display:none;margin-top:6px;border:1px solid var(--border);border-radius:8px;overflow:hidden"></div>
      <div style="margin-top:10px;border:1px solid var(--border);border-radius:10px;padding:12px">
        <div id="wifiFormTitle" style="font-size:.82rem;color:var(--dim);margin-bottom:8px">Agregar red</div>
        <div class="cols">
          <div><label style="margin-top:0">SSID</label><input id="wifiSsidNew" placeholder="SSID (o eleg&iacute; de la lista)" autocomplete="off"></div>
          <div><label style="margin-top:0">Password</label>
            <div style="display:flex;gap:6px">
              <input id="wifiPassNew" type="password" placeholder="Password" autocomplete="off">
              <button type="button" id="pwEye" title="Mostrar/ocultar" onclick="togglePw()" style="flex-shrink:0;padding:10px 12px"><svg class="lic"><use href="#i-eye" id="pwEyeUse"/></svg></button>
            </div>
          </div>
          <div><label style="margin-top:0">Direcci&oacute;n IP</label>
            <select id="wifiMode" onchange="onModeChange()"><option value="dhcp">Autom&aacute;tica (DHCP)</option><option value="static">Manual (IP fija)</option></select>
          </div>
        </div>
        <div id="wifiStatic" style="display:none">
          <div class="cols">
            <div><label>IP</label><input id="wifiIp" placeholder="192.168.1.50" autocomplete="off"></div>
            <div><label>Gateway</label><input id="wifiGw" placeholder="192.168.1.1" autocomplete="off"></div>
            <div><label>M&aacute;scara</label><input id="wifiSn" placeholder="255.255.255.0" autocomplete="off"></div>
            <div><label>DNS</label><input id="wifiDns" placeholder="192.168.1.1 (opcional)" autocomplete="off"></div>
          </div>
        </div>
        <div class="row" style="justify-content:flex-start;margin-top:10px">
          <button type="button" onclick="addWifi()"><svg class="lic"><use href="#i-check"/></svg> <span id="wifiAddLabel">Agregar red</span></button>
          <button type="button" id="wifiCancelEdit" onclick="resetWifiForm()" style="display:none">Cancelar</button>
        </div>
      </div>
      <div class="cols" style="margin-top:6px">
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

<!-- ============ AYUDA ============ -->
<section class="view" id="view-help">
  <div class="panel">
    <h2>Uso r&aacute;pido</h2>
    <table>
      <tr><td><b>Bot&oacute;n f&iacute;sico (corto)</b></td><td>Captura la medici&oacute;n: la tipea por Bluetooth en la PC/celular emparejado y la guarda en Capturas. Si hay una medici&oacute;n guiada en curso, llena la fila actual y avanza.</td></tr>
      <tr><td><b>Bot&oacute;n f&iacute;sico (largo 1.5s)</b></td><td>Zero relativo: medir diferencias respecto de la posici&oacute;n actual.</td></tr>
      <tr><td><b>Medici&oacute;n guiada</b></td><td>En la vista Medici&oacute;n escrib&iacute;s la lista (una por l&iacute;nea) e inici&aacute;s. Cada captura llena la fila actual. Toc&aacute; una fila para repetirla. Al completar todas, Confirmar.</td></tr>
      <tr><td><b>Teclado Bluetooth</b></td><td>Emparej&aacute; "Calibre-ESP" desde la PC/celu. Cada captura tipea el valor + la tecla final configurada (Enter/Tab/Espacio). El separador decimal se elige en Config.</td></tr>
      <tr><td><b>Hold / mm&#8644;in</b></td><td>Solo afectan lo que se muestra en pantalla, no lo que se captura.</td></tr>
      <tr><td><b>Sin lectura</b></td><td>Verific&aacute; que el calibre est&eacute; conectado y encendido. El badge CALIBRE en verde indica se&ntilde;al OK. En Config pod&eacute;s re-detectar.</td></tr>
    </table>
  </div>
  <div class="panel">
    <h2>Para Claude / asistentes IA</h2>
    <p style="font-size:.92rem;line-height:1.5;color:var(--dim)">
      Este dispositivo es usable por una IA: con el <b>MCP "calibre"</b> (en el repo,
      <code style="color:var(--fg)">mcp/calibre_mcp.py</code>) Claude puede leer mediciones en vivo y pedir
      listas de mediciones que el usuario completa con el bot&oacute;n. Sin MCP, toda la
      funcionalidad est&aacute; disponible por API REST.
      La gu&iacute;a completa para IAs vive en
      <a href="/llms.txt" style="color:var(--acc)">/llms.txt</a> &mdash; si us&aacute;s un asistente sin acceso
      a este equipo, copi&aacute;le la gu&iacute;a y va a saber exactamente c&oacute;mo operarlo.
    </p>
    <div class="row">
      <button class="acc" onclick="copyLlms()"><svg class="lic"><use href="#i-copy"/></svg> Copiar gu&iacute;a para IA</button>
      <button onclick="location.href='/llms.txt'"><svg class="lic"><use href="#i-download"/></svg> Ver /llms.txt</button>
    </div>
  </div>
  <div class="panel">
    <h2>Enlaces</h2>
    <p style="font-size:.9rem;color:var(--dim)">
      Firmware v<span id="fwv2">?</span> &middot; <a href="/update" style="color:var(--acc)">Actualizaci&oacute;n OTA</a> &middot;
      <a href="https://github.com/sefeguz/calibre-esp" style="color:var(--acc)">GitHub</a> &middot;
      API: <code style="color:var(--fg)">/api/status</code>, <code style="color:var(--fg)">/api/value</code>
    </p>
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
  document.getElementById('themeUse').setAttribute('href', t==='dark' ? '#i-sun' : '#i-moon');
  document.getElementById('themeTxt').textContent = t==='dark' ? 'Modo claro' : 'Modo oscuro';
  localStorage.theme=t;
}
document.getElementById('themeBtn').onclick=()=>applyTheme(document.body.dataset.theme==='dark'?'light':'dark');
applyTheme(localStorage.theme||'dark');

/* ---------- vistas ---------- */
const TITLES={live:'En vivo',session:'Medición',caps:'Capturas',cfg:'Configuración',help:'Ayuda'};
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
let wsReconnectT=null;
function connect(){
  // no abrir una conexión nueva si ya hay uno conectando/abierto: reconexiones
  // solapadas dejarían varios WebSocket vivos y saturarían el equipo
  if(ws && (ws.readyState===0 || ws.readyState===1)) return;
  clearTimeout(wsReconnectT);
  try{ if(ws) ws.close(); }catch(e){}
  ws=new WebSocket(`ws://${location.host}/ws`);
  ws.onopen=()=>badge('bWs',true);
  ws.onclose=()=>{ badge('bWs',false); clearTimeout(wsReconnectT); wsReconnectT=setTimeout(connect,1500); };
  ws.onerror=()=>{ try{ws.close();}catch(e){} };
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
function copyLlms(){
  fetch('/llms.txt').then(r=>r.text()).then(t=>{
    const base=`Guía del dispositivo Calibre-ESP en http://${location.host}\n\n`;
    return navigator.clipboard.writeText(base+t);
  }).then(()=>toast('Guía copiada — pegásela a tu asistente'))
    .catch(()=>{ location.href='/llms.txt'; });
}

/* ---------- config ---------- */
let cfgWifi=[];   // [{ssid, pass|null, static, ip, gw, sn, dns}] — pass null = conservar
let wifiEditIdx=-1;
function renderWifi(){
  const tb=document.querySelector('#wifiTable tbody');
  tb.innerHTML='';
  cfgWifi.forEach((w,i)=>{
    const mode=w.static?`IP: ${esc(w.ip||'?')}`:'DHCP';
    const tr=document.createElement('tr');
    tr.innerHTML=`<td>${esc(w.ssid)}${w.pass!==null&&w.pass!==undefined?' <span style="color:var(--ok);font-size:.72rem">(nueva)</span>':''}
        <span style="color:var(--dim);font-size:.72rem">— ${mode}</span></td>
      <td style="width:78px;text-align:right;white-space:nowrap">
        <button type="button" title="Editar" style="padding:4px 8px" onclick="editWifi(${i})"><svg class="lic"><use href="#i-pencil"/></svg></button>
        <button type="button" title="Quitar" style="padding:4px 8px;color:var(--bad)" onclick="delWifi(${i})">&#10005;</button></td>`;
    tb.appendChild(tr);
  });
  if(!cfgWifi.length){
    tb.innerHTML='<tr><td style="color:var(--dim)">sin redes guardadas — el equipo queda en modo hotspot</td></tr>';
  }
}
function togglePw(){
  const p=document.getElementById('wifiPassNew');
  const show=p.type==='password';
  p.type=show?'text':'password';
  document.getElementById('pwEyeUse').setAttribute('href',show?'#i-eyeoff':'#i-eye');
}
function onModeChange(){
  document.getElementById('wifiStatic').style.display=
    document.getElementById('wifiMode').value==='static'?'block':'none';
}
function resetWifiForm(){
  wifiEditIdx=-1;
  ['wifiSsidNew','wifiPassNew','wifiIp','wifiGw','wifiSn','wifiDns'].forEach(id=>document.getElementById(id).value='');
  document.getElementById('wifiMode').value='dhcp'; onModeChange();
  document.getElementById('wifiFormTitle').textContent='Agregar red';
  document.getElementById('wifiAddLabel').textContent='Agregar red';
  document.getElementById('wifiCancelEdit').style.display='none';
}
function editWifi(i){
  const w=cfgWifi[i]; wifiEditIdx=i;
  document.getElementById('wifiSsidNew').value=w.ssid;
  document.getElementById('wifiPassNew').value='';
  document.getElementById('wifiPassNew').placeholder='(sin cambios)';
  document.getElementById('wifiMode').value=w.static?'static':'dhcp'; onModeChange();
  document.getElementById('wifiIp').value=w.ip||'';
  document.getElementById('wifiGw').value=w.gw||'';
  document.getElementById('wifiSn').value=w.sn||'';
  document.getElementById('wifiDns').value=w.dns||'';
  document.getElementById('wifiFormTitle').textContent='Editar red';
  document.getElementById('wifiAddLabel').textContent='Guardar cambios';
  document.getElementById('wifiCancelEdit').style.display='';
}
function addWifi(silent){
  const ssid=document.getElementById('wifiSsidNew').value.trim();
  if(!ssid){ if(!silent) toast('Escribí el SSID'); return; }
  const isStatic=document.getElementById('wifiMode').value==='static';
  const ip=document.getElementById('wifiIp').value.trim();
  if(isStatic && !ipOk(ip)){ if(!silent) toast('IP fija inválida'); return; }
  const pass=document.getElementById('wifiPassNew').value;
  const net={ssid:ssid,static:isStatic,
             ip:ip,gw:document.getElementById('wifiGw').value.trim(),
             sn:document.getElementById('wifiSn').value.trim(),
             dns:document.getElementById('wifiDns').value.trim()};
  if(wifiEditIdx>=0){
    net.pass=pass.length?pass:cfgWifi[wifiEditIdx].pass; // vacío = conservar
    cfgWifi[wifiEditIdx]=net;
  } else {
    if(cfgWifi.length>=10){ if(!silent) toast('Máximo 10 redes'); return; }
    const ex=cfgWifi.findIndex(w=>w.ssid===ssid);
    net.pass=pass;
    if(ex>=0) cfgWifi[ex]=net; else cfgWifi.push(net);
  }
  resetWifiForm(); renderWifi();
}
function ipOk(s){ return /^(\d{1,3}\.){3}\d{1,3}$/.test(s) && s.split('.').every(o=>+o<=255); }
function delWifi(i){ cfgWifi.splice(i,1); if(wifiEditIdx===i)resetWifiForm(); renderWifi(); }
let scanTries=0;
function scanWifi(){
  const btn=document.getElementById('scanBtn'), list=document.getElementById('scanList');
  btn.disabled=true; list.style.display='block';
  list.innerHTML='<div style="padding:10px;color:var(--dim)">Buscando redes...</div>';
  scanTries=0; pollScan();
}
function pollScan(){
  fetch('/api/wifi/scan').then(r=>r.json()).then(j=>{
    const btn=document.getElementById('scanBtn'), list=document.getElementById('scanList');
    if(j.scanning){
      if(++scanTries>15){ list.innerHTML='<div style="padding:10px;color:var(--bad)">No se pudo escanear</div>'; btn.disabled=false; return; }
      setTimeout(pollScan,1200); return;
    }
    btn.disabled=false;
    const nets=j.networks||[];
    if(!nets.length){ list.innerHTML='<div style="padding:10px;color:var(--dim)">No se encontraron redes</div>'; return; }
    list.innerHTML='';
    nets.forEach(n=>{
      const pct=Math.max(0,Math.min(100,Math.round((n.rssi+95)*2)));
      const saved=cfgWifi.some(w=>w.ssid===n.ssid);
      const div=document.createElement('div');
      div.style.cssText='display:flex;align-items:center;gap:10px;padding:9px 12px;cursor:pointer;border-bottom:1px solid var(--border)';
      div.innerHTML=`<span style="flex:1">${esc(n.ssid)}${saved?' <span style="color:var(--ok);font-size:.72rem">guardada</span>':''}</span>
        ${n.sec?'<svg class="lic" style="color:var(--dim)"><use href="#i-lock"/></svg>':''}
        <span style="color:var(--dim);font-size:.8rem;font-family:Consolas,monospace">${pct}%</span>`;
      div.onclick=()=>{
        document.getElementById('wifiSsidNew').value=n.ssid;
        document.getElementById('wifiPassNew').focus();
        list.style.display='none';
      };
      div.onmouseenter=()=>div.style.background='var(--panel2)';
      div.onmouseleave=()=>div.style.background='';
      list.appendChild(div);
    });
  }).catch(()=>{
    // escanear desde el hotspot puede cortar la conexión un instante (una
    // sola radio): reintentar en vez de abandonar — el AP vuelve solo.
    if(++scanTries>15){ document.getElementById('scanBtn').disabled=false;
      document.getElementById('scanList').innerHTML='<div style="padding:10px;color:var(--dim)">Reconectate al hotspot y volvé a tocar Buscar</div>'; return; }
    setTimeout(pollScan,1200);
  });
}
function loadCfg(){
  fetch('/api/config').then(r=>r.json()).then(j=>{
    const f=document.getElementById('cfg');
    cfgWifi=(j.networks||[]).map(n=>(typeof n==='string'
      ? {ssid:n,pass:null,static:false}
      : {ssid:n.ssid,pass:null,static:!!n.static,ip:n.ip||'',gw:n.gw||'',sn:n.sn||'',dns:n.dns||''}));
    resetWifiForm(); renderWifi();
    f.name.value=j.name; f.sep.value=j.sep;
    f.eol.value=j.eol; f.ble.value=j.ble?1:0; f.rmode.value=j.rmode; f.inv.value=j.inv?1:0;
    cfgSep=j.sep;
    document.getElementById('fwv').textContent=j.fw;
    document.getElementById('fwv2').textContent=j.fw;
  });
}
function saveCfg(ev){
  ev.preventDefault();
  const f=ev.target;
  addWifi(true);   // recoger un SSID escrito a mano que no se "Agregó" aún
  const body={wifi:cfgWifi.map(w=>({ssid:w.ssid,pass:w.pass===null||w.pass===undefined?'':w.pass,
                static:!!w.static,ip:w.ip||'',gw:w.gw||'',sn:w.sn||'',dns:w.dns||''})),
              name:f.name.value,sep:f.sep.value,
              eol:+f.eol.value,ble:f.ble.value==='1',rmode:+f.rmode.value,inv:f.inv.value==='1'};
  cfgSep=f.sep.value;
  fetch('/api/config',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)})
    .then(r=>r.ok?toast('Guardado. Reiniciá para aplicar los cambios de WiFi/BLE.')
                 :toast('Error al guardar'));
}

/* ---------- refresco del chip de sesión en vivo ---------- */
const _renderSession=renderSession;
renderSession=function(){ _renderSession(); renderSesChip(); };

connect(); loadCaps(); loadCfg(); loadSession();
</script>
</body>
</html>)rawliteral";
