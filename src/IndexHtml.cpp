#include "IndexHtml.h"

const char INDEX_HTML[] PROGMEM = R"WTHTML(<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="color-scheme" content="dark">
<title>WorldTime FW</title>
<style>
:root{
  --bg:#020617;
  --bg-2:#0f172a;
  --card:rgba(15,23,42,.65);
  --border:#1e293b;
  --border-2:#334155;
  --border-3:#475569;
  --text:#e2e8f0;
  --text-2:#cbd5e1;
  --muted:#94a3b8;
  --muted-2:#64748b;
  --accent:#10b981;
  --accent-hi:#34d399;
  --accent-deep:#047857;
  --warn:#fbbf24;
  --err:#f87171;
  --danger-bd:#7f1d1d;
}
*{box-sizing:border-box}
html,body{margin:0;padding:0}
body{
  font-family:ui-sans-serif,system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;
  background:var(--bg);
  color:var(--text);
  line-height:1.5;
  min-height:100vh;
  -webkit-font-smoothing:antialiased;
}
.bg-grad{
  background-image:
    radial-gradient(ellipse at 15% -10%,rgba(16,185,129,.10),transparent 55%),
    radial-gradient(ellipse at 85% 110%,rgba(99,102,241,.08),transparent 55%);
  min-height:100vh;
}
.container{
  max-width:48rem;
  margin:0 auto;
  padding:1.25rem 1rem 7.5rem;
}

/* Header */
.header{
  display:flex;align-items:center;justify-content:space-between;
  flex-wrap:wrap;gap:.75rem;margin-bottom:1.25rem;
}
.title{display:flex;align-items:baseline;gap:.5rem}
.title h1{margin:0;font-size:1.75rem;font-weight:700;letter-spacing:-.02em}
.badge{
  padding:.15rem .55rem;font-size:.7rem;font-family:ui-monospace,monospace;
  border-radius:.4rem;
  background:rgba(16,185,129,.12);
  border:1px solid rgba(16,185,129,.35);
  color:var(--accent-hi);
}
.status-line{font-size:.75rem;font-family:ui-monospace,monospace;color:var(--muted)}

/* Cards */
.card{
  background:var(--card);
  backdrop-filter:blur(8px);
  -webkit-backdrop-filter:blur(8px);
  border:1px solid var(--border);
  border-radius:1rem;
  padding:1.15rem;
  margin-bottom:.9rem;
  box-shadow:0 4px 14px -4px rgba(0,0,0,.35);
}
.card-head{
  display:flex;align-items:center;justify-content:space-between;
  gap:.5rem;margin-bottom:.95rem;flex-wrap:wrap;
}
.h-section{
  margin:0;
  font-size:.72rem;font-weight:600;
  text-transform:uppercase;letter-spacing:.09em;
  color:var(--muted);
}
.note{font-size:.72rem;color:var(--muted-2);line-height:1.5;margin:.7rem 0 0}

/* Layout */
.row{display:flex;gap:.5rem;flex-wrap:wrap;align-items:center}
.row-end{display:flex;gap:.65rem;flex-wrap:wrap;align-items:flex-end}
.col{display:flex;flex-direction:column;gap:.3rem}
.grid-2{display:grid;gap:.75rem;grid-template-columns:1fr}
@media (min-width:640px){.grid-2{grid-template-columns:1fr 1fr}}
.label{font-size:.72rem;color:var(--muted);display:block;margin-bottom:.3rem}

/* Form controls */
input:not([type=color]):not([type=range]):not([type=checkbox]):not([type=file]),
select{
  background:var(--bg);
  border:1px solid var(--border-2);
  color:var(--text);
  padding:.5rem .7rem;
  border-radius:.55rem;
  font-family:ui-monospace,SFMono-Regular,monospace;
  font-size:.85rem;
  outline:none;
  transition:border-color .15s,box-shadow .15s;
  width:100%;
}
input:focus:not([type=range]):not([type=checkbox]),
select:focus{
  border-color:var(--accent);
  box-shadow:0 0 0 3px rgba(16,185,129,.18);
}
input[type=range]{accent-color:var(--accent);width:100%;height:.45rem;cursor:pointer}
input[type=checkbox]{accent-color:var(--accent);width:1rem;height:1rem;cursor:pointer}
input[type=color]{
  padding:0;background:transparent;border:1px solid var(--border-2);
  border-radius:.4rem;cursor:pointer;width:2.5rem;height:2rem;
}
input[type=file]{
  background:var(--bg);border:1px solid var(--border-2);
  color:var(--text);padding:.4rem;border-radius:.55rem;
  font-size:.8rem;font-family:inherit;
}
input[type=file]::file-selector-button{
  background:#1e293b;border:1px solid var(--border-2);
  color:var(--text);padding:.35rem .8rem;border-radius:.4rem;
  font-size:.78rem;margin-right:.65rem;cursor:pointer;
  font-family:inherit;
}
input[type=file]::file-selector-button:hover{background:#334155}
code{
  background:var(--bg);padding:.1rem .35rem;border-radius:.3rem;
  font-size:.85em;color:var(--text-2);
  font-family:ui-monospace,SFMono-Regular,monospace;
}

/* Buttons */
.btn{
  display:inline-flex;align-items:center;justify-content:center;
  gap:.4rem;
  background:#1e293b;border:1px solid var(--border-2);
  color:var(--text);padding:.55rem 1rem;
  border-radius:.55rem;cursor:pointer;
  font-size:.85rem;font-weight:500;
  transition:all .15s;font-family:inherit;
}
.btn:hover{background:#293548;border-color:var(--border-3)}
.btn:active{transform:translateY(1px)}
.btn:disabled{opacity:.5;cursor:not-allowed;transform:none}
.btn-sm{padding:.35rem .75rem;font-size:.75rem}
.btn-primary{
  background:linear-gradient(180deg,#10b981 0%,#059669 100%);
  border-color:#0b8a64;color:#022c22;
  box-shadow:0 1px 0 rgba(255,255,255,.18) inset;
}
.btn-primary:hover{
  background:linear-gradient(180deg,#34d399 0%,#10b981 100%);
  border-color:#10b981;
}
.btn-danger{color:#fca5a5;border-color:var(--danger-bd);background:transparent}
.btn-danger:hover{background:rgba(127,29,29,.4);color:#fecaca}

/* Toggle switch */
.toggle{position:relative;display:inline-block;width:42px;height:24px;flex-shrink:0}
.toggle input{opacity:0;width:0;height:0;position:absolute}
.toggle-slider{
  position:absolute;inset:0;background:#334155;
  border-radius:9999px;cursor:pointer;transition:.2s;
}
.toggle-slider:before{
  content:'';position:absolute;
  height:18px;width:18px;left:3px;top:3px;
  background:#fff;border-radius:50%;transition:.2s;
  box-shadow:0 1px 2px rgba(0,0,0,.4);
}
.toggle input:checked+.toggle-slider{background:var(--accent)}
.toggle input:checked+.toggle-slider:before{transform:translateX(18px)}
.toggle-row{display:flex;align-items:center;gap:.75rem;cursor:pointer;margin-bottom:1rem}

/* Brightness value */
.bright-val{
  font-family:ui-monospace,monospace;font-weight:700;font-size:1.4rem;
  color:var(--accent-hi);font-variant-numeric:tabular-nums;
}
.bright-val-sm{
  font-family:ui-monospace,monospace;font-weight:700;font-size:.9rem;
  color:var(--accent-hi);font-variant-numeric:tabular-nums;
}
.range-marks{
  display:flex;justify-content:space-between;
  font-size:.7rem;color:var(--muted-2);
  margin-top:.55rem;font-family:ui-monospace,monospace;
}

/* WiFi */
.wifi-status{font-size:.9rem;margin-bottom:.85rem;display:flex;flex-wrap:wrap;align-items:center;gap:.5rem}
#nets{display:flex;flex-direction:column;gap:.3rem;max-height:14rem;overflow-y:auto;margin-bottom:.85rem;padding-right:.2rem}
#nets:empty{display:none}
.net{
  display:flex;justify-content:space-between;align-items:center;
  padding:.55rem .8rem;
  background:rgba(2,6,23,.6);
  border:1px solid transparent;
  border-radius:.5rem;cursor:pointer;font-size:.85rem;transition:all .15s;
}
.net:hover{background:#1e293b;border-color:var(--border-2)}
.net-name{font-family:ui-monospace,monospace;color:var(--text)}
.net-rssi{font-family:ui-monospace,monospace;font-size:.78rem;font-variant-numeric:tabular-nums}
.rssi-good{color:var(--accent-hi)}
.rssi-mid{color:var(--warn)}
.rssi-bad{color:var(--err)}

/* Status pill */
.pill{
  display:inline-flex;align-items:center;gap:.45rem;
  padding:.22rem .6rem;border-radius:9999px;
  font-size:.75rem;font-weight:500;
}
.pill-ok{background:rgba(16,185,129,.12);color:var(--accent-hi);border:1px solid rgba(16,185,129,.35)}
.pill-warn{background:rgba(251,191,36,.12);color:var(--warn);border:1px solid rgba(251,191,36,.35)}
.pill-err{background:rgba(248,113,113,.12);color:var(--err);border:1px solid rgba(248,113,113,.35)}
.pill-dot{width:.45rem;height:.45rem;border-radius:9999px;background:currentColor;animation:pulse 2s ease-in-out infinite}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.45}}

/* Cities */
#cities{display:flex;flex-direction:column;gap:.55rem}
.city-row{
  display:grid;
  grid-template-columns:2.8rem minmax(5rem,5.5rem) 1fr 1fr;
  gap:.5rem;align-items:center;
}

/* Icon editor */
.icon-edit-row{display:flex;flex-wrap:wrap;align-items:flex-start;gap:1.5rem}
.icon-edit-right{flex:1;min-width:200px}
#icon-grid{
  display:grid;grid-template-columns:repeat(5,1fr);
  gap:1px;background:var(--border-2);padding:1px;
  border-radius:.55rem;width:200px;height:200px;
  flex-shrink:0;box-shadow:inset 0 1px 3px rgba(0,0,0,.5);
}
#icon-grid button{padding:0;border:0;border-radius:0;cursor:pointer;transition:transform .08s}
#icon-grid button:hover{transform:scale(1.07);z-index:1;outline:1px solid #fff;outline-offset:-1px}
.frame-tab{
  display:inline-flex;align-items:center;gap:.35rem;
  padding:.32rem .7rem;border-radius:.45rem;
  font-size:.75rem;background:var(--bg);
  border:1px solid var(--border-2);cursor:pointer;
  font-family:ui-monospace,monospace;transition:all .15s;
  color:var(--text-2);
}
.frame-tab:hover{border-color:var(--border-3);background:#1e293b}
.frame-tab.active{background:var(--accent-deep);border-color:var(--accent);color:#fff;box-shadow:0 0 0 1px var(--accent)}
.frame-tab button{padding:0 .15rem;border:0;background:transparent;color:#fca5a5;cursor:pointer;font-size:.95rem;line-height:1}
#palette{display:grid;grid-template-columns:repeat(8,auto);gap:.5rem;width:fit-content}
.swatch{display:flex;flex-direction:column;align-items:center;gap:.2rem}
.swatch button{width:2rem;height:2rem;padding:0;border-radius:.4rem;cursor:pointer;transition:transform .08s}
.swatch button:hover{transform:scale(1.08)}
.swatch input[type=color]{width:2rem;height:.7rem;border-radius:0;border:1px solid var(--border-2)}
.swatch-tag{font-size:.6rem;color:var(--muted-2)}

/* Weather table */
.tbl-wrap{overflow-x:auto;margin:0 -.25rem}
.weather-tbl{width:100%;border-collapse:collapse;font-size:.85rem;font-family:ui-monospace,monospace}
.weather-tbl th{
  text-align:left;padding:.3rem .55rem;
  font-size:.68rem;text-transform:uppercase;letter-spacing:.08em;
  color:var(--muted-2);font-weight:600;
}
.weather-tbl td{padding:.4rem .55rem;border-top:1px solid rgba(30,41,59,.7)}
.text-temp{color:var(--accent-hi)}
.src-om{color:var(--accent-hi)}        /* Open-Meteo: verde */
.src-tio{color:#60a5fa}                /* Tomorrow.io: azul */
.src-none{color:var(--muted-2)}
.text-day{color:#fcd34d}
.text-night{color:#a5b4fc}
.text-muted{color:var(--muted-2)}
.text-accent{color:var(--accent-hi)}
.text-warn{color:var(--warn)}

/* OTA */
#ota-progress{height:.5rem;background:var(--bg);border-radius:9999px;overflow:hidden;border:1px solid var(--border)}
#ota-bar{height:100%;width:0;background:linear-gradient(90deg,#10b981,#34d399);transition:width .1s linear}

/* Save bar */
.savebar{
  position:fixed;bottom:0;left:0;right:0;
  background:rgba(2,6,23,.92);
  backdrop-filter:blur(12px);-webkit-backdrop-filter:blur(12px);
  border-top:1px solid var(--border);z-index:10;
}
.savebar-inner{
  max-width:48rem;margin:0 auto;
  padding:.8rem 1rem;
  display:flex;flex-wrap:wrap;gap:.55rem;
}
.btn-save{flex:1;min-width:160px;padding:.7rem 1rem;font-size:.95rem;font-weight:600}
#msg{
  max-width:48rem;margin:0 auto;
  padding:0 1rem .55rem;font-size:.85rem;
  text-align:center;min-height:1.25em;color:var(--muted);
}
.msg-ok{color:var(--accent-hi)}
.msg-warn{color:var(--warn)}
.msg-err{color:var(--err)}

/* Modal */
.modal{position:fixed;inset:0;z-index:50;display:flex;align-items:center;justify-content:center;padding:1rem}
.modal.hidden{display:none}
.modal-backdrop{position:absolute;inset:0;background:rgba(2,6,23,.78);backdrop-filter:blur(4px);-webkit-backdrop-filter:blur(4px)}
.modal-card{position:relative;background:#0f172a;border:1px solid var(--border);border-radius:1rem;width:100%;max-width:42rem;max-height:85vh;display:flex;flex-direction:column;box-shadow:0 20px 50px rgba(0,0,0,.5)}
.modal-head{display:flex;align-items:center;justify-content:space-between;padding:.85rem 1.1rem;border-bottom:1px solid var(--border)}
.modal-head h3{margin:0;font-size:.95rem;font-weight:600}
.modal-body{padding:1rem 1.1rem;overflow-y:auto;display:flex;flex-direction:column;gap:.85rem}
.modal-meta{font-size:.75rem;color:var(--muted);font-family:ui-monospace,monospace;display:flex;flex-wrap:wrap;gap:.65rem}
.modal-meta b{color:var(--text-2);font-weight:600}
.modal-tabs{display:flex;gap:.3rem;width:100%;margin-bottom:.4rem}
.modal-tabs button{flex:1;background:var(--bg);border:1px solid var(--border-2);color:var(--muted);padding:.35rem .6rem;border-radius:.4rem;cursor:pointer;font-size:.75rem;font-family:inherit;transition:all .15s}
.modal-tabs button:hover{border-color:var(--border-3);color:var(--text-2)}
.modal-tabs button.active{background:var(--accent-deep);border-color:var(--accent);color:#fff}
.modal-section pre{margin:.3rem 0 0;background:var(--bg);border:1px solid var(--border);border-radius:.5rem;padding:.65rem;font-size:.72rem;line-height:1.45;color:var(--text-2);overflow-x:auto;white-space:pre-wrap;word-break:break-all;max-height:18rem;overflow-y:auto}
.icon-btn{background:transparent;border:1px solid var(--border-2);color:var(--text-2);padding:.15rem .45rem;border-radius:.35rem;cursor:pointer;font-size:.7rem;font-family:inherit;transition:all .15s}
.icon-btn:hover{background:#1e293b;border-color:var(--border-3);color:var(--accent-hi)}

/* Helpers */
.hidden{display:none}
.flex-1{flex:1}
.min-w-200{min-width:200px}
.mb-3{margin-bottom:.85rem}
.mb-4{margin-bottom:1rem}
.gap-1{gap:.3rem}
</style>
</head>
<body>
<div class="bg-grad">
<div class="container">

<header class="header">
  <div class="title">
    <h1>WorldTime</h1>
    <span class="badge">FW</span>
  </div>
  <div id="status" class="status-line">cargando…</div>
</header>

<section class="card">
  <div class="card-head">
    <h2 class="h-section">WiFi</h2>
    <button id="scan" class="btn btn-sm">Buscar redes</button>
  </div>
  <div id="wifi-status" class="wifi-status">-</div>
  <div id="nets"></div>
  <div class="grid-2 mb-3">
    <label><span class="label">SSID</span><input id="ssid"/></label>
    <label><span class="label">Password</span><input id="pwd" type="password"/></label>
  </div>
  <button id="connect" class="btn btn-primary">Conectar y reiniciar</button>
</section>

<section class="card">
  <div class="card-head">
    <h2 class="h-section">Brillo (día)</h2>
    <span id="bright-val" class="bright-val">50%</span>
  </div>
  <input id="bright" type="range" min="5" max="100" step="5"/>
  <div class="range-marks"><span>5%</span><span>100%</span></div>
</section>

<section class="card">
  <div class="card-head">
    <h2 class="h-section">Modo noche</h2>
    <label class="toggle"><input id="nm-en" type="checkbox"/><span class="toggle-slider"></span></label>
  </div>
  <div class="grid-2 mb-3">
    <label><span class="label">Inicio</span><input id="nm-start" type="time"/></label>
    <label><span class="label">Fin</span><input id="nm-end" type="time"/></label>
  </div>
  <div class="card-head" style="margin-bottom:.4rem">
    <span class="label" style="margin-bottom:0">Brillo nocturno</span>
    <span id="nm-bright-val" class="bright-val-sm">10%</span>
  </div>
  <input id="nm-bright" type="range" min="5" max="100" step="5"/>
</section>

<section class="card">
  <h2 class="h-section mb-3">Ciudades</h2>
  <div id="cities"></div>
</section>

<section class="card">
  <h2 class="h-section mb-3">Iconos</h2>
  <div class="row-end mb-3">
    <label><span class="label">Editar</span><select id="icon-pick" style="min-width:8rem"></select></label>
    <button id="reset-icon" class="btn btn-danger btn-sm">Restablecer este icono</button>
  </div>
  <div class="row mb-3">
    <div id="icon-frames" class="row gap-1"></div>
    <button id="frame-add" class="btn btn-sm">+ Frame</button>
    <button id="frame-play" class="btn btn-sm"><span class="text-accent">▶</span> Play</button>
    <button id="frame-play-device" class="btn btn-sm" title="Reproduce este icono en la fila 0 del panel real"><span style="color:#60a5fa">📺</span> Device</button>
  </div>
  <label style="display:block;margin-bottom:1rem;max-width:10rem">
    <span class="label">Duración frame (ms)</span>
    <input id="frame-ms" type="number" min="50" max="5000" step="50"/>
  </label>
  <div class="icon-edit-row">
    <div id="icon-grid"></div>
    <div class="icon-edit-right">
      <span class="label">Paleta</span>
      <div id="palette"></div>
    </div>
  </div>
</section>

<section class="card">
  <h2 class="h-section mb-3">Otros</h2>
  <label class="toggle-row">
    <span class="toggle"><input id="blink" type="checkbox"/><span class="toggle-slider"></span></span>
    <span style="font-size:.9rem">Dos puntos parpadeando</span>
  </label>
  <label class="toggle-row">
    <span class="toggle"><input id="hour-lz" type="checkbox"/><span class="toggle-slider"></span></span>
    <span style="font-size:.9rem">Cero a la izquierda en la hora <span class="text-muted">(p.ej. 07:05)</span></span>
  </label>
  <label class="toggle-row">
    <span class="toggle"><input id="date-text" type="checkbox"/><span class="toggle-slider"></span></span>
    <span style="font-size:.9rem">$DATE como "8 May" <span class="text-muted">(en lugar de "08/05"; mes en español, día sin cero a la izquierda)</span></span>
  </label>
  <label class="toggle-row">
    <span class="toggle"><input id="om-ind" type="checkbox"/><span class="toggle-slider"></span></span>
    <span style="font-size:.9rem">Indicador OM en panel <span class="text-muted">(punto gris bajo el º cuando una fila usa Open-Meteo)</span></span>
  </label>
  <div class="grid-2 mb-3">
    <label>
      <span class="label">Indicador de segundos</span>
      <select id="sec-indicator">
        <option value="none">Ninguno</option>
        <option value="marker">Marcador (3 px en fila inferior)</option>
        <option value="bar">Barra vertical (full-height por detras)</option>
      </select>
    </label>
    <label id="sec-bar-color-wrap">
      <span class="label">Color barra vertical</span>
      <input id="sec-bar-color" type="color" value="#333333"/>
    </label>
  </div>
  <div class="grid-2 mb-3" id="sec-bar-extras">
    <label>
      <span class="label">Ancho barra (px)</span>
      <input id="sec-bar-width" type="number" min="1" max="16" step="1" value="1"/>
    </label>
    <label class="toggle-row">
      <span class="toggle"><input id="sec-bar-progress" type="checkbox"/><span class="toggle-slider"></span></span>
      <span style="font-size:.9rem">Modo progressbar <span class="text-muted">(rellena la zona ya recorrida)</span></span>
    </label>
  </div>
  <label class="toggle-row">
    <span class="toggle"><input id="trend-en" type="checkbox"/><span class="toggle-slider"></span></span>
    <span style="font-size:.9rem">Indicador de tendencia <span class="text-muted">(2 px tras el º: verde sube / rojo baja)</span></span>
  </label>
  <div class="grid-2 mb-3" id="trend-extras">
    <label>
      <span class="label">Horizonte forecast</span>
      <select id="trend-horizon">
        <option value="1">1 h</option>
        <option value="2">2 h</option>
      </select>
    </label>
    <label>
      <span class="label">Umbrales (°C)</span>
      <div class="row">
        <input id="trend-th1" type="number" min="0" max="50" step="0.1" value="0.5" style="flex:1"/>
        <input id="trend-th2" type="number" min="0" max="50" step="0.1" value="1.5" style="flex:1"/>
        <input id="trend-th3" type="number" min="0" max="50" step="0.1" value="3" style="flex:1"/>
      </div>
      <span class="note" style="margin-top:.25rem">|Δ| ≥ th1 → 1px, ≥ th2 → 2px, ≥ th3 → 3px</span>
    </label>
    <label>
      <span class="label">Color sube</span>
      <input id="trend-color-up" type="color" value="#00C000"/>
    </label>
    <label>
      <span class="label">Color baja</span>
      <input id="trend-color-down" type="color" value="#C00000"/>
    </label>
    <label>
      <span class="label">Color estable (=)</span>
      <input id="trend-color-stable" type="color" value="#666666"/>
    </label>
  </div>
  <h3 style="margin-top:1rem">Modo focus (boton central)</h3>
  <span class="note">Colores especificos cuando el modo focus esta activo (solo se muestra la primera ciudad en grande). Independientes del color de la ciudad.</span>
  <div class="grid-2">
    <label>
      <span class="label">Color hora (focus)</span>
      <input id="focus-hour-color" type="color" value="#FFFFFF"/>
    </label>
    <label>
      <span class="label">Color fecha (focus)</span>
      <input id="focus-date-color" type="color" value="#AAAAAA"/>
    </label>
  </div>
  <h3 style="margin-top:1rem">Claude stats (modo 3)</h3>
  <span class="note">Pegar el valor de la cookie <code>sessionKey</code> de <code>claude.ai</code> (DevTools &rarr; Application &rarr; Cookies). Si esta vacio, el modo Claude no aparece en el ciclo del boton central. El <code>orgId</code> se descubre automaticamente al primer fetch exitoso.</span>
  <div class="grid-2">
    <label>
      <span class="label">sessionKey (claude.ai)</span>
      <input id="claude-session-key" type="text" autocomplete="off" spellcheck="false" placeholder="sk-ant-sid01-...."/>
    </label>
    <label>
      <span class="label">Refresco (segundos, 60-3600)</span>
      <input id="claude-refresh" type="number" min="60" max="3600" step="30" value="180"/>
    </label>
  </div>
  <div class="grid-2">
    <label>
      <span class="label">Refresco meteo (segundos)</span>
      <input id="refresh" type="number" min="30" max="3600" step="30"/>
    </label>
    <label>
      <span class="label">Orden RGB <span class="text-warn">(reinicia al cambiar)</span></span>
      <select id="rgb-order">
        <option value="RGB">RGB (estándar)</option>
        <option value="RBG">RBG (G/B intercambiados)</option>
      </select>
    </label>
  </div>
</section>

<section class="card">
  <h2 class="h-section mb-3">Proveedor meteo</h2>
  <p class="note" style="margin-top:0">
    Open-Meteo (default, sin clave) siempre da hora local y día/noche. Si seleccionas
    Tomorrow.io o WeatherAPI, ese provider se usa para temperatura y código del tiempo;
    si su último fetch tiene > 1h sin éxito, esa fila cae a Open-Meteo.
  </p>
  <label style="margin-top:.85rem">
    <span class="label">Provider activo</span>
    <select id="prov-active">
      <option value="none">Ninguno (solo Open-Meteo)</option>
      <option value="tomorrow">Tomorrow.io</option>
      <option value="weatherapi">WeatherAPI</option>
    </select>
  </label>
  <div class="grid-2 mb-3" style="margin-top:.85rem">
    <label>
      <span class="label">API key Tomorrow.io</span>
      <input id="tio-key" type="text" placeholder="pega aquí la api key" autocomplete="off" spellcheck="false"/>
      <span class="note" id="tio-key-info" style="margin-top:.25rem">-</span>
    </label>
    <label>
      <span class="label">Refresco Tomorrow.io (s) <span class="text-warn">free=25 calls/día</span></span>
      <input id="tio-refresh" type="number" min="60" max="86400" step="60"/>
    </label>
  </div>
  <div class="grid-2 mb-3">
    <label>
      <span class="label">API key WeatherAPI</span>
      <input id="wap-key" type="text" placeholder="pega aquí la api key" autocomplete="off" spellcheck="false"/>
      <span class="note" id="wap-key-info" style="margin-top:.25rem">-</span>
    </label>
    <label>
      <span class="label">Refresco WeatherAPI (s) <span class="text-warn">free=1M/mes</span></span>
      <input id="wap-refresh" type="number" min="60" max="86400" step="60"/>
    </label>
  </div>
  <button id="prov-save" class="btn btn-primary">Guardar provider</button>
</section>

<section class="card">
  <h2 class="h-section mb-3">Logs meteo</h2>
  <div class="tbl-wrap">
    <table id="weather" class="weather-tbl">
      <thead>
        <tr><th>Ciudad</th><th>Offset</th><th>Temp</th><th>Code</th><th>Day</th><th>OM</th><th id="th-prem">Prem</th><th title="Orden de refresco automatico del provider premium (1 = proxima)">Ord</th><th></th></tr>
      </thead>
      <tbody></tbody>
    </table>
  </div>
</section>

<!-- Modal de debug por ciudad: URL llamada y respuesta raw -->
<div id="wx-modal" class="modal hidden">
  <div class="modal-backdrop"></div>
  <div class="modal-card">
    <div class="modal-head">
      <h3 id="wx-modal-title">Debug meteo</h3>
      <button id="wx-modal-close" class="btn btn-sm">×</button>
    </div>
    <div class="modal-body">
      <div class="modal-meta" id="wx-modal-meta"></div>
      <div class="modal-section">
        <span class="label">URL</span>
        <pre id="wx-modal-url"></pre>
      </div>
      <div class="modal-section">
        <span class="label">Response body</span>
        <pre id="wx-modal-body"></pre>
      </div>
    </div>
  </div>
</div>

<section class="card">
  <h2 class="h-section mb-3">Actualizar firmware (OTA)</h2>
  <div class="row mb-3">
    <input id="ota-file" type="file" accept=".bin" class="flex-1 min-w-200"/>
    <button id="ota-upload" class="btn btn-primary">Subir y reiniciar</button>
  </div>
  <div id="ota-progress" class="hidden"><div id="ota-bar"></div></div>
  <p class="note">
    Sube el .bin generado con <code>pio run -e matrixportal_s3</code> (en
    <code>.pio/build/matrixportal_s3/firmware.bin</code>). Tarda ~1 min.
  </p>
</section>

<section class="card">
  <h2 class="h-section mb-3">Backup / Restaurar</h2>
  <div class="row">
    <button id="cfg-export" class="btn">Descargar config</button>
    <button id="cfg-import-btn" class="btn">Cargar config…</button>
    <input id="cfg-import" type="file" accept="application/json,.json" class="hidden"/>
  </div>
  <p class="note">El JSON descargado contiene cities, brillo, modo noche, paleta e iconos. NO incluye creds WiFi.</p>
</section>

</div>
</div>

<div class="savebar">
  <div class="savebar-inner">
    <button id="save" class="btn btn-primary btn-save">Guardar cambios</button>
    <button id="reload" class="btn">Recargar</button>
    <button id="reset-dev" class="btn btn-danger">Reiniciar device</button>
  </div>
  <div id="msg"></div>
</div>

<script>
const $ = s => document.querySelector(s);
let cfg = null, curIcon = 'SUN', curFrame = 0, curColor = 1;
let playTimer = null;
let initialRgbOrder = null;   // baseline para detectar cambio en el selector RGB/RBG
// AbortController para cancelar fetches periódicos antes del OTA (evita que
// requests en vuelo queden colgadas esperando un device que se acaba de resetear).
let pollAborter = new AbortController();
function pollSignal() { return pollAborter.signal; }
function abortPolls() { pollAborter.abort(); pollAborter = new AbortController(); }

// Timers de polling: durante OTA o reset los pausamos para no martillar al
// device mientras escribe flash. Se reinician al recargar la pagina.
let statusTimer = null, weatherTimer = null;
function startPolls() {
  if (!statusTimer) statusTimer = setInterval(loadStatus, 5000);
  if (!weatherTimer) weatherTimer = setInterval(loadWeather, 10000);
}
function stopPolls() {
  if (statusTimer) { clearInterval(statusTimer); statusTimer = null; }
  if (weatherTimer) { clearInterval(weatherTimer); weatherTimer = null; }
  abortPolls();
}

function setMsg(t, kind){
  const m = $('#msg');
  m.textContent = t || '';
  m.className = '';
  if (kind === 'ok') m.classList.add('msg-ok');
  else if (kind === 'err') m.classList.add('msg-err');
  else if (kind === 'warn') m.classList.add('msg-warn');
}
function fmtUp(s){ s=Math.floor(s); if(s<60)return s+'s'; if(s<3600)return Math.floor(s/60)+'m '+(s%60)+'s'; return Math.floor(s/3600)+'h '+Math.floor((s%3600)/60)+'m'; }
function intToHex(n){ return '#'+(n|0).toString(16).padStart(6,'0'); }
function hexToInt(h){ return parseInt(h.replace('#',''),16); }
function minsToHHMM(m){ const h=Math.floor(m/60),mm=m%60; return String(h).padStart(2,'0')+':'+String(mm).padStart(2,'0'); }
function hhmmToMins(s){ const [h,m]=s.split(':').map(Number); return h*60+m; }
function rssiClass(r){ return r >= -60 ? 'rssi-good' : r >= -75 ? 'rssi-mid' : 'rssi-bad'; }

async function loadStatus(){
  try{
    const r = await fetch('/api/status', {signal: pollSignal()}); const d = await r.json();
    $('#status').innerHTML = `<span class="text-accent">${d.ip||'-'}</span> · ${fmtUp(d.uptime_sec)} · heap ${(d.heap_free/1024).toFixed(1)}KB${d.rssi?' · '+d.rssi+'dBm':''}`;
  }catch(e){}
}
async function loadWifi(){
  try{
    const r = await fetch('/api/wifi'); const d = await r.json();
    let html = '';
    if (d.mode === 'sta') html = `<span class="pill pill-ok"><span class="pill-dot"></span>Conectado</span><span>a <code>${d.current_ssid}</code> · IP <code>${d.ip}</code></span>`;
    else if (d.mode === 'ap') html = `<span class="pill pill-warn"><span class="pill-dot"></span>Modo AP</span><span>SSID <code>${d.ap_ssid}</code> · IP <code>${d.ip}</code></span>`;
    else html = `<span class="pill pill-err">Sin conexión</span>`;
    $('#wifi-status').innerHTML = html;
    if (d.current_ssid && !$('#ssid').value) $('#ssid').value = d.current_ssid;
  }catch(e){}
}
async function loadConfig(){
  try{
    const r = await fetch('/api/config'); cfg = await r.json();
    $('#bright').value = Math.round(cfg.brightness*100);
    $('#bright-val').textContent = $('#bright').value+'%';
    $('#nm-en').checked = cfg.night_mode.enabled;
    $('#nm-start').value = minsToHHMM(cfg.night_mode.start_mins);
    $('#nm-end').value = minsToHHMM(cfg.night_mode.end_mins);
    $('#nm-bright').value = Math.round(cfg.night_mode.brightness*100);
    $('#nm-bright-val').textContent = $('#nm-bright').value+'%';
    $('#blink').checked = cfg.colon_blink;
    $('#hour-lz').checked = cfg.hour_leading_zero !== false;   // default true
    $('#date-text').checked = !!cfg.date_format_text;
    $('#om-ind').checked = !!cfg.om_indicator;
    $('#sec-indicator').value = cfg.seconds_indicator || (cfg.seconds_bar ? 'marker' : 'none');
    $('#sec-bar-color').value = intToHex(cfg.seconds_bar_color != null ? cfg.seconds_bar_color : 0x333333);
    $('#sec-bar-width').value = cfg.seconds_bar_width || 1;
    $('#sec-bar-progress').checked = !!cfg.seconds_bar_progress;
    const showBarExtras = ($('#sec-indicator').value === 'bar');
    $('#sec-bar-color-wrap').style.display = showBarExtras ? '' : 'none';
    $('#sec-bar-extras').style.display = showBarExtras ? '' : 'none';
    $('#trend-en').checked = !!cfg.forecast_indicator_enabled;
    $('#trend-horizon').value = (cfg.forecast_indicator_horizon_h === 2) ? '2' : '1';
    $('#trend-th1').value = cfg.forecast_thresh_1 != null ? cfg.forecast_thresh_1 : 0.5;
    $('#trend-th2').value = cfg.forecast_thresh_2 != null ? cfg.forecast_thresh_2 : 1.5;
    $('#trend-th3').value = cfg.forecast_thresh_3 != null ? cfg.forecast_thresh_3 : 3;
    $('#trend-color-up').value     = intToHex(cfg.forecast_color_rising  != null ? cfg.forecast_color_rising  : 0x00C000);
    $('#trend-color-down').value   = intToHex(cfg.forecast_color_falling != null ? cfg.forecast_color_falling : 0xC00000);
    $('#trend-color-stable').value = intToHex(cfg.forecast_color_stable  != null ? cfg.forecast_color_stable  : 0x666666);
    $('#focus-hour-color').value   = intToHex(cfg.focus_hour_color       != null ? cfg.focus_hour_color       : 0xFFFFFF);
    $('#focus-date-color').value   = intToHex(cfg.focus_date_color       != null ? cfg.focus_date_color       : 0xAAAAAA);
    // Claude: la sessionKey se devuelve plana desde el backend y se muestra
    // en el campo para que el usuario pueda verla / editarla.
    $('#claude-session-key').value = cfg.claude_session_key || '';
    $('#claude-refresh').value = cfg.claude_refresh_sec || 180;
    $('#trend-extras').style.display = $('#trend-en').checked ? '' : 'none';
    $('#refresh').value = cfg.weather_refresh_sec;
    $('#rgb-order').value = cfg.rgb_order || 'RGB';
    initialRgbOrder = $('#rgb-order').value;
    renderCities();
    renderIconPicker();
    renderPalette();
    renderFrames();
    renderIconGrid();
  }catch(e){ setMsg('Error config: '+e.message, 'err'); }
}
function renderCities(){
  const box = $('#cities'); box.innerHTML = '';
  cfg.cities.forEach((c,i) => {
    const row = document.createElement('div');
    row.className = 'city-row';
    row.innerHTML = `
      <input type="color" data-i="${i}" data-k="color" value="${intToHex(c.color)}"/>
      <input data-i="${i}" data-k="name" value="${c.name||''}" maxlength="6"/>
      <input data-i="${i}" data-k="lat" type="number" step="0.000001" value="${c.lat}" placeholder="lat"/>
      <input data-i="${i}" data-k="lon" type="number" step="0.000001" value="${c.lon}" placeholder="lon"/>`;
    box.appendChild(row);
  });
  box.querySelectorAll('input').forEach(el => el.addEventListener('input', e => {
    const i = +e.target.dataset.i, k = e.target.dataset.k;
    let v = e.target.value;
    if (k === 'lat' || k === 'lon') v = parseFloat(v);
    if (k === 'color') v = hexToInt(v);
    cfg.cities[i][k] = v;
  }));
}

function renderIconPicker(){
  const sel = $('#icon-pick'); sel.innerHTML = '';
  Object.keys(cfg.icons).forEach(n => {
    const o = document.createElement('option'); o.value = n; o.text = n;
    sel.appendChild(o);
  });
  if (!cfg.icons[curIcon]) curIcon = Object.keys(cfg.icons)[0];
  sel.value = curIcon;
}
$('#icon-pick').addEventListener('change', e => {
  stopPlay();
  curIcon = e.target.value; curFrame = 0;
  renderFrames(); renderIconGrid();
});
function curFrames(){ return cfg.icons[curIcon] || []; }
function curFrameObj(){ return curFrames()[curFrame]; }

function renderFrames(){
  const box = $('#icon-frames'); box.innerHTML = '';
  const frames = curFrames();
  frames.forEach((f, i) => {
    const tab = document.createElement('span');
    tab.className = 'frame-tab' + (i === curFrame ? ' active' : '');
    const lbl = document.createElement('span');
    lbl.textContent = `F${i+1} · ${f.ms||500}ms`;
    lbl.onclick = () => { stopPlay(); curFrame = i; renderFrames(); renderIconGrid(); };
    tab.appendChild(lbl);
    if (frames.length > 1) {
      const del = document.createElement('button');
      del.textContent = '×';
      del.onclick = ev => {
        ev.stopPropagation();
        frames.splice(i, 1);
        if (curFrame >= frames.length) curFrame = frames.length-1;
        renderFrames(); renderIconGrid();
      };
      tab.appendChild(del);
    }
    box.appendChild(tab);
  });
  const f = curFrameObj();
  $('#frame-ms').value = f ? (f.ms||500) : 500;
}
$('#frame-add').addEventListener('click', () => {
  const f = curFrameObj();
  const newF = f
    ? { px: f.px.map(r => r.slice()), ms: f.ms || 500 }
    : { px: [[0,0,0,0,0],[0,0,0,0,0],[0,0,0,0,0],[0,0,0,0,0],[0,0,0,0,0]], ms: 500 };
  curFrames().push(newF);
  curFrame = curFrames().length - 1;
  renderFrames(); renderIconGrid();
});
$('#frame-ms').addEventListener('input', e => {
  const f = curFrameObj(); if (!f) return;
  let v = parseInt(e.target.value, 10);
  if (isNaN(v)) return;
  if (v < 50) v = 50; if (v > 5000) v = 5000;
  f.ms = v; renderFrames();
});
$('#reset-icon').addEventListener('click', () => {
  if (!confirm('Restablecer este icono a su default?')) return;
  alert('Para defaults oficiales: sigue editando manualmente o recarga sin guardar.');
});

function renderPalette(){
  const box = $('#palette'); box.innerHTML = '';
  (cfg.palette || []).forEach((c, i) => {
    const w = document.createElement('div'); w.className = 'swatch';
    const hex = intToHex(c);
    const b = document.createElement('button');
    b.style.background = i === 0
      ? 'repeating-linear-gradient(45deg,#334155 0 4px,#1e293b 4px 8px)'
      : hex;
    b.style.border = i === curColor ? '2px solid #34d399' : '1px solid #334155';
    b.style.boxShadow = i === curColor ? '0 0 0 2px rgba(52,211,153,.25)' : 'none';
    b.title = i === 0 ? 'Transparente' : 'Color '+i;
    b.onclick = () => { curColor = i; renderPalette(); };
    w.appendChild(b);
    if (i > 0) {
      const ed = document.createElement('input');
      ed.type = 'color'; ed.value = hex;
      ed.oninput = e => {
        cfg.palette[i] = hexToInt(e.target.value);
        b.style.background = e.target.value;
        renderIconGrid();
      };
      w.appendChild(ed);
    } else {
      const s = document.createElement('span');
      s.textContent = 'transp'; s.className = 'swatch-tag';
      w.appendChild(s);
    }
    box.appendChild(w);
  });
}

function renderIconGrid(){
  const g = $('#icon-grid'); g.innerHTML = '';
  const f = curFrameObj(); if (!f) return;
  const pal = (cfg.palette || []).map(intToHex);
  for (let y = 0; y < 5; y++) for (let x = 0; x < 5; x++) {
    const c = document.createElement('button');
    const v = f.px[y][x];
    c.style.background = v === 0
      ? 'repeating-linear-gradient(45deg,#334155 0 4px,#1e293b 4px 8px)'
      : pal[v];
    c.onclick = () => { curFrameObj().px[y][x] = curColor; renderIconGrid(); };
    g.appendChild(c);
  }
}

function stopPlay(){
  if (playTimer) clearTimeout(playTimer);
  playTimer = null;
  $('#frame-play').innerHTML = '<span class="text-accent">▶</span> Play';
}
function startPlay(){
  const fr = curFrames();
  if (!fr || fr.length < 2) { stopPlay(); return; }
  $('#frame-play').innerHTML = '<span class="text-warn">⏸</span> Pausa';
  const tick = () => {
    const f = curFrames();
    if (!f || f.length < 2) { stopPlay(); return; }
    curFrame = (curFrame + 1) % f.length;
    renderFrames(); renderIconGrid();
    playTimer = setTimeout(tick, f[curFrame].ms || 500);
  };
  playTimer = setTimeout(tick, fr[curFrame].ms || 500);
}
$('#frame-play').addEventListener('click', () => { playTimer ? stopPlay() : startPlay(); });

// --- Play on device ---
let devicePlaying = false;
async function startDevicePlay(){
  const frames = curFrames();
  if (!frames || !frames.length) { setMsg('Sin frames', 'err'); return; }
  try{
    const r = await fetch('/api/icons/preview', {method:'POST', headers:{'Content-Type':'application/json'},
      body: JSON.stringify({frames, duration_ms: 120000})});  // 2min failsafe
    const d = await r.json();
    if (!d.ok) throw new Error('preview rechazado');
    devicePlaying = true;
    $('#frame-play-device').innerHTML = '<span style="color:#f87171">■</span> Stop';
    setMsg(`Preview en device (${d.frames} frames, ${d.duration_ms/1000}s max)`, 'ok');
  }catch(e){ setMsg('Error: '+e.message, 'err'); }
}
async function stopDevicePlay(){
  try{
    await fetch('/api/icons/preview/stop', {method:'POST'});
  }catch(e){}
  devicePlaying = false;
  $('#frame-play-device').innerHTML = '<span style="color:#60a5fa">📺</span> Device';
  setMsg('Preview detenido', 'ok');
}
$('#frame-play-device').addEventListener('click', () => {
  devicePlaying ? stopDevicePlay() : startDevicePlay();
});

async function loadWeather(){
  try{
    const r = await fetch('/api/weather', {signal: pollSignal()}); const d = await r.json();
    const tbody = $('#weather').querySelector('tbody');
    tbody.innerHTML = '';
    const provider = d.premium_provider || (d.tomorrow_active ? 'tomorrow' : 'none');
    const nextIdx = (typeof d.premium_next_idx === 'number') ? d.premium_next_idx
                  : (typeof d.tio_next_idx === 'number') ? d.tio_next_idx : 0;
    const provLabel = provider === 'tomorrow' ? 'TIO'
                    : provider === 'weatherapi' ? 'WAPI'
                    : 'Prem';
    const provCss = provider === 'weatherapi' ? 'src-tio' : 'src-tio';   // mismo azul para premium
    const thPrem = $('#th-prem');
    if (thPrem) thPrem.textContent = provLabel;
    const fmtAge = s => (s == null || s < 0) ? '<span class="text-muted">-</span>' : `${s}s`;
    // Orden visual por Ord (1 = proxima en la rotacion premium). idx original
    // se mantiene para los onclick (API usa idx). Si no hay premium activo,
    // mantenemos el orden natural por idx.
    const ordOf = idx => ((idx - nextIdx + 4) % 4) + 1;
    const ordered = d.cities.map((c, idx) => ({c, idx}));
    if (provider !== 'none') ordered.sort((a, b) => ordOf(a.idx) - ordOf(b.idx));
    ordered.forEach(({c, idx}) => {
      const tr = document.createElement('tr');
      const srcClass = c.temp_source === 'tomorrow'   ? 'src-tio'
                     : c.temp_source === 'weatherapi' ? 'src-tio'
                     : c.temp_source === 'openmeteo'  ? 'src-om' : 'src-none';
      const day  = c.has_data ? (c.is_day ? '<span class="text-day">☀</span>' : '<span class="text-night">🌙</span>') : '<span class="text-muted">-</span>';
      const tmp  = c.has_data ? `<span class="${srcClass}">${c.temp_c}°</span>` : '<span class="text-muted">-</span>';
      const off  = c.has_data ? `${(c.offset_sec/3600).toFixed(1)}h` : '-';
      const codeStr = c.has_data ? `<span class="${srcClass}">${c.code}</span>` : '<span class="text-muted">-</span>';
      const omAge  = `<span class="src-om text-muted">${fmtAge(c.om_age_s)}</span>`;
      // Edad del provider premium activo (TIO o WAP). Si no hay activo, "-".
      const premAgeS = provider === 'tomorrow'   ? c.tio_age_s
                     : provider === 'weatherapi' ? c.wap_age_s
                     : null;
      const premAge = (provider === 'none')
        ? '<span class="text-muted">-</span>'
        : `<span class="${provCss}">${fmtAge(premAgeS)}</span>`;
      // Orden de actualizacion en la rotacion automatica del premium activo:
      // 1=siguiente, 2=tras ese, etc. Permite ver de un vistazo cuanto falta
      // para que el device refresque cada ciudad sin pulsar "forzar".
      const ordCell = (provider === 'none')
        ? '<span class="text-muted">-</span>'
        : `<span class="${provCss}">${((idx - nextIdx + 4) % 4) + 1}</span>`;
      tr.innerHTML = `<td>${c.name}</td><td class="text-muted">${off}</td><td>${tmp}</td><td>${codeStr}</td><td>${day}</td><td>${omAge}</td><td>${premAge}</td><td>${ordCell}</td><td></td>`;
      const cell = tr.lastElementChild;
      cell.style.whiteSpace = 'nowrap';
      const btnDbg = document.createElement('button');
      btnDbg.className = 'icon-btn';
      btnDbg.textContent = '?';
      btnDbg.title = 'Ver URL llamada y respuesta';
      btnDbg.onclick = () => openWxDebug(idx);
      cell.appendChild(btnDbg);
      // Botón de refetch del premium activo: solo si hay alguno configurado.
      if (provider !== 'none') {
        const btn = document.createElement('button');
        btn.className = 'icon-btn';
        btn.style.marginLeft = '.25rem';
        btn.textContent = '↻';
        btn.title = `Forzar fetch ${provLabel}`;
        btn.onclick = async () => {
          btn.disabled = true; btn.textContent = '…';
          try {
            const r = await fetch(`/api/weather/fetch?idx=${idx}&provider=${provider}`);
            const rd = await r.json();
            if (!rd.ok) setMsg(`${provLabel} idx=${idx}: HTTP ${rd.http} ${rd.err||''}`, 'err');
            else setMsg(`${provLabel} idx=${idx} actualizado`, 'ok');
          } catch(e) { setMsg('Error: '+e.message, 'err'); }
          finally { loadWeather(); }
        };
        cell.appendChild(btn);
      }
      tbody.appendChild(tr);
    });
  }catch(e){}
}

let curWxIdx = 0;
let curWxProvider = 'openmeteo';
async function openWxDebug(idx){
  curWxIdx = idx;
  curWxProvider = 'openmeteo';
  $('#wx-modal').classList.remove('hidden');
  await renderWxDebug();
}
async function renderWxDebug(){
  $('#wx-modal-title').textContent = 'Debug meteo · cargando…';
  $('#wx-modal-meta').innerHTML = '';
  $('#wx-modal-url').textContent = '';
  $('#wx-modal-body').textContent = '';
  try{
    const r = await fetch(`/api/weather/debug?idx=${curWxIdx}&provider=${curWxProvider}`);
    const d = await r.json();
    $('#wx-modal-title').textContent = `Debug meteo · ${d.name || ('city '+d.idx)}`;
    const ageStr   = (d.age_ms    && d.last_at_ms)    ? `hace ${fmtUp(d.age_ms/1000)}`     : 'nunca';
    const okAgeStr = (d.ok_age_ms && d.last_ok_at_ms) ? `hace ${fmtUp(d.ok_age_ms/1000)}` : 'nunca';
    const httpClass = d.http === 200 ? 'text-accent' : (d.http>0?'text-warn':'text-muted');
    const tabs = `
      <div class="modal-tabs">
        <button class="${curWxProvider==='openmeteo'?'active':''}" data-prov="openmeteo">Open-Meteo</button>
        <button class="${curWxProvider==='tomorrow'?'active':''}" data-prov="tomorrow">Tomorrow.io</button>
        <button class="${curWxProvider==='weatherapi'?'active':''}" data-prov="weatherapi">WeatherAPI</button>
      </div>`;
    let meta = tabs + `<span><b>HTTP:</b> <span class="${httpClass}">${d.http}</span></span>`;
    meta += `<span><b>Intentos:</b> ${d.attempts}</span>`;
    meta += `<span><b>Último intento:</b> ${ageStr}</span>`;
    meta += `<span><b>Último éxito:</b> ${okAgeStr}</span>`;
    if (d.body_len) meta += `<span><b>Body:</b> ${d.body_len} B</span>`;
    if (d.err) meta += `<span class="text-warn"><b>Err:</b> ${d.err}</span>`;
    $('#wx-modal-meta').innerHTML = meta;
    $('#wx-modal-meta').querySelectorAll('.modal-tabs button').forEach(b => {
      b.onclick = () => { curWxProvider = b.dataset.prov; renderWxDebug(); };
    });
    $('#wx-modal-url').textContent = d.url || '(sin url, fetch aún no realizado para este proveedor)';
    let bodyText = d.body || '(vacío)';
    try { bodyText = JSON.stringify(JSON.parse(bodyText), null, 2); } catch{}
    $('#wx-modal-body').textContent = bodyText;
  }catch(e){
    $('#wx-modal-title').textContent = 'Debug meteo · error';
    $('#wx-modal-body').textContent = e.message;
  }
}
$('#wx-modal-close').onclick = () => $('#wx-modal').classList.add('hidden');
$('#wx-modal').querySelector('.modal-backdrop').onclick = () => $('#wx-modal').classList.add('hidden');
document.addEventListener('keydown', e => { if (e.key === 'Escape') $('#wx-modal').classList.add('hidden'); });

$('#scan').onclick = async () => {
  const btn = $('#scan'); btn.disabled = true; btn.textContent = 'Buscando…';
  try{
    const r = await fetch('/api/wifi/scan'); const d = await r.json();
    const box = $('#nets'); box.innerHTML = '';
    (d.networks || []).forEach(n => {
      const div = document.createElement('div');
      div.className = 'net';
      const lock = n.secure ? ' <span class="text-muted">🔒</span>' : '';
      div.innerHTML = `<span class="net-name">${n.ssid}${lock}</span><span class="net-rssi ${rssiClass(n.rssi)}">${n.rssi} dBm</span>`;
      div.onclick = () => { $('#ssid').value = n.ssid; $('#pwd').focus(); };
      box.appendChild(div);
    });
  }finally{ btn.disabled = false; btn.textContent = 'Buscar redes'; }
};
$('#connect').onclick = async () => {
  const ssid = $('#ssid').value.trim(), password = $('#pwd').value;
  if (!ssid) { setMsg('Falta SSID', 'err'); return; }
  if (!confirm(`Conectar a "${ssid}" y reiniciar?`)) return;
  try{
    await fetch('/api/wifi', {method:'POST', headers:{'Content-Type':'application/json'}, body:JSON.stringify({ssid,password})});
    setMsg('Guardado. Reiniciando — reconecta a la nueva red.', 'ok');
  }catch(e){ setMsg('Error: '+e.message, 'err'); }
};

let brightTimer;
$('#bright').addEventListener('input', e => {
  $('#bright-val').textContent = e.target.value+'%';
  clearTimeout(brightTimer);
  brightTimer = setTimeout(() => {
    fetch('/api/brightness', {method:'POST', headers:{'Content-Type':'application/json'},
      body: JSON.stringify({brightness: e.target.value/100})}).catch(()=>{});
  }, 80);
});
$('#sec-indicator').addEventListener('change', e => {
  const show = (e.target.value === 'bar');
  $('#sec-bar-color-wrap').style.display = show ? '' : 'none';
  $('#sec-bar-extras').style.display = show ? '' : 'none';
});
$('#trend-en').addEventListener('change', e => {
  $('#trend-extras').style.display = e.target.checked ? '' : 'none';
});
let nmBrightTimer;
$('#nm-bright').addEventListener('input', e => {
  $('#nm-bright-val').textContent = e.target.value+'%';
  clearTimeout(nmBrightTimer);
  nmBrightTimer = setTimeout(() => {
    fetch('/api/brightness', {method:'POST', headers:{'Content-Type':'application/json'},
      body: JSON.stringify({night_brightness: e.target.value/100})}).catch(()=>{});
  }, 80);
});

$('#save').onclick = async () => {
  const patch = {
    brightness: $('#bright').value/100,
    weather_refresh_sec: +$('#refresh').value,
    colon_blink: $('#blink').checked,
    hour_leading_zero: $('#hour-lz').checked,
    date_format_text: $('#date-text').checked,
    om_indicator: $('#om-ind').checked,
    seconds_indicator: $('#sec-indicator').value,
    seconds_bar_color: hexToInt($('#sec-bar-color').value),
    seconds_bar_width: +$('#sec-bar-width').value,
    seconds_bar_progress: $('#sec-bar-progress').checked,
    forecast_indicator_enabled: $('#trend-en').checked,
    forecast_indicator_horizon_h: +$('#trend-horizon').value,
    forecast_thresh_1: +$('#trend-th1').value,
    forecast_thresh_2: +$('#trend-th2').value,
    forecast_thresh_3: +$('#trend-th3').value,
    forecast_color_rising:  hexToInt($('#trend-color-up').value),
    forecast_color_falling: hexToInt($('#trend-color-down').value),
    forecast_color_stable:  hexToInt($('#trend-color-stable').value),
    focus_hour_color:       hexToInt($('#focus-hour-color').value),
    focus_date_color:       hexToInt($('#focus-date-color').value),
    claude_refresh_sec:     parseInt($('#claude-refresh').value, 10) || 180,
    cities: cfg.cities,
    night_mode: {
      enabled: $('#nm-en').checked,
      start_mins: hhmmToMins($('#nm-start').value),
      end_mins: hhmmToMins($('#nm-end').value),
      brightness: $('#nm-bright').value/100,
    },
    palette: cfg.palette,
    icons: cfg.icons,
  };
  try{
    // rgb_order vive en NVS, endpoint dedicado. Solo lo enviamos si cambio.
    const newRgb = $('#rgb-order').value;
    if (newRgb !== initialRgbOrder) {
      const rr = await fetch('/api/rgb_order', {method:'POST', headers:{'Content-Type':'application/json'},
        body: JSON.stringify({rgb_order: newRgb})});
      const rd = await rr.json();
      if (!rd.ok) throw new Error('rgb_order: '+(rd.error||'fallo'));
      initialRgbOrder = newRgb;
    }
    // Claude sessionKey: la enviamos siempre tal cual aparece en el input.
    // Vacio = borra la key, contenido = la guarda. El campo se muestra ya
    // pre-poblada con el valor actual al cargar la pagina.
    patch.claude_session_key = $('#claude-session-key').value.trim();
    const r = await fetch('/api/config', {method:'POST', headers:{'Content-Type':'application/json'},
      body: JSON.stringify(patch)});
    const d = await r.json();
    if (d.error) throw new Error(d.error);
    setMsg('Guardado.' + (d.cities_changed ? ' (refresh meteo en curso)' : ''), 'ok');
    loadWeather();
  }catch(e){ setMsg('Error: '+e.message, 'err'); }
};

$('#cfg-export').onclick = async () => {
  try{
    // /api/config/export devuelve SOLO el contenido de cfg.json (sin rgb_order
    // ni claves NVS), para que el backup sea portable entre devices.
    const r = await fetch('/api/config/export'); const txt = await r.text();
    let body = txt; try { body = JSON.stringify(JSON.parse(txt), null, 2); } catch{}
    const a = document.createElement('a');
    a.href = 'data:application/json;charset=utf-8,'+encodeURIComponent(body);
    const ts = new Date().toISOString().slice(0,16).replace(/[:T]/g,'-');
    a.download = `worldtime_config_${ts}.json`;
    a.click();
    setMsg('Descargado.', 'ok');
  }catch(e){ setMsg('Error: '+e.message, 'err'); }
};
$('#cfg-import-btn').onclick = () => $('#cfg-import').click();
$('#cfg-import').onchange = async e => {
  const f = e.target.files[0]; if (!f) return;
  if (!confirm(`Cargar "${f.name}"? Sobreescribira la configuracion actual.`)) { e.target.value = ''; return; }
  try{
    const txt = await f.text();
    JSON.parse(txt);
    const r = await fetch('/api/config', {method:'POST', headers:{'Content-Type':'application/json'}, body: txt});
    const d = await r.json();
    if (d.error) throw new Error(d.error);
    setMsg('Cargado.', 'ok');
    setTimeout(loadConfig, 300);
  }catch(err){ setMsg('Error: '+err.message, 'err'); }
  finally{ e.target.value = ''; }
};

// Polling activo a /api/status hasta que el device responda. Se llama tras
// un OTA en lugar de hacer reload ciego con setTimeout — evita recargar
// antes de que el HTTP server esté arriba (timeout TCP feo) y también
// evita esperar de más si ya está listo.
async function waitForDevice(maxMs){
  const start = Date.now();
  while (Date.now() - start < maxMs) {
    try {
      const ctrl = new AbortController();
      const t = setTimeout(() => ctrl.abort(), 2000);
      const r = await fetch('/api/status', {cache:'no-store', signal: ctrl.signal});
      clearTimeout(t);
      if (r.ok) return Date.now() - start;
    } catch(e) {}
    await new Promise(r => setTimeout(r, 800));
  }
  return -1;
}

$('#ota-upload').onclick = () => {
  const f = $('#ota-file').files[0];
  if (!f) { setMsg('Selecciona un .bin primero', 'err'); return; }
  if (!confirm(`Subir ${f.name} (${(f.size/1024).toFixed(1)} KB) y reiniciar?`)) return;

  const fd = new FormData();
  fd.append('firmware', f);

  const xhr = new XMLHttpRequest();
  xhr.open('POST', '/api/firmware');

  // Para los polls antes de la subida: durante el OTA el device está
  // ocupado escribiendo flash y no necesita servir /api/status ni /api/weather.
  stopPolls();
  $('#ota-progress').classList.remove('hidden');
  $('#ota-bar').style.width = '0%';
  $('#ota-upload').disabled = true;
  setMsg('Subiendo firmware…');

  xhr.upload.onprogress = e => {
    if (e.lengthComputable) {
      const pct = Math.round((e.loaded/e.total)*100);
      $('#ota-bar').style.width = pct+'%';
      setMsg(`Subiendo ${pct}% (${(e.loaded/1024).toFixed(0)}/${(e.total/1024).toFixed(0)} KB)`);
    }
  };
  xhr.onload = async () => {
    $('#ota-upload').disabled = false;
    if (xhr.status !== 200) {
      setMsg('Error '+xhr.status+': '+xhr.responseText, 'err');
      startPolls();   // upload fallido → reanuda polls (device sigue vivo)
      return;
    }
    // Polls ya parados antes del upload; aquí nos aseguramos de cancelar
    // cualquier request que pudiera estar en vuelo (paranoia).
    abortPolls();
    setMsg('Firmware aceptado. Esperando reboot…', 'ok');
    const ms = await waitForDevice(30000);
    if (ms >= 0) {
      setMsg(`Device respondiendo tras ${(ms/1000).toFixed(1)}s. Recargando…`, 'ok');
      setTimeout(() => location.reload(), 500);
    } else {
      setMsg('Timeout esperando al device tras 30s. Recarga manualmente.', 'err');
    }
  };
  xhr.onerror = () => {
    $('#ota-upload').disabled = false;
    setMsg('Error de red durante la subida', 'err');
    startPolls();
  };
  xhr.send(fd);
};

$('#reload').onclick = () => location.reload();
$('#reset-dev').onclick = async () => {
  if (!confirm('Reiniciar el device?')) return;
  stopPolls();
  try{ await fetch('/api/reset', {method:'POST'}); }catch(e){}
  setMsg('Reiniciando…', 'warn');
  const ms = await waitForDevice(20000);
  if (ms >= 0) {
    setMsg(`Listo en ${(ms/1000).toFixed(1)}s. Recargando…`, 'ok');
    setTimeout(() => location.reload(), 400);
  } else {
    setMsg('Timeout. Recarga manualmente.', 'err');
  }
};

async function loadProvider(){
  try{
    const r = await fetch('/api/weather_provider'); const d = await r.json();
    $('#prov-active').value = d.active || 'none';
    const t = d.tomorrow || {}, w = d.weatherapi || {};
    $('#tio-refresh').value = t.refresh_sec || 14400;
    $('#tio-key').value = t.api_key || '';
    $('#tio-key-info').textContent = t.api_key ? `${t.api_key.length} chars guardados` : 'sin clave';
    $('#wap-refresh').value = w.refresh_sec || 1800;
    $('#wap-key').value = w.api_key || '';
    $('#wap-key-info').textContent = w.api_key ? `${w.api_key.length} chars guardados` : 'sin clave';
  }catch(e){}
}
$('#prov-save').onclick = async () => {
  const active = $('#prov-active').value;
  const body = {
    active,
    tomorrow:   { api_key: $('#tio-key').value.trim(), refresh_sec: +$('#tio-refresh').value },
    weatherapi: { api_key: $('#wap-key').value.trim(), refresh_sec: +$('#wap-refresh').value },
  };
  try{
    const r = await fetch('/api/weather_provider', {method:'POST', headers:{'Content-Type':'application/json'},
      body: JSON.stringify(body)});
    const d = await r.json();
    if (!d.ok) throw new Error(d.error || 'fallo');
    const label = d.active === 'tomorrow' ? 'Tomorrow.io'
                : d.active === 'weatherapi' ? 'WeatherAPI'
                : 'Open-Meteo';
    setMsg(`Provider activo: ${label}`, 'ok');
    loadProvider();
    loadWeather();
  }catch(e){ setMsg('Error: '+e.message, 'err'); }
};

loadStatus(); loadWifi(); loadConfig(); loadWeather(); loadProvider();
startPolls();
</script>
</body></html>
)WTHTML";

const size_t INDEX_HTML_LEN = sizeof(INDEX_HTML) - 1;
