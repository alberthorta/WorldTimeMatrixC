#include "IndexHtml.h"

const char INDEX_HTML[] PROGMEM = R"WTHTML(<!DOCTYPE html>
<html lang="es"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>WorldTime FW</title>
<style>
*{box-sizing:border-box}
body{font-family:system-ui,sans-serif;background:#0f172a;color:#e2e8f0;margin:0;padding:1.5rem;max-width:48rem}
h1{margin:0 0 1rem 0}
h2{margin:0 0 .75rem 0;font-size:1.05rem}
section{background:#1e293b;border:1px solid #334155;border-radius:.75rem;padding:1.25rem;margin-bottom:1rem}
button{background:#1e293b;border:1px solid #334155;color:#e2e8f0;padding:.4rem .9rem;border-radius:.4rem;cursor:pointer;font-size:.875rem}
button:hover{background:#334155}
button.primary{background:#059669;border-color:#059669}
button.primary:hover{background:#10b981}
button.danger{color:#fca5a5;border-color:#7f1d1d}
input,select{background:#0f172a;border:1px solid #334155;color:#e2e8f0;padding:.35rem .55rem;border-radius:.4rem;font-family:monospace;font-size:.875rem}
input[type=range]{padding:0;width:100%}
input[type=color]{padding:0;width:2rem;height:1.6rem;cursor:pointer;border:1px solid #334155}
input[type=checkbox]{accent-color:#10b981}
.net{display:flex;justify-content:space-between;padding:.4rem .6rem;background:#0f172a;border-radius:.3rem;margin-bottom:.2rem;cursor:pointer;font-size:.875rem}
.net:hover{background:#334155}
code{background:#0f172a;padding:.1rem .3rem;border-radius:.2rem;font-size:.875em}
.ok{color:#34d399}.warn{color:#fbbf24}.err{color:#f87171}
.row{display:flex;gap:.5rem;flex-wrap:wrap;align-items:center}
.col{display:flex;flex-direction:column;gap:.25rem}
label span{font-size:.75rem;color:#94a3b8}
#msg{position:sticky;bottom:1rem;background:#0f172a;padding:.5rem;border-radius:.4rem;text-align:center;font-size:.875rem;min-height:1.2em;border:1px solid #334155}
.city-grid{display:grid;grid-template-columns:auto 4rem 1fr 1fr;gap:.5rem;align-items:center;margin-bottom:.4rem}
.city-grid input[data-k=name]{font-family:monospace}
table{width:100%;font-size:.85rem;border-collapse:collapse}
td,th{padding:.25rem .5rem;text-align:left;border-bottom:1px solid #334155}
.bright-val{color:#34d399;font-weight:600}
/* Icon editor */
#icon-grid{display:grid;grid-template-columns:repeat(5,1fr);gap:1px;background:#334155;padding:1px;border-radius:.3rem;width:160px;height:160px}
#icon-grid button{padding:0;border:0;border-radius:0}
#palette{display:grid;grid-template-columns:repeat(8,auto);gap:.4rem;width:fit-content}
.swatch{display:flex;flex-direction:column;align-items:center;gap:.15rem}
.swatch button{width:1.8rem;height:1.8rem;border-radius:.3rem;padding:0}
.swatch input[type=color]{width:1.8rem;height:.8rem}
.frame-tab{display:inline-flex;align-items:center;gap:.3rem;padding:.25rem .55rem;border-radius:.3rem;font-size:.75rem;background:#0f172a;border:1px solid #334155;cursor:pointer}
.frame-tab.active{background:#047857;border-color:#10b981;color:#fff}
.frame-tab button{padding:0;border:0;background:transparent;color:#fca5a5;cursor:pointer;font-size:.85rem}
</style>
</head>
<body>
<h1>WorldTime <span style="font-size:.75em;color:#64748b">(FW)</span></h1>

<section>
  <h2>Estado</h2>
  <div id="status">cargando...</div>
</section>

<section>
  <h2>WiFi</h2>
  <div id="wifi-status" style="margin-bottom:.5rem">-</div>
  <div class="row" style="margin-bottom:.5rem"><button id="scan">Buscar redes</button></div>
  <div id="nets" style="margin-bottom:.5rem"></div>
  <div class="row" style="margin-bottom:.5rem">
    <label class="col"><span>SSID</span><input id="ssid"/></label>
    <label class="col"><span>Password</span><input id="pwd" type="password"/></label>
  </div>
  <button class="primary" id="connect">Conectar y reiniciar</button>
</section>

<section>
  <h2>Brillo (dia)</h2>
  <input id="bright" type="range" min="5" max="100" step="5"/>
  <div style="display:flex;justify-content:space-between;font-size:.75rem;color:#94a3b8;margin-top:.25rem">
    <span>5%</span><span id="bright-val" class="bright-val">50%</span><span>100%</span>
  </div>
</section>

<section>
  <h2>Modo noche</h2>
  <div class="row" style="margin-bottom:.5rem">
    <label><input id="nm-en" type="checkbox"/> Activado</label>
  </div>
  <div class="row" style="margin-bottom:.5rem">
    <label class="col"><span>Inicio</span><input id="nm-start" type="time"/></label>
    <label class="col"><span>Fin</span><input id="nm-end" type="time"/></label>
  </div>
  <label class="col">
    <span>Brillo nocturno: <span id="nm-bright-val" class="bright-val">10%</span></span>
    <input id="nm-bright" type="range" min="5" max="100" step="5"/>
  </label>
</section>

<section>
  <h2>Ciudades</h2>
  <div id="cities"></div>
</section>

<section>
  <h2>Iconos</h2>
  <div class="row" style="margin-bottom:.5rem">
    <label class="col"><span>Editar</span><select id="icon-pick"></select></label>
    <button id="reset-icon" class="danger" style="align-self:end">Restablecer este icono</button>
  </div>
  <div class="row" style="margin-bottom:.5rem">
    <div id="icon-frames" class="row" style="gap:.3rem"></div>
    <button id="frame-add" style="font-size:.75rem">+ Frame</button>
    <button id="frame-play" style="font-size:.75rem;color:#34d399">▶ Play</button>
  </div>
  <div class="row" style="margin-bottom:.75rem">
    <label class="col"><span>Duracion frame (ms)</span>
      <input id="frame-ms" type="number" min="50" max="5000" step="50" style="width:6rem"/>
    </label>
  </div>
  <div class="row" style="align-items:flex-start;gap:1rem">
    <div id="icon-grid"></div>
    <div class="col" style="flex:1;gap:.5rem">
      <span>Paleta</span>
      <div id="palette"></div>
    </div>
  </div>
</section>

<section>
  <h2>Otros</h2>
  <div class="row" style="margin-bottom:.5rem">
    <label><input id="blink" type="checkbox"/> Dos puntos parpadeando</label>
  </div>
  <label class="col" style="max-width:14rem">
    <span>Refresco meteo (segundos)</span>
    <input id="refresh" type="number" min="30" max="3600" step="30"/>
  </label>
  <label class="col" style="max-width:14rem;margin-top:.5rem">
    <span>Orden RGB del panel (cambio requiere reinicio)</span>
    <select id="rgb-order">
      <option value="RGB">RGB (estandar)</option>
      <option value="RBG">RBG (G/B intercambiados)</option>
    </select>
  </label>
</section>

<section>
  <h2>Logs meteo</h2>
  <table id="weather"><tr><th>Ciudad</th><th>Offset</th><th>Temp</th><th>Code</th><th>Day</th></tr></table>
</section>

<section>
  <h2>Actualizar firmware (OTA)</h2>
  <div class="row" style="margin-bottom:.5rem">
    <input id="ota-file" type="file" accept=".bin"/>
    <button id="ota-upload" class="primary">Subir y reiniciar</button>
  </div>
  <div id="ota-progress" style="height:.5rem;background:#0f172a;border-radius:.3rem;overflow:hidden;display:none">
    <div id="ota-bar" style="height:100%;width:0;background:#10b981;transition:width 100ms"></div>
  </div>
  <p style="font-size:.75rem;color:#94a3b8;margin-top:.4rem">
    Sube el .bin generado con <code>pio run -e matrixportal_s3</code> (en
    <code>.pio/build/matrixportal_s3/firmware.bin</code>). Tarda ~1 min.
  </p>
</section>

<section>
  <h2>Backup / Restaurar</h2>
  <div class="row">
    <button id="cfg-export">Descargar config</button>
    <button id="cfg-import-btn">Cargar config...</button>
    <input id="cfg-import" type="file" accept="application/json,.json" style="display:none"/>
  </div>
  <p style="font-size:.75rem;color:#94a3b8;margin-top:.5rem">El JSON descargado contiene cities, brillo, modo noche, paleta e iconos. NO incluye creds WiFi.</p>
</section>

<div class="row" style="position:sticky;bottom:0;background:#0f172a;padding:.75rem 0;gap:.5rem">
  <button class="primary" id="save" style="flex:1">Guardar cambios</button>
  <button id="reload">Recargar</button>
  <button id="reset-dev" class="danger">Reiniciar device</button>
</div>

<div id="msg"></div>

<script>
const $ = s => document.querySelector(s);
let cfg = null, curIcon = 'SUN', curFrame = 0, curColor = 1;
let playTimer = null;

function setMsg(t, kind){
  const m = $('#msg');
  m.textContent = t || '';
  m.className = kind || '';
}
function fmtUp(s){ s=Math.floor(s); if(s<60)return s+'s'; if(s<3600)return Math.floor(s/60)+'m '+(s%60)+'s'; return Math.floor(s/3600)+'h '+Math.floor((s%3600)/60)+'m'; }
function intToHex(n){ return '#'+(n|0).toString(16).padStart(6,'0'); }
function hexToInt(h){ return parseInt(h.replace('#',''),16); }
function minsToHHMM(m){ const h=Math.floor(m/60),mm=m%60; return String(h).padStart(2,'0')+':'+String(mm).padStart(2,'0'); }
function hhmmToMins(s){ const [h,m]=s.split(':').map(Number); return h*60+m; }

async function loadStatus(){
  try{
    const r = await fetch('/api/status'); const d = await r.json();
    $('#status').innerHTML = `<code>${d.ip||'-'}</code> · uptime ${fmtUp(d.uptime_sec)} · heap ${(d.heap_free/1024).toFixed(1)}KB · psram ${(d.psram_free/1024).toFixed(1)}KB${d.rssi?' · '+d.rssi+' dBm':''}`;
  }catch(e){}
}
async function loadWifi(){
  try{
    const r = await fetch('/api/wifi'); const d = await r.json();
    let html = '';
    if (d.mode === 'sta') html = `<span class="ok">Conectado</span> a <code>${d.current_ssid}</code> · IP <code>${d.ip}</code>`;
    else if (d.mode === 'ap') html = `<span class="warn">Modo AP</span> · SSID <code>${d.ap_ssid}</code> · IP <code>${d.ip}</code>`;
    else html = `<span class="err">Sin conexion</span>`;
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
    $('#refresh').value = cfg.weather_refresh_sec;
    $('#rgb-order').value = cfg.rgb_order || 'RGB';
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
    row.className = 'city-grid';
    row.innerHTML = `
      <input type="color" data-i="${i}" data-k="color" value="${intToHex(c.color)}"/>
      <input data-i="${i}" data-k="name" value="${c.name||''}" maxlength="6"/>
      <input data-i="${i}" data-k="lat" type="number" step="0.000001" value="${c.lat}"/>
      <input data-i="${i}" data-k="lon" type="number" step="0.000001" value="${c.lon}"/>`;
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

// --- Icon editor ---
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
    lbl.textContent = `F${i+1} (${f.ms||500}ms)`;
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
  // El default lo proporciona el server; pedimos el config global de nuevo.
  // Simplificacion: solo borramos frames extras y dejamos uno vacio (el usuario re-edita).
  // Para defaults reales, recargamos pagina tras hacer un POST sin este icono y recargando.
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
      s.textContent = 'transp';
      s.style.fontSize = '.6rem'; s.style.color = '#64748b';
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

// --- Play ---
function stopPlay(){
  if (playTimer) clearTimeout(playTimer);
  playTimer = null;
  $('#frame-play').textContent = '▶ Play';
}
function startPlay(){
  const fr = curFrames();
  if (!fr || fr.length < 2) { stopPlay(); return; }
  $('#frame-play').textContent = '⏸ Pausa';
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

// --- Weather logs ---
async function loadWeather(){
  try{
    const r = await fetch('/api/weather'); const d = await r.json();
    let html = '<tr><th>Ciudad</th><th>Offset</th><th>Temp</th><th>Code</th><th>Day</th></tr>';
    d.cities.forEach(c => {
      html += `<tr><td>${c.name}</td><td>${c.has_data?(c.offset_sec/3600).toFixed(1)+'h':'-'}</td><td>${c.has_data?c.temp_c+'°':'-'}</td><td>${c.has_data?c.code:'-'}</td><td>${c.has_data?(c.is_day?'☀':'🌙'):'-'}</td></tr>`;
    });
    $('#weather').innerHTML = html;
  }catch(e){}
}

// --- WiFi handlers ---
$('#scan').onclick = async () => {
  const btn = $('#scan'); btn.disabled = true; btn.textContent = 'Buscando...';
  try{
    const r = await fetch('/api/wifi/scan'); const d = await r.json();
    const box = $('#nets'); box.innerHTML = '';
    (d.networks || []).forEach(n => {
      const div = document.createElement('div');
      div.className = 'net';
      div.innerHTML = `<span>${n.ssid} ${n.secure?'🔒':''}</span><span>${n.rssi} dBm</span>`;
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

// --- Brightness live ---
let brightTimer;
$('#bright').addEventListener('input', e => {
  $('#bright-val').textContent = e.target.value+'%';
  clearTimeout(brightTimer);
  brightTimer = setTimeout(() => {
    fetch('/api/brightness', {method:'POST', headers:{'Content-Type':'application/json'},
      body: JSON.stringify({brightness: e.target.value/100})}).catch(()=>{});
  }, 80);
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

// --- Save ---
$('#save').onclick = async () => {
  const patch = {
    brightness: $('#bright').value/100,
    weather_refresh_sec: +$('#refresh').value,
    colon_blink: $('#blink').checked,
    cities: cfg.cities,
    night_mode: {
      enabled: $('#nm-en').checked,
      start_mins: hhmmToMins($('#nm-start').value),
      end_mins: hhmmToMins($('#nm-end').value),
      brightness: $('#nm-bright').value/100,
    },
    palette: cfg.palette,
    icons: cfg.icons,
    rgb_order: $('#rgb-order').value,
  };
  try{
    const r = await fetch('/api/config', {method:'POST', headers:{'Content-Type':'application/json'},
      body: JSON.stringify(patch)});
    const d = await r.json();
    if (d.error) throw new Error(d.error);
    setMsg('Guardado.' + (d.cities_changed ? ' (refresh meteo en curso)' : ''), 'ok');
    loadWeather();
  }catch(e){ setMsg('Error: '+e.message, 'err'); }
};

// --- Backup / Restaurar ---
$('#cfg-export').onclick = async () => {
  try{
    const r = await fetch('/api/config'); const txt = await r.text();
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

// --- OTA upload ---
$('#ota-upload').onclick = () => {
  const f = $('#ota-file').files[0];
  if (!f) { setMsg('Selecciona un .bin primero', 'err'); return; }
  if (!confirm(`Subir ${f.name} (${(f.size/1024).toFixed(1)} KB) y reiniciar?`)) return;

  const fd = new FormData();
  fd.append('firmware', f);

  const xhr = new XMLHttpRequest();
  xhr.open('POST', '/api/firmware');

  $('#ota-progress').style.display = 'block';
  $('#ota-bar').style.width = '0%';
  $('#ota-upload').disabled = true;
  setMsg('Subiendo firmware...');

  xhr.upload.onprogress = e => {
    if (e.lengthComputable) {
      const pct = Math.round((e.loaded/e.total)*100);
      $('#ota-bar').style.width = pct+'%';
      setMsg(`Subiendo ${pct}% (${(e.loaded/1024).toFixed(0)}/${(e.total/1024).toFixed(0)} KB)`);
    }
  };
  xhr.onload = () => {
    $('#ota-upload').disabled = false;
    if (xhr.status === 200) {
      setMsg('Firmware aceptado. Reiniciando — recarga en ~15s.', 'ok');
      setTimeout(() => location.reload(), 15000);
    } else {
      setMsg('Error '+xhr.status+': '+xhr.responseText, 'err');
    }
  };
  xhr.onerror = () => {
    $('#ota-upload').disabled = false;
    setMsg('Error de red durante la subida', 'err');
  };
  xhr.send(fd);
};

$('#reload').onclick = () => location.reload();
$('#reset-dev').onclick = async () => {
  if (!confirm('Reiniciar el device?')) return;
  try{ await fetch('/api/reset', {method:'POST'}); }catch(e){}
  setMsg('Reiniciando...', 'warn');
  setTimeout(() => location.reload(), 6000);
};

loadStatus(); loadWifi(); loadConfig(); loadWeather();
setInterval(loadStatus, 5000);
setInterval(loadWeather, 10000);
</script>
</body></html>
)WTHTML";

const size_t INDEX_HTML_LEN = sizeof(INDEX_HTML) - 1;
