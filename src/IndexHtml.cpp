#include "IndexHtml.h"

const char INDEX_HTML[] PROGMEM = R"WTHTML(<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover">
<meta name="color-scheme" content="light dark">
<title>Pixelario</title>
<script>
// Tema antes de pintar nada, para que no parpadee al recargar en oscuro.
try { const t = localStorage.getItem('theme'); if (t === 'light' || t === 'dark') document.documentElement.dataset.theme = t; } catch (e) {}
</script>
<style>
/* Instrumento de laboratorio: grafito, ambar de LED como acento y un panel
   de puntos en la cabecera. Solo fuentes del sistema: en modo AP (sin
   internet) la pagina tiene que verse igual de terminada. */
:root{
  --bg:#f2f3f5; --surface:#ffffff; --surface-2:#f6f7f9; --surface-3:#eceef2;
  --line:#e3e6eb; --line-2:#d2d7de;
  --text:#14181e; --text-2:#323a46; --muted:#646e7e; --muted-2:#8b94a3;
  --accent:#d98316; --accent-hi:#b86c0b; --accent-ink:#1f1300; --accent-soft:rgba(217,131,22,.12);
  --ok:#17935c; --ok-soft:rgba(23,147,92,.12);
  --warn:#b97f00; --warn-soft:rgba(185,127,0,.13);
  --err:#d23f3f; --err-soft:rgba(210,63,63,.10);
  --info:#2a73c8;
  --clawd:#e07a2f;
  --panel:#07080a; --led-off:#17191e;
  --shadow:0 1px 2px rgba(16,24,40,.05),0 1px 3px rgba(16,24,40,.06);
  --shadow-lg:0 12px 32px -8px rgba(16,24,40,.22);
  --ring:0 0 0 3px rgba(217,131,22,.25);
  --sans:ui-sans-serif,system-ui,-apple-system,"SF Pro Text","Segoe UI",Roboto,"Helvetica Neue",sans-serif;
  --mono:ui-monospace,"SF Mono",SFMono-Regular,Menlo,Consolas,monospace;
  --radius:14px; --radius-sm:9px;
  --side-w:252px;
  color-scheme:light;
}
@media (prefers-color-scheme:dark){
  :root:not([data-theme="light"]){
    --bg:#0b0d10; --surface:#12161b; --surface-2:#171c22; --surface-3:#1e242c;
    --line:#222932; --line-2:#2d3540;
    --text:#e8ebf0; --text-2:#c5ccd6; --muted:#8a94a3; --muted-2:#646e7e;
    --accent:#f4a93b; --accent-hi:#ffc164; --accent-ink:#1f1300; --accent-soft:rgba(244,169,59,.13);
    --ok:#3ccf8e; --ok-soft:rgba(60,207,142,.12);
    --warn:#f2c14b; --warn-soft:rgba(242,193,75,.12);
    --err:#ff6b6b; --err-soft:rgba(255,107,107,.12);
    --info:#63a8ff;
    --led-off:#16181d;
    --shadow:0 1px 2px rgba(0,0,0,.3);
    --shadow-lg:0 16px 40px -10px rgba(0,0,0,.6);
    --ring:0 0 0 3px rgba(244,169,59,.28);
    color-scheme:dark;
  }
}
:root[data-theme="dark"]{
  --bg:#0b0d10; --surface:#12161b; --surface-2:#171c22; --surface-3:#1e242c;
  --line:#222932; --line-2:#2d3540;
  --text:#e8ebf0; --text-2:#c5ccd6; --muted:#8a94a3; --muted-2:#646e7e;
  --accent:#f4a93b; --accent-hi:#ffc164; --accent-ink:#1f1300; --accent-soft:rgba(244,169,59,.13);
  --ok:#3ccf8e; --ok-soft:rgba(60,207,142,.12);
  --warn:#f2c14b; --warn-soft:rgba(242,193,75,.12);
  --err:#ff6b6b; --err-soft:rgba(255,107,107,.12);
  --info:#63a8ff;
  --led-off:#16181d;
  --shadow:0 1px 2px rgba(0,0,0,.3);
  --shadow-lg:0 16px 40px -10px rgba(0,0,0,.6);
  --ring:0 0 0 3px rgba(244,169,59,.28);
  color-scheme:dark;
}

*{box-sizing:border-box}
html,body{margin:0;padding:0}
body{
  font-family:var(--sans);font-size:14px;line-height:1.5;
  background:var(--bg);color:var(--text);
  -webkit-font-smoothing:antialiased;
}
button,input,select{font:inherit;color:inherit}
code{font-family:var(--mono);font-size:.86em;background:var(--surface-3);padding:.08rem .35rem;border-radius:5px;color:var(--text-2)}
a{color:var(--accent-hi)}
.hidden,[hidden]{display:none!important}
.mono{font-family:var(--mono);font-variant-numeric:tabular-nums}

/* ── Esqueleto: barra lateral + contenido ─────────────────────────── */
.app{display:grid;grid-template-columns:var(--side-w) minmax(0,1fr);min-height:100vh}
.sidebar{
  position:sticky;top:0;height:100vh;overflow-y:auto;min-width:0;
  display:flex;flex-direction:column;gap:22px;
  padding:22px 16px 18px;border-right:1px solid var(--line);background:var(--surface);
}
.brand{display:flex;flex-direction:column;gap:12px;padding:0 6px}
.led{
  display:block;width:100%;max-width:220px;height:auto;aspect-ratio:26/9;
  background:var(--panel);border-radius:10px;padding:0;
  box-shadow:inset 0 0 0 1px rgba(255,255,255,.04),0 6px 18px -8px rgba(0,0,0,.5);
}
.brand-name{display:flex;align-items:baseline;justify-content:space-between;gap:8px}
.brand-name b{font-size:17px;font-weight:650;letter-spacing:-.01em}
.brand-host{font-family:var(--mono);font-size:12px;color:var(--muted)}
.fw{font-family:var(--mono);font-size:11px;color:var(--muted);border:1px solid var(--line-2);border-radius:999px;padding:1px 8px;white-space:nowrap}

.nav{display:flex;flex-direction:column;gap:2px}
.nav button{
  display:flex;align-items:center;gap:11px;width:100%;
  padding:8px 10px;border:0;border-radius:9px;background:transparent;
  color:var(--text-2);font-size:14px;font-weight:500;text-align:left;cursor:pointer;
  transition:background .12s,color .12s;
}
.nav button svg{width:18px;height:18px;flex:none;color:var(--muted)}
.nav button:hover{background:var(--surface-3)}
.nav button.active{background:var(--accent-soft);color:var(--text)}
.nav button.active svg{color:var(--accent)}
.nav button:focus-visible{outline:none;box-shadow:var(--ring)}
.subnav{display:flex;flex-direction:column;gap:2px;margin:0 0 4px 19px;padding-left:10px;border-left:1px solid var(--line-2)}
.subnav button{padding:6px 10px;font-size:13.5px}
.subnav button svg{width:16px;height:16px}
.nav button.open{color:var(--text)}
.nav button.open svg{color:var(--accent)}

.side-foot{margin-top:auto;display:flex;flex-direction:column;gap:14px;padding:0 6px}
.side-status{display:grid;grid-template-columns:auto 1fr;gap:5px 12px;font-size:12.5px}
.side-status dt{color:var(--muted)}
.side-status dd{margin:0;font-family:var(--mono);font-variant-numeric:tabular-nums;text-align:right;color:var(--text-2);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.theme-switch{display:flex;background:var(--surface-3);border-radius:9px;padding:3px;gap:2px}
.theme-switch button{flex:1;border:0;background:transparent;border-radius:7px;padding:5px 0;cursor:pointer;color:var(--muted);display:flex;justify-content:center}
.theme-switch button svg{width:16px;height:16px}
.theme-switch button.on{background:var(--surface);color:var(--text);box-shadow:var(--shadow)}

.main{padding:34px 40px 140px;min-width:0}
.content{max-width:880px;margin:0 auto;display:flex;flex-direction:column;gap:16px}
.page-head{margin:0 0 6px}
.page-head h1{margin:0;font-size:26px;font-weight:680;letter-spacing:-.02em;text-wrap:balance}
.page-head p{margin:4px 0 0;color:var(--muted);max-width:62ch}
.group-title{margin:14px 0 -4px;font-size:11.5px;font-weight:600;text-transform:uppercase;letter-spacing:.09em;color:var(--muted-2)}

/* ── Movil: cabecera arriba y navegacion en una tira horizontal ──── */
@media (max-width:899px){
  .app{grid-template-columns:minmax(0,1fr);grid-template-rows:auto 1fr}
  .sidebar{
    position:sticky;top:0;z-index:20;height:auto;overflow:visible;
    flex-direction:column;gap:10px;padding:calc(10px + env(safe-area-inset-top,0px)) 0 0;
    border-right:0;border-bottom:1px solid var(--line);
    background:color-mix(in srgb,var(--surface) 88%,transparent);
    backdrop-filter:saturate(1.4) blur(14px);-webkit-backdrop-filter:saturate(1.4) blur(14px);
  }
  .brand{flex-direction:row;align-items:center;gap:12px;padding:0 16px}
  .led{width:84px;flex:none;border-radius:7px}
  .brand-name{flex:1;flex-direction:column;align-items:flex-start;gap:0}
  .brand-name b{font-size:15px}
  .nav{flex-direction:row;overflow-x:auto;gap:4px;padding:0 12px 10px;scrollbar-width:none}
  .nav::-webkit-scrollbar{display:none}
  .nav button{width:auto;flex:none;padding:7px 12px;border-radius:999px;font-size:13.5px;gap:7px}
  .nav button svg{width:16px;height:16px}
  .subnav{display:none;flex-direction:row;gap:4px;margin:0;padding:0 0 0 4px;border-left:0;flex:none}
  .subnav button{background:var(--surface-2);box-shadow:inset 0 0 0 1px var(--line)}
  .nav[data-group="modes"] .subnav[data-parent="modes"],.nav[data-group="weather"] .subnav[data-parent="weather"]{display:flex}
  .side-foot{display:none}
  .main{padding:20px 16px 150px}
  .page-head h1{font-size:22px}
}

/* ── Tarjetas ──────────────────────────────────────────────────────── */
.card{
  background:var(--surface);border:1px solid var(--line);border-radius:var(--radius);
  padding:20px;box-shadow:var(--shadow);min-width:0;
}
.card-head{display:flex;align-items:flex-start;justify-content:space-between;gap:14px;margin-bottom:16px}
.card-head h2{margin:0;font-size:15px;font-weight:620;letter-spacing:-.005em}
.card-head p{margin:3px 0 0;color:var(--muted);font-size:13px;max-width:60ch}
.card-head .actions{display:flex;gap:8px;flex:none;align-items:center}
.card-head:last-child{margin-bottom:0}
.note{font-size:12.5px;color:var(--muted);margin:12px 0 0;line-height:1.55}
.split{display:grid;gap:16px;grid-template-columns:repeat(auto-fit,minmax(min(100%,300px),1fr))}

/* Filas de ajustes: texto a la izquierda, control a la derecha. */
.settings{display:flex;flex-direction:column}
.setting{display:flex;align-items:center;justify-content:space-between;gap:16px;padding:12px 0;border-top:1px solid var(--line)}
.setting:first-child{border-top:0;padding-top:0}
.setting:last-child{padding-bottom:0}
.setting .t{min-width:0}
.setting .t b{display:block;font-weight:550;font-size:14px}
.setting .t span{display:block;color:var(--muted);font-size:12.5px}
.setting > .ctl{flex:none;display:flex;align-items:center;gap:8px}

/* ── Formularios ───────────────────────────────────────────────────── */
.fields{display:grid;gap:14px;grid-template-columns:repeat(auto-fit,minmax(min(100%,210px),1fr))}
.fields:has(> :only-child){max-width:360px}
.field{display:flex;flex-direction:column;gap:6px;min-width:0}
.field > span,.label{font-size:12.5px;font-weight:550;color:var(--text-2)}
.field small{font-size:12px;color:var(--muted)}
:where(input:not([type=color]):not([type=range]):not([type=checkbox]):not([type=file]),select){
  width:100%;height:38px;padding:0 11px;
  background:var(--surface-2);border:1px solid var(--line-2);border-radius:var(--radius-sm);
  color:var(--text);font-size:14px;outline:none;
  transition:border-color .12s,box-shadow .12s,background .12s;
}
input.mono,.mono input,input[type=number],input[type=time]{font-family:var(--mono);font-variant-numeric:tabular-nums}
:where(input:not([type=color]):not([type=range]):not([type=checkbox]):not([type=file]),select):hover{border-color:var(--muted-2)}
:where(input:not([type=color]):not([type=range]):not([type=checkbox]):not([type=file]),select):focus{border-color:var(--accent);box-shadow:var(--ring);background:var(--surface)}
:where(select){
  appearance:none;-webkit-appearance:none;padding-right:32px;cursor:pointer;
  background-image:url("data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' width='12' height='12' viewBox='0 0 24 24' fill='none' stroke='%238a94a3' stroke-width='2.5' stroke-linecap='round' stroke-linejoin='round'%3E%3Cpath d='m6 9 6 6 6-6'/%3E%3C/svg%3E");
  background-repeat:no-repeat;background-position:right 11px center;
}
input[type=range]{-webkit-appearance:none;appearance:none;width:100%;height:22px;margin:0;background:transparent;cursor:pointer;--fill:50%}
input[type=range]::-webkit-slider-runnable-track{height:6px;border-radius:999px;background:linear-gradient(var(--accent),var(--accent)) 0 0/var(--fill) 100% no-repeat,var(--surface-3)}
input[type=range]::-moz-range-track{height:6px;border-radius:999px;background:var(--surface-3)}
input[type=range]::-moz-range-progress{height:6px;border-radius:999px;background:var(--accent)}
input[type=range]::-webkit-slider-thumb{-webkit-appearance:none;width:18px;height:18px;margin-top:-6px;border-radius:50%;background:#fff;border:2px solid var(--accent);box-shadow:0 1px 3px rgba(0,0,0,.25)}
input[type=range]::-moz-range-thumb{width:14px;height:14px;border-radius:50%;background:#fff;border:2px solid var(--accent)}
input[type=range]:focus-visible{outline:none}
input[type=range]:focus-visible::-webkit-slider-thumb{box-shadow:var(--ring)}
input[type=checkbox]{accent-color:var(--accent);width:16px;height:16px;cursor:pointer;margin:0}
input[type=color]{
  -webkit-appearance:none;appearance:none;width:38px;height:38px;padding:3px;flex:none;
  border:1px solid var(--line-2);border-radius:var(--radius-sm);background:var(--surface-2);cursor:pointer;
}
input[type=color]::-webkit-color-swatch-wrapper{padding:0}
input[type=color]::-webkit-color-swatch{border:0;border-radius:6px}
input[type=color]::-moz-color-swatch{border:0;border-radius:6px}
input[type=file]{font-size:13px;color:var(--muted);max-width:100%}
input[type=file]::file-selector-button{
  margin-right:10px;height:34px;padding:0 14px;border-radius:8px;cursor:pointer;
  border:1px solid var(--line-2);background:var(--surface-3);color:var(--text);font:inherit;font-weight:550;
}
.color-field{display:flex;align-items:center;gap:10px}
.color-field code{font-size:12px}

/* Interruptor */
.toggle{position:relative;display:inline-flex;width:40px;height:23px;flex:none;cursor:pointer}
.toggle input{position:absolute;opacity:0;width:100%;height:100%;margin:0;cursor:pointer;z-index:1}
.toggle-slider{position:absolute;inset:0;background:var(--line-2);border-radius:999px;transition:background .18s}
.toggle-slider::before{
  content:"";position:absolute;width:17px;height:17px;left:3px;top:3px;border-radius:50%;
  background:#fff;box-shadow:0 1px 3px rgba(0,0,0,.3);transition:transform .18s cubic-bezier(.3,.7,.4,1);
}
.toggle input:checked + .toggle-slider{background:var(--accent)}
.toggle input:checked + .toggle-slider::before{transform:translateX(17px)}
.toggle input:focus-visible + .toggle-slider{box-shadow:var(--ring)}

/* Botones */
.btn{
  display:inline-flex;align-items:center;justify-content:center;gap:7px;
  height:36px;padding:0 14px;border-radius:var(--radius-sm);
  border:1px solid var(--line-2);background:var(--surface);color:var(--text);
  font-size:13.5px;font-weight:560;cursor:pointer;white-space:nowrap;
  transition:background .12s,border-color .12s,transform .06s,box-shadow .12s;
}
.btn:hover{background:var(--surface-3)}
.btn:active{transform:translateY(1px)}
.btn:focus-visible{outline:none;box-shadow:var(--ring)}
.btn:disabled{opacity:.5;cursor:not-allowed;transform:none}
.btn svg{width:16px;height:16px}
.btn-sm{height:30px;padding:0 11px;font-size:12.5px;border-radius:8px}
.btn-primary{background:var(--accent);border-color:var(--accent);color:var(--accent-ink)}
.btn-primary:hover{background:var(--accent-hi);border-color:var(--accent-hi)}
.btn-ghost{background:transparent;border-color:transparent;color:var(--muted)}
.btn-ghost:hover{background:var(--surface-3);color:var(--text)}
.btn-danger{color:var(--err);border-color:color-mix(in srgb,var(--err) 35%,transparent);background:transparent}
.btn-danger:hover{background:var(--err-soft)}
.btn-row{display:flex;flex-wrap:wrap;gap:8px;align-items:center}

/* Pastillas de estado */
.pill{display:inline-flex;align-items:center;gap:7px;height:24px;padding:0 10px;border-radius:999px;font-size:12.5px;font-weight:560;white-space:nowrap}
.pill-dot{width:7px;height:7px;border-radius:50%;background:currentColor}
.pill-ok{background:var(--ok-soft);color:var(--ok)}
.pill-warn{background:var(--warn-soft);color:var(--warn)}
.pill-err{background:var(--err-soft);color:var(--err)}
.pill-mute{background:var(--surface-3);color:var(--muted)}
.pill-ok .pill-dot{animation:pulse 2.4s ease-in-out infinite}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:.35}}
.msg-ok,.text-accent{color:var(--ok)}
.msg-warn,.text-warn{color:var(--warn)}
.msg-err{color:var(--err)}
.text-muted{color:var(--muted)}
.text-day{color:#e8a317}
.text-night{color:#8d8ff0}
.src-om{color:var(--ok)}
.src-tio{color:var(--info)}
.src-none{color:var(--muted-2)}

/* ── Resumen ───────────────────────────────────────────────────────── */
.stats{display:grid;gap:12px;grid-template-columns:repeat(auto-fit,minmax(min(100%,150px),1fr))}
.stat{background:var(--surface);border:1px solid var(--line);border-radius:var(--radius);padding:16px;box-shadow:var(--shadow);min-width:0}
.stat .k{font-size:12px;color:var(--muted);font-weight:550;display:flex;align-items:center;gap:6px}
.stat .k svg{width:15px;height:15px}
.stat .v{font-size:20px;font-weight:650;letter-spacing:-.015em;margin-top:6px;font-variant-numeric:tabular-nums;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.stat .s{font-size:12.5px;color:var(--muted);margin-top:2px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.bars{display:inline-flex;align-items:flex-end;gap:2px;height:14px;vertical-align:-1px}
.bars i{width:3px;border-radius:1px;background:var(--line-2)}
.bars i:nth-child(1){height:5px}.bars i:nth-child(2){height:8px}.bars i:nth-child(3){height:11px}.bars i:nth-child(4){height:14px}
.bars[data-l="1"] i:nth-child(-n+1),.bars[data-l="2"] i:nth-child(-n+2),.bars[data-l="3"] i:nth-child(-n+3),.bars[data-l="4"] i:nth-child(-n+4){background:var(--ok)}
.bars[data-l="1"] i:nth-child(-n+1){background:var(--err)}
.bars[data-l="2"] i:nth-child(-n+2){background:var(--warn)}

.remote{display:flex;align-items:center;justify-content:center;gap:22px;padding:10px 0 4px}
.rbtn{
  width:62px;height:62px;border-radius:50%;cursor:pointer;display:grid;place-items:center;
  background:radial-gradient(circle at 50% 35%,var(--surface),var(--surface-3));
  border:1px solid var(--line-2);color:var(--text-2);
  box-shadow:var(--shadow),inset 0 -2px 0 rgba(0,0,0,.06);
  transition:transform .08s,box-shadow .12s,color .12s;
}
.rbtn svg{width:22px;height:22px}
.rbtn:hover{color:var(--accent)}
.rbtn:active{transform:scale(.94)}
.rbtn:focus-visible{outline:none;box-shadow:var(--ring)}
.rbtn.center{width:76px;height:76px;border-color:color-mix(in srgb,var(--accent) 55%,var(--line-2));color:var(--accent)}
.remote-caption{display:flex;justify-content:center;gap:22px;font-size:12px;color:var(--muted);text-align:center}
.remote-caption span{width:62px}.remote-caption span:nth-child(2){width:76px}

/* ── Claude ────────────────────────────────────────────────────────── */
.usage{display:flex;align-items:baseline;gap:10px}
.usage b{font-size:30px;font-weight:680;letter-spacing:-.02em;font-variant-numeric:tabular-nums}
.meter{height:8px;border-radius:999px;background:var(--surface-3);overflow:hidden;margin-top:10px}
.meter > i{display:block;height:100%;width:0;border-radius:inherit;background:var(--clawd);transition:width .4s}
.anim-grid{display:grid;gap:12px;grid-template-columns:repeat(auto-fill,minmax(min(100%,168px),1fr))}
.anim{
  position:relative;display:flex;flex-direction:column;gap:8px;padding:10px;cursor:pointer;
  border:1px solid var(--line);border-radius:12px;background:var(--surface-2);
  transition:border-color .15s,background .15s,opacity .15s;
}
.anim:hover{border-color:var(--line-2)}
.anim canvas{display:block;width:100%;height:auto;aspect-ratio:21/14;border-radius:8px;background:var(--panel);transition:filter .2s,opacity .2s}
.anim .row{display:flex;align-items:center;justify-content:space-between;gap:8px}
.anim b{font-size:13.5px;font-weight:600}
.anim small{display:block;color:var(--muted);font-size:12px;line-height:1.4}
.anim:has(input:checked){border-color:color-mix(in srgb,var(--clawd) 45%,var(--line))}
.anim:not(:has(input:checked)) canvas{filter:grayscale(1);opacity:.35}
.anim:not(:has(input:checked)) b,.anim:not(:has(input:checked)) small{opacity:.6}
.anim:has(input:focus-visible){box-shadow:var(--ring)}

/* ── Listas: programaciones, botones, ciudades ─────────────────────── */
.list{display:flex;flex-direction:column;border:1px solid var(--line);border-radius:12px;overflow:hidden}
.list-row{display:flex;align-items:center;gap:10px;padding:9px 12px;border-top:1px solid var(--line);background:var(--surface);flex-wrap:wrap}
.list-row:first-child{border-top:0}
.list-row .idx{font-family:var(--mono);font-size:12px;color:var(--muted-2);width:18px;text-align:right;flex:none}
.list-row input[type=time]{width:112px;flex:none}
.list-row select{flex:1;min-width:140px}
.list-row .name{min-width:84px;font-weight:550}
.list-row:has(.toggle input:not(:checked)) > :not(.toggle):not(.idx):not(.name){opacity:.5}
.city-head,.city-row{display:grid;grid-template-columns:38px minmax(64px,.8fr) 1fr 1fr;gap:8px;align-items:center}
.city-head{font-size:12px;color:var(--muted);font-weight:550;padding:0 0 6px}
#cities{display:flex;flex-direction:column;gap:8px}

/* Tabla meteo */
.tbl-wrap{overflow-x:auto;border:1px solid var(--line);border-radius:12px}
.weather-tbl{width:100%;border-collapse:collapse;font-size:13px;font-family:var(--mono);font-variant-numeric:tabular-nums}
.weather-tbl th{text-align:left;padding:9px 12px;font-size:11px;font-family:var(--sans);font-weight:600;text-transform:uppercase;letter-spacing:.07em;color:var(--muted);background:var(--surface-2);border-bottom:1px solid var(--line);white-space:nowrap}
.weather-tbl td{padding:9px 12px;border-top:1px solid var(--line);white-space:nowrap}
.weather-tbl tr:first-child td{border-top:0}
.weather-tbl td:first-child{font-family:var(--sans);font-weight:600}
.icon-btn{
  width:26px;height:26px;border-radius:7px;border:1px solid var(--line-2);background:var(--surface);
  color:var(--text-2);cursor:pointer;font-size:12px;font-family:var(--sans);transition:all .12s;
}
.icon-btn:hover{border-color:var(--accent);color:var(--accent)}

/* Editor de iconos */
.icon-edit{display:flex;flex-wrap:wrap;gap:24px;align-items:flex-start}
#icon-grid{
  display:grid;grid-template-columns:repeat(5,1fr);gap:3px;padding:8px;flex:none;
  width:232px;height:232px;background:var(--panel);border-radius:12px;
}
#icon-grid button{border:0;padding:0;border-radius:5px;cursor:pointer;transition:transform .08s}
#icon-grid button:hover{transform:scale(1.06);box-shadow:0 0 0 2px rgba(255,255,255,.6)}
.transp{background:repeating-conic-gradient(#2a2f37 0 25%,#1d2128 0 50%) 0 0/10px 10px!important}
.frames{display:flex;flex-wrap:wrap;gap:6px;align-items:center}
.frame-tab{
  display:inline-flex;align-items:center;gap:4px;height:30px;padding:0 10px;border-radius:8px;cursor:pointer;
  border:1px solid var(--line-2);background:var(--surface);font-family:var(--mono);font-size:12px;color:var(--text-2);
}
.frame-tab.active{border-color:var(--accent);background:var(--accent-soft);color:var(--text)}
.frame-tab button{border:0;background:transparent;color:var(--err);cursor:pointer;font-size:15px;line-height:1;padding:0 0 0 2px}
#palette{display:grid;grid-template-columns:repeat(8,36px);gap:8px}
.swatch{display:flex;flex-direction:column;align-items:center;gap:4px}
.swatch button{width:36px;height:36px;border-radius:9px;padding:0;cursor:pointer;border:1px solid var(--line-2);transition:transform .08s}
.swatch button:hover{transform:scale(1.06)}
.swatch button.sel{box-shadow:0 0 0 2px var(--surface),0 0 0 4px var(--accent)}
.swatch input[type=color]{width:36px;height:14px;padding:0;border-radius:4px}
.swatch-tag{font-size:10.5px;color:var(--muted)}

/* Modo imagen */
.canvas-box{display:flex;flex-direction:column;gap:6px}
#userimg-src,#userimg-preview{border-radius:10px;background:var(--panel);max-width:100%;height:auto;border:1px solid var(--line)}
#userimg-src{cursor:move;touch-action:none}
#userimg-preview{image-rendering:pixelated}

/* WiFi */
#nets{display:flex;flex-direction:column;border:1px solid var(--line);border-radius:12px;overflow:hidden;margin-bottom:14px;max-height:15rem;overflow-y:auto}
#nets:empty{display:none}
.net{display:flex;justify-content:space-between;align-items:center;gap:10px;padding:10px 14px;cursor:pointer;border-top:1px solid var(--line);transition:background .12s}
.net:first-child{border-top:0}
.net:hover{background:var(--surface-2)}
.net-name{font-weight:550}
.net-rssi{font-family:var(--mono);font-size:12px}
.rssi-good{color:var(--ok)}.rssi-mid{color:var(--warn)}.rssi-bad{color:var(--err)}
.wifi-status{display:flex;flex-wrap:wrap;align-items:center;gap:8px;margin-bottom:14px;color:var(--text-2)}

/* OTA */
#ota-progress{height:8px;background:var(--surface-3);border-radius:999px;overflow:hidden;margin-top:14px}
#ota-bar{height:100%;width:0;background:var(--accent);transition:width .1s linear}

.kv{display:flex;align-items:center;gap:8px;padding:10px 12px;border-radius:10px;background:var(--surface-2);border:1px solid var(--line);font-size:13px}
.kv .text-muted{flex:none}

/* ── Barra de guardado (solo con cambios) y avisos ─────────────────── */
.savebar{
  position:fixed;z-index:40;left:calc(50% + var(--side-w) / 2);bottom:calc(18px + env(safe-area-inset-bottom,0px));
  transform:translate(-50%,160%);transition:transform .28s cubic-bezier(.3,.7,.3,1);
  display:flex;align-items:center;gap:10px;padding:8px 8px 8px 16px;
  background:var(--surface);border:1px solid var(--line-2);border-radius:999px;box-shadow:var(--shadow-lg);
  white-space:nowrap;
}
.savebar.show{transform:translate(-50%,0)}
.savebar .dot{width:8px;height:8px;border-radius:50%;background:var(--accent);box-shadow:0 0 0 4px var(--accent-soft)}
.savebar span{font-weight:560;font-size:13.5px;margin-right:6px}
.savebar .btn{border-radius:999px}
@media (max-width:899px){.savebar{left:50%;width:calc(100% - 24px);justify-content:flex-end}.savebar span{margin-right:auto}}
#msg{
  position:fixed;z-index:60;top:calc(16px + env(safe-area-inset-top,0px));right:16px;max-width:min(420px,calc(100% - 32px));
  padding:11px 16px;border-radius:12px;background:var(--surface);border:1px solid var(--line-2);box-shadow:var(--shadow-lg);
  font-size:13.5px;font-weight:550;color:var(--text);
  opacity:0;transform:translateY(-8px);pointer-events:none;transition:opacity .2s,transform .2s;
}
#msg.show{opacity:1;transform:none;pointer-events:auto}
#msg.msg-ok{border-left:4px solid var(--ok)}
#msg.msg-err{border-left:4px solid var(--err)}
#msg.msg-warn{border-left:4px solid var(--warn)}
@media (max-width:899px){#msg{left:16px;right:16px;max-width:none}}

/* Modal */
.modal{position:fixed;inset:0;z-index:70;display:flex;align-items:center;justify-content:center;padding:16px}
.modal.hidden{display:none}
.modal-backdrop{position:absolute;inset:0;background:rgba(5,7,10,.55);backdrop-filter:blur(3px);-webkit-backdrop-filter:blur(3px)}
.modal-card{position:relative;width:100%;max-width:44rem;max-height:86vh;display:flex;flex-direction:column;background:var(--surface);border:1px solid var(--line-2);border-radius:16px;box-shadow:var(--shadow-lg)}
.modal-head{display:flex;align-items:center;justify-content:space-between;padding:14px 18px;border-bottom:1px solid var(--line)}
.modal-head h3{margin:0;font-size:15px;font-weight:620}
.modal-body{padding:16px 18px;overflow-y:auto;display:flex;flex-direction:column;gap:14px}
.modal-meta{display:flex;flex-wrap:wrap;gap:8px 14px;font-size:12.5px;color:var(--muted);font-family:var(--mono)}
.modal-meta b{color:var(--text-2)}
.modal-tabs{display:flex;gap:4px;width:100%;background:var(--surface-3);padding:3px;border-radius:9px}
.modal-tabs button{flex:1;border:0;background:transparent;border-radius:7px;padding:6px;cursor:pointer;color:var(--muted);font-size:12.5px;font-family:var(--sans)}
.modal-tabs button.active{background:var(--surface);color:var(--text);box-shadow:var(--shadow)}
.modal-section pre{margin:6px 0 0;background:var(--surface-2);border:1px solid var(--line);border-radius:10px;padding:12px;font-family:var(--mono);font-size:12px;line-height:1.5;color:var(--text-2);white-space:pre-wrap;word-break:break-all;max-height:18rem;overflow:auto}

@media (prefers-reduced-motion:reduce){*{transition:none!important;animation:none!important}}
</style>
</head>
<body>
<svg width="0" height="0" style="position:absolute" aria-hidden="true">
  <defs>
    <symbol id="i-home" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M3 10.5 12 3l9 7.5"/><path d="M5 9.5V21h14V9.5"/><path d="M10 21v-6h4v6"/></symbol>
    <symbol id="i-display" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="2.5" y="5" width="19" height="12" rx="2"/><path d="M8 21h8M12 17v4"/><path d="M6 9h.01M9 9h.01M12 9h.01M6 12h.01M9 12h.01"/></symbol>
    <symbol id="i-modes" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M12 3 2.5 8 12 13l9.5-5L12 3Z"/><path d="m2.5 13 9.5 5 9.5-5"/></symbol>
    <symbol id="i-claude" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.2" stroke-linecap="round"><path d="M12 3v18M4.2 7.5l15.6 9M4.2 16.5l15.6-9"/></symbol>
    <symbol id="i-weather" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><path d="M7.5 19h9.5a4 4 0 0 0 .6-7.96A6 6 0 0 0 6.1 10 4.5 4.5 0 0 0 7.5 19Z"/></symbol>
    <symbol id="i-icons" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linejoin="round"><rect x="3.5" y="3.5" width="7" height="7" rx="1.5"/><rect x="13.5" y="3.5" width="7" height="7" rx="1.5"/><rect x="3.5" y="13.5" width="7" height="7" rx="1.5"/><rect x="13.5" y="13.5" width="7" height="7" rx="1.5"/></symbol>
    <symbol id="i-mouse" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><rect x="6" y="3" width="12" height="18" rx="6"/><path d="M12 7v4"/></symbol>
    <symbol id="i-system" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><circle cx="12" cy="12" r="3.2"/><path d="M12 2.5v2.6M12 18.9v2.6M2.5 12h2.6M18.9 12h2.6M5.3 5.3l1.8 1.8M16.9 16.9l1.8 1.8M5.3 18.7l1.8-1.8M16.9 7.1l1.8-1.8"/></symbol>
    <symbol id="i-auto" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2"><circle cx="12" cy="12" r="8.5"/><path d="M12 3.5v17a8.5 8.5 0 0 0 0-17Z" fill="currentColor"/></symbol>
    <symbol id="i-sun" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><circle cx="12" cy="12" r="4"/><path d="M12 2v2M12 20v2M2 12h2M20 12h2M4.9 4.9l1.4 1.4M17.7 17.7l1.4 1.4M4.9 19.1l1.4-1.4M17.7 6.3l1.4-1.4"/></symbol>
    <symbol id="i-moon" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linejoin="round"><path d="M20 14.5A8 8 0 1 1 9.5 4a6.5 6.5 0 0 0 10.5 10.5Z"/></symbol>
    <symbol id="i-left" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round"><path d="m14.5 6-6 6 6 6"/></symbol>
    <symbol id="i-right" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2.4" stroke-linecap="round" stroke-linejoin="round"><path d="m9.5 6 6 6-6 6"/></symbol>
    <symbol id="i-dot" viewBox="0 0 24 24"><circle cx="12" cy="12" r="5" fill="currentColor"/></symbol>
    <symbol id="i-wifi" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M2.5 9a14 14 0 0 1 19 0M5.5 12.5a9.5 9.5 0 0 1 13 0M8.6 16a5 5 0 0 1 6.8 0"/><path d="M12 19.5h.01"/></symbol>
    <symbol id="i-clock" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><circle cx="12" cy="12" r="8.5"/><path d="M12 7.5V12l3 2"/></symbol>
    <symbol id="i-chip" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><rect x="6" y="6" width="12" height="12" rx="2"/><path d="M9 2.5v3M15 2.5v3M9 18.5v3M15 18.5v3M2.5 9h3M2.5 15h3M18.5 9h3M18.5 15h3"/></symbol>
    <symbol id="i-life" viewBox="0 0 24 24" fill="currentColor"><rect x="9.5" y="3.5" width="5" height="5" rx="1"/><rect x="15.5" y="9.5" width="5" height="5" rx="1"/><rect x="3.5" y="15.5" width="5" height="5" rx="1"/><rect x="9.5" y="15.5" width="5" height="5" rx="1"/><rect x="15.5" y="15.5" width="5" height="5" rx="1"/></symbol>
    <symbol id="i-fire" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linejoin="round"><path d="M12 21c-4 0-6.5-2.7-6.5-6.2 0-3.6 3-5.6 3.5-9.3 2 1.3 3 3.2 3 5 1-.8 1.6-2 1.7-3.3 2.6 1.9 4.8 4.6 4.8 7.6 0 3.5-2.5 6.2-6.5 6.2Z"/></symbol>
    <symbol id="i-plasma" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><path d="M3 8c3-3 6 3 9 0s6-3 9 0M3 13c3-3 6 3 9 0s6-3 9 0M3 18c3-3 6 3 9 0s6-3 9 0"/></symbol>
    <symbol id="i-moire" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="1.8"><circle cx="9" cy="12" r="2.5"/><circle cx="9" cy="12" r="6"/><circle cx="15" cy="12" r="2.5"/><circle cx="15" cy="12" r="6"/></symbol>
    <symbol id="i-nyan" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linejoin="round" stroke-linecap="round"><path d="M5 19V6l4 4h6l4-4v13Z"/><path d="M9.5 14h.01M14.5 14h.01"/></symbol>
    <symbol id="i-image" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round" stroke-linejoin="round"><rect x="3" y="4.5" width="18" height="15" rx="2"/><circle cx="8.5" cy="9.5" r="1.6"/><path d="m21 16-5-5-9 8.5"/></symbol>
    <symbol id="i-tag" viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linejoin="round"><path d="M3 12V4h8l10 10-8 8L3 12Z"/><circle cx="7.5" cy="7.5" r="1.2" fill="currentColor"/></symbol>
  </defs>
</svg>

<div class="app">
<aside class="sidebar">
  <div class="brand">
    <canvas id="led" class="led" width="104" height="36" aria-label="Hora del panel"></canvas>
    <div class="brand-name">
      <div>
        <b>Pixelario</b>
        <div id="brand-host" class="brand-host">—</div>
      </div>
      <span id="brand-fw" class="fw">—</span>
    </div>
  </div>
  <nav class="nav" aria-label="Secciones">
    <button type="button" data-tab-btn="home"><svg><use href="#i-home"/></svg>Resumen</button>
    <button type="button" data-tab-btn="display"><svg><use href="#i-display"/></svg>Pantalla</button>
    <button type="button" data-tab-btn="modes"><svg><use href="#i-modes"/></svg>Modos</button>
    <div class="subnav" data-parent="modes">
      <button type="button" data-tab-btn="claude"><svg><use href="#i-claude"/></svg>Claude</button>
      <button type="button" data-tab-btn="life"><svg><use href="#i-life"/></svg>Game of Life</button>
      <button type="button" data-tab-btn="fire"><svg><use href="#i-fire"/></svg>Llama</button>
      <button type="button" data-tab-btn="plasma"><svg><use href="#i-plasma"/></svg>Plasma</button>
      <button type="button" data-tab-btn="moire"><svg><use href="#i-moire"/></svg>Moiré</button>
      <button type="button" data-tab-btn="nyan"><svg><use href="#i-nyan"/></svg>Nyan Cat</button>
      <button type="button" data-tab-btn="image"><svg><use href="#i-image"/></svg>Imagen</button>
    </div>
    <button type="button" data-tab-btn="weather"><svg><use href="#i-weather"/></svg>Meteo</button>
    <div class="subnav" data-parent="weather">
      <button type="button" data-tab-btn="icons"><svg><use href="#i-icons"/></svg>Iconos</button>
    </div>
    <button type="button" data-tab-btn="jitter"><svg><use href="#i-mouse"/></svg>Jitter</button>
    <button type="button" data-tab-btn="system"><svg><use href="#i-system"/></svg>Sistema</button>
  </nav>
  <div class="side-foot">
    <dl class="side-status">
      <dt>IP</dt><dd id="sb-ip">—</dd>
      <dt>Señal</dt><dd id="sb-rssi">—</dd>
      <dt>Encendido</dt><dd id="sb-up">—</dd>
      <dt>Memoria</dt><dd id="sb-heap">—</dd>
    </dl>
    <div class="theme-switch" role="group" aria-label="Tema">
      <button type="button" data-theme-btn="auto" title="Automático"><svg><use href="#i-auto"/></svg></button>
      <button type="button" data-theme-btn="light" title="Claro"><svg><use href="#i-sun"/></svg></button>
      <button type="button" data-theme-btn="dark" title="Oscuro"><svg><use href="#i-moon"/></svg></button>
    </div>
  </div>
</aside>

<main class="main">
<div class="content">

<!-- ═════════════════════════ Resumen ═════════════════════════ -->
<header class="page-head" data-tab="home">
  <h1>Resumen</h1>
  <p>Estado del panel en tiempo real y un mando para controlarlo desde aquí.</p>
</header>
<div class="stats" data-tab="home">
  <div class="stat"><div class="k"><svg><use href="#i-wifi"/></svg>Conexión</div><div class="v" id="ov-conn">—</div><div class="s" id="ov-conn-sub">&nbsp;</div></div>
  <div class="stat"><div class="k"><svg><use href="#i-tag"/></svg>Dirección</div><div class="v" id="ov-ip">—</div><div class="s" id="ov-host">&nbsp;</div></div>
  <div class="stat"><div class="k"><svg><use href="#i-clock"/></svg>Encendido</div><div class="v" id="ov-up">—</div><div class="s" id="ov-heap">&nbsp;</div></div>
  <div class="stat"><div class="k"><svg><use href="#i-chip"/></svg>Firmware</div><div class="v" id="ov-fw">—</div><div class="s" id="ov-fw-sub">&nbsp;</div></div>
  <div class="stat"><div class="k"><svg><use href="#i-moon"/></svg>Luna</div><div class="v" id="ov-moon">—</div><div class="s" id="ov-moon-sub">&nbsp;</div></div>
</div>
<section class="card" data-tab="home">
  <div class="card-head">
    <div>
      <h2>Mando</h2>
      <p>Hace lo mismo que los botones táctiles del panel. En el reloj: anterior, menú y siguiente. Dentro del menú, las flechas navegan o ajustan y el centro confirma o vuelve.</p>
    </div>
  </div>
  <div class="remote">
    <button id="btn-sim-left" type="button" class="rbtn" aria-label="Izquierda"><svg><use href="#i-left"/></svg></button>
    <button id="btn-sim-center" type="button" class="rbtn center" aria-label="Centro"><svg><use href="#i-dot"/></svg></button>
    <button id="btn-sim-right" type="button" class="rbtn" aria-label="Derecha"><svg><use href="#i-right"/></svg></button>
  </div>
  <div class="remote-caption"><span>Anterior</span><span>Menú / OK</span><span>Siguiente</span></div>
</section>

<!-- ═════════════════════════ Pantalla ═════════════════════════ -->
<header class="page-head" data-tab="display">
  <h1>Pantalla</h1>
  <p>Brillo, modo noche y cómo se dibuja el reloj.</p>
</header>
<section class="card" data-tab="display" data-save>
  <div class="card-head">
    <div><h2>Brillo</h2><p>Se aplica al momento mientras mueves el control.</p></div>
    <span class="usage"><b id="bright-val" style="font-size:24px">50%</b></span>
  </div>
  <input id="bright" type="range" min="5" max="100" step="5" aria-label="Brillo"/>
  <div style="margin-top:20px;padding-top:18px;border-top:1px solid var(--line)">
    <div class="card-head" style="margin-bottom:14px">
      <div><h2>Modo noche</h2><p>Baja el brillo en una franja horaria. Clawd se va a dormir.</p></div>
      <label class="toggle"><input id="nm-en" type="checkbox" aria-label="Modo noche"/><span class="toggle-slider"></span></label>
    </div>
    <div class="fields">
      <label class="field"><span>Desde</span><input id="nm-start" type="time"/></label>
      <label class="field"><span>Hasta</span><input id="nm-end" type="time"/></label>
      <div class="field">
        <span style="display:flex;justify-content:space-between">Brillo nocturno <span id="nm-bright-val" class="mono">10%</span></span>
        <input id="nm-bright" type="range" min="5" max="100" step="5" style="height:38px" aria-label="Brillo nocturno"/>
      </div>
    </div>
  </div>
</section>

<section class="card" data-tab="display" data-save>
  <div class="card-head"><div><h2>Reloj</h2></div></div>
  <div class="settings">
    <label class="setting"><div class="t"><b>Dos puntos parpadeando</b></div><span class="toggle"><input id="blink" type="checkbox"/><span class="toggle-slider"></span></span></label>
    <label class="setting"><div class="t"><b>Cero a la izquierda</b><span>07:05 en lugar de 7:05</span></div><span class="toggle"><input id="hour-lz" type="checkbox"/><span class="toggle-slider"></span></span></label>
    <label class="setting"><div class="t"><b>Fecha en texto</b><span>La fila $DATE muestra «8 May» en lugar de «08/05»</span></div><span class="toggle"><input id="date-text" type="checkbox"/><span class="toggle-slider"></span></span></label>
    <label class="setting"><div class="t"><b>Indicador Open-Meteo</b><span>Punto gris bajo el º cuando una fila usa Open-Meteo</span></div><span class="toggle"><input id="om-ind" type="checkbox"/><span class="toggle-slider"></span></span></label>
  </div>
</section>

<section class="card" data-tab="display" data-save>
  <div class="card-head"><div><h2>Segundero</h2><p>Solo en el modo de 4 filas.</p></div></div>
  <div class="fields">
    <label class="field">
      <span>Indicador</span>
      <select id="sec-indicator">
        <option value="none">Ninguno</option>
        <option value="marker">Marcador (3 px en la fila inferior)</option>
        <option value="bar">Barra vertical (detrás, de arriba abajo)</option>
      </select>
    </label>
    <label class="field" id="sec-bar-color-wrap">
      <span>Color de la barra</span>
      <span class="color-field"><input id="sec-bar-color" type="color" value="#333333"/></span>
    </label>
  </div>
  <div class="fields" id="sec-bar-extras" style="margin-top:14px">
    <label class="field"><span>Ancho de la barra (px)</span><input id="sec-bar-width" type="number" min="1" max="16" step="1" value="1"/></label>
    <label class="setting" style="border:0;padding:0;align-self:end;min-height:38px"><div class="t"><b>Rellenar lo recorrido</b><span>Como una barra de progreso</span></div><span class="toggle"><input id="sec-bar-progress" type="checkbox"/><span class="toggle-slider"></span></span></label>
  </div>
</section>

<section class="card" data-tab="display" data-save>
  <div class="card-head">
    <div><h2>Tendencia de temperatura</h2><p>2 px tras el º: verde si va a subir, rojo si va a bajar.</p></div>
    <label class="toggle"><input id="trend-en" type="checkbox"/><span class="toggle-slider"></span></label>
  </div>
  <div id="trend-extras">
    <div class="fields">
      <label class="field">
        <span>Horizonte de la previsión</span>
        <select id="trend-horizon"><option value="1">1 hora</option><option value="2">2 horas</option></select>
      </label>
      <div class="field">
        <span>Umbrales (°C) para 1, 2 y 3 px</span>
        <div style="display:flex;gap:8px">
          <input id="trend-th1" type="number" min="0" max="50" step="0.1" value="0.5"/>
          <input id="trend-th2" type="number" min="0" max="50" step="0.1" value="1.5"/>
          <input id="trend-th3" type="number" min="0" max="50" step="0.1" value="3"/>
        </div>
      </div>
    </div>
    <div class="fields" style="margin-top:14px">
      <label class="field"><span>Sube</span><span class="color-field"><input id="trend-color-up" type="color" value="#00C000"/></span></label>
      <label class="field"><span>Baja</span><span class="color-field"><input id="trend-color-down" type="color" value="#C00000"/></span></label>
      <label class="field"><span>Estable</span><span class="color-field"><input id="trend-color-stable" type="color" value="#666666"/></span></label>
    </div>
  </div>
</section>

<div class="split" data-tab="display" data-save>
  <section class="card">
    <div class="card-head"><div><h2>Colores de hora y fecha</h2><p>Modos Focus y Claude.</p></div></div>
    <div class="settings">
      <label class="setting"><div class="t"><b>Hora</b></div><input id="focus-hour-color" type="color" value="#FFFFFF"/></label>
      <label class="setting"><div class="t"><b>Fecha</b></div><input id="focus-date-color" type="color" value="#AAAAAA"/></label>
    </div>
  </section>
  <section class="card">
    <div class="card-head"><div><h2>Panel</h2><p>Si el verde y el azul salen cambiados, prueba RBG.</p></div></div>
    <label class="field">
      <span>Orden de colores <span class="text-warn">· reinicia al guardar</span></span>
      <select id="rgb-order">
        <option value="RGB">RGB (estándar)</option>
        <option value="RBG">RBG (verde y azul intercambiados)</option>
      </select>
    </label>
  </section>
</div>

<!-- ═════════════════════════ Modos ═════════════════════════ -->
<header class="page-head" data-tab="modes">
  <h1>Modos</h1>
  <p>Con qué modo arranca el panel y cuándo cambia solo. Los ajustes de cada modo están en sus apartados.</p>
</header>
<section class="card" data-tab="modes" data-save>
  <div class="card-head"><div><h2>Al arrancar</h2><p>Si eliges Claude sin sessionKey, arranca en 4 filas.</p></div></div>
  <div class="fields">
    <label class="field">
      <span>Modo inicial</span>
      <select id="startup-mode"></select>
    </label>
  </div>
</section>

<section class="card" data-tab="modes" data-save>
  <div class="card-head"><div><h2>Programaciones</h2><p>A la hora indicada el panel cambia de modo solo. Hora local de la primera ciudad.</p></div></div>
  <div id="schedule-list" class="list"></div>
</section>

<!-- ═════════════════════════ Game of Life ═════════════════════════ -->
<header class="page-head" data-tab="life">
  <h1>Game of Life</h1>
  <p>El juego de la vida de Conway en el panel. Si el patrón se estanca, vuelve a empezar solo.</p>
</header>
<section class="card" data-tab="life" data-save>
  <div class="settings">
    <label class="setting"><div class="t"><b>Arcoíris</b><span>El color avanza unos grados en cada paso</span></div><span class="toggle"><input id="life-rainbow" type="checkbox"/><span class="toggle-slider"></span></span></label>
    <label class="setting" id="life-color-row"><div class="t"><b>Color de las células</b></div><input id="life-color" type="color" value="#80C0FF"/></label>
    <div class="setting" style="flex-direction:column;align-items:stretch;gap:8px">
      <div style="display:flex;justify-content:space-between"><b style="font-weight:550">Velocidad</b><span class="mono text-muted"><span id="life-step-val">150</span> ms por paso</span></div>
      <input id="life-step" type="range" min="50" max="1000" step="10" value="150" aria-label="Velocidad"/>
    </div>
  </div>
</section>

<!-- ═════════════════════════ Llama ═════════════════════════ -->
<header class="page-head" data-tab="fire">
  <h1>Llama</h1>
  <p>El fuego del Doom subiendo por el panel, con la hora abajo.</p>
</header>
<section class="card" data-tab="fire" data-save>
  <div class="card-head"><div><h2>Paleta</h2><p>Solo para este modo.</p></div></div>
  <div class="settings">
    <label class="setting"><div class="t"><b>Paleta clásica</b><span>Naranja Doom. Si la quitas, se genera desde tu color (negro → color → blanco).</span></div><span class="toggle"><input id="fire-default" type="checkbox" checked/><span class="toggle-slider"></span></span></label>
    <label class="setting" id="fire-color-row"><div class="t"><b>Color base</b></div><input id="fire-color" type="color" value="#FF6000"/></label>
  </div>
</section>

<!-- ═════════════════════════ Plasma ═════════════════════════ -->
<header class="page-head" data-tab="plasma">
  <h1>Plasma</h1>
  <p>Ondas de color que se mezclan sin parar, con la hora abajo.</p>
</header>
<section class="card" data-tab="plasma" data-save>
  <div class="card-head"><div><h2>Paleta</h2><p>Solo para este modo.</p></div></div>
  <div class="settings">
    <label class="setting"><div class="t"><b>Paleta clásica</b><span>Naranja Doom. Si la quitas, se genera desde tu color (negro → color → blanco).</span></div><span class="toggle"><input id="plasma-default" type="checkbox" checked/><span class="toggle-slider"></span></span></label>
    <label class="setting" id="plasma-color-row"><div class="t"><b>Color base</b></div><input id="plasma-color" type="color" value="#FF6000"/></label>
  </div>
</section>

<!-- ═════════════════════════ Moiré ═════════════════════════ -->
<header class="page-head" data-tab="moire">
  <h1>Moiré</h1>
  <p>Dos series de círculos que se cruzan y crean interferencias, con la hora abajo.</p>
</header>
<section class="card" data-tab="moire" data-save>
  <div class="card-head"><div><h2>Paleta</h2><p>Solo para este modo.</p></div></div>
  <div class="settings">
    <label class="setting"><div class="t"><b>Paleta clásica</b><span>Naranja Doom. Si la quitas, se genera desde tu color (negro → color → blanco).</span></div><span class="toggle"><input id="moire-default" type="checkbox" checked/><span class="toggle-slider"></span></span></label>
    <label class="setting" id="moire-color-row"><div class="t"><b>Color base</b></div><input id="moire-color" type="color" value="#FF6000"/></label>
  </div>
</section>

<!-- ═════════════════════════ Nyan Cat ═════════════════════════ -->
<header class="page-head" data-tab="nyan">
  <h1>Nyan Cat</h1>
  <p>El gato del pop-tart cruzando el panel con su estela arcoíris, con la hora abajo.</p>
</header>
<section class="card" data-tab="nyan">
  <div class="card-head"><div><h2>Sin ajustes</h2><p>Los colores son los del original. Llega a él con los botones del panel o prográmalo en Modos.</p></div></div>
</section>

<!-- ═════════════════════════ Imagen ═════════════════════════ -->
<header class="page-head" data-tab="image">
  <h1>Imagen</h1>
  <p>Tu propia imagen en el panel. Sube una foto y encuadra la zona (proporción 64:23): arrastra el rectángulo y ajusta el zoom.</p>
</header>
<section class="card" data-tab="image">
  <input id="userimg-file" type="file" accept="image/*"/>
  <div style="display:flex;gap:16px;align-items:flex-start;margin-top:14px;flex-wrap:wrap">
    <div class="canvas-box"><span class="label">Original</span><canvas id="userimg-src" width="320" height="200"></canvas></div>
    <div class="canvas-box"><span class="label">Así se verá</span><canvas id="userimg-preview" width="256" height="92"></canvas></div>
  </div>
  <label class="field" style="margin-top:14px;max-width:340px">
    <span>Zoom · <span id="userimg-zoom-val" class="mono">100%</span></span>
    <input id="userimg-zoom" type="range" min="20" max="100" step="1" value="100"/>
  </label>
  <div class="btn-row" style="margin-top:14px">
    <button id="userimg-upload" type="button" class="btn btn-primary">Enviar al panel</button>
    <span id="userimg-status" class="text-muted"></span>
  </div>
</section>

<!-- ═════════════════════════ Claude ═════════════════════════ -->
<header class="page-head" data-tab="claude">
  <h1>Claude</h1>
  <p>Uso de tu cuenta de claude.ai y el modo con Clawd.</p>
</header>
<div class="split" data-tab="claude">
  <section class="card" data-save>
    <div class="card-head"><div><h2>Cuenta</h2><p>Cookie <code>sessionKey</code> de claude.ai (DevTools → Application → Cookies). Sin ella, el modo Claude no aparece.</p></div></div>
    <div class="fields">
      <label class="field" style="grid-column:1/-1"><span>sessionKey</span><input id="claude-session-key" class="mono" type="text" autocomplete="off" spellcheck="false" placeholder="sk-ant-sid01-…"/></label>
      <label class="field"><span>Refresco (segundos)</span><input id="claude-refresh" type="number" min="60" max="3600" step="30" value="180"/></label>
    </div>
  </section>
  <section class="card">
    <div class="card-head"><div><h2>Ventana de 5 horas</h2><p id="cl-use-sub">Esperando datos…</p></div></div>
    <div class="usage"><b id="cl-use-val">—</b><span class="text-muted">usado</span></div>
    <div class="meter"><i id="cl-use-bar"></i></div>
  </section>
</div>

<section class="card" data-tab="claude" data-save>
  <div class="card-head">
    <div><h2>Auto «hola»</h2><p>Manda un «hola» a claude.ai a una hora fija para abrir la ventana de 5 h (crea una conversación y la borra). Una vez al día, hora local de la primera ciudad.</p></div>
    <label class="toggle"><input type="checkbox" id="hola-en"><span class="toggle-slider"></span></label>
  </div>
  <div class="fields">
    <label class="field"><span>Hora</span><input id="hola-time" type="time"/></label>
    <div class="field"><span>&nbsp;</span><button id="hola-now" type="button" class="btn">Enviar «hola» ahora</button></div>
  </div>
  <div class="kv" style="margin-top:14px"><span class="text-muted">Último envío</span><span id="hola-status">—</span></div>
  <div class="settings" style="margin-top:14px">
    <label class="setting"><div class="t"><b>Mantener la sesión despierta</b><span>Cuando la ventana de 5 h se agota, manda otro «hola» para abrir la siguiente.</span></div><span class="toggle"><input type="checkbox" id="keepawake-en"><span class="toggle-slider"></span></span></label>
  </div>
</section>

<section class="card" data-tab="claude" data-save>
  <div class="card-head">
    <div><h2>Animaciones de Clawd</h2><p>Entre rato y rato de reposo, Clawd elige al azar entre las animaciones activas. Se aplica al guardar, sin reiniciar.</p></div>
    <div class="actions">
      <button type="button" class="btn btn-sm btn-ghost" data-anim-all="1">Todas</button>
      <button type="button" class="btn btn-sm btn-ghost" data-anim-all="0">Ninguna</button>
    </div>
  </div>
  <div class="anim-grid" id="anim-acts"></div>
  <h3 class="group-title" style="margin:20px 0 12px">Reacciones</h3>
  <div class="anim-grid" id="anim-reacts"></div>
</section>

<!-- ═════════════════════════ Meteo ═════════════════════════ -->
<header class="page-head" data-tab="weather">
  <h1>Meteo</h1>
  <p>Las cuatro filas del reloj y de dónde sale el tiempo.</p>
</header>
<section class="card" data-tab="weather" data-save>
  <div class="card-head"><div><h2>Ciudades</h2><p>Nombre de hasta 6 letras. <code>$DATE</code> como nombre muestra la fecha en esa fila.</p></div></div>
  <div class="city-head"><span>Color</span><span>Nombre</span><span>Latitud</span><span>Longitud</span></div>
  <div id="cities"></div>
  <div class="fields" style="margin-top:16px">
    <label class="field"><span>Refresco Open-Meteo (segundos)</span><input id="refresh" type="number" min="30" max="3600" step="30"/></label>
  </div>
</section>

<section class="card" data-tab="weather">
  <div class="card-head"><div><h2>Estado</h2><p>Ordenado por el próximo refresco del proveedor premium.</p></div></div>
  <div class="tbl-wrap">
    <table id="weather" class="weather-tbl">
      <thead>
        <tr><th>Ciudad</th><th>Offset</th><th>Temp</th><th>Code</th><th>Día</th><th>OM</th><th id="th-prem">Prem</th><th title="Orden de refresco del proveedor premium (1 = el próximo)">Ord</th><th></th></tr>
      </thead>
      <tbody></tbody>
    </table>
  </div>
</section>

<section class="card" data-tab="weather">
  <div class="card-head"><div><h2>Proveedor premium</h2><p>Open-Meteo siempre da la hora local y el día/noche. Con Tomorrow.io o WeatherAPI, la temperatura y el tiempo salen de ahí; si una ciudad lleva más de 1 h sin éxito, vuelve a Open-Meteo.</p></div></div>
  <label class="field" style="max-width:340px">
    <span>Proveedor activo</span>
    <select id="prov-active">
      <option value="none">Ninguno (solo Open-Meteo)</option>
      <option value="tomorrow">Tomorrow.io</option>
      <option value="weatherapi">WeatherAPI</option>
    </select>
  </label>
  <div class="fields" style="margin-top:14px">
    <label class="field"><span>API key Tomorrow.io</span><input id="tio-key" class="mono" type="text" placeholder="pega aquí la clave" autocomplete="off" spellcheck="false"/><small id="tio-key-info">—</small></label>
    <label class="field"><span>Refresco Tomorrow.io (s) <span class="text-warn">· gratis: 25/día</span></span><input id="tio-refresh" type="number" min="60" max="86400" step="60"/></label>
  </div>
  <div class="fields" style="margin-top:14px">
    <label class="field"><span>API key WeatherAPI</span><input id="wap-key" class="mono" type="text" placeholder="pega aquí la clave" autocomplete="off" spellcheck="false"/><small id="wap-key-info">—</small></label>
    <label class="field"><span>Refresco WeatherAPI (s) <span class="text-warn">· gratis: 1M/mes</span></span><input id="wap-refresh" type="number" min="60" max="86400" step="60"/></label>
  </div>
  <div class="btn-row" style="margin-top:16px"><button id="prov-save" class="btn btn-primary">Guardar proveedor</button></div>
</section>

<div id="wx-modal" class="modal hidden">
  <div class="modal-backdrop"></div>
  <div class="modal-card" role="dialog" aria-modal="true" aria-labelledby="wx-modal-title">
    <div class="modal-head">
      <h3 id="wx-modal-title">Debug meteo</h3>
      <button id="wx-modal-close" class="btn btn-sm btn-ghost" aria-label="Cerrar">✕</button>
    </div>
    <div class="modal-body">
      <div class="modal-meta" id="wx-modal-meta"></div>
      <div class="modal-section"><span class="label">URL</span><pre id="wx-modal-url"></pre></div>
      <div class="modal-section"><span class="label">Respuesta</span><pre id="wx-modal-body"></pre></div>
    </div>
  </div>
</div>

<!-- ═════════════════════════ Iconos ═════════════════════════ -->
<header class="page-head" data-tab="icons">
  <h1>Iconos</h1>
  <p>Los iconos del tiempo: 5×5 píxeles y tantos fotogramas como quieras.</p>
</header>
<section class="card" data-tab="icons" data-save>
  <div class="card-head">
    <div class="fields" style="flex:1;max-width:420px">
      <label class="field"><span>Icono</span><select id="icon-pick" data-nodirty></select></label>
      <label class="field"><span>Duración del fotograma (ms)</span><input id="frame-ms" type="number" min="50" max="5000" step="50"/></label>
    </div>
    <div class="actions"><button id="reset-icon" class="btn btn-sm btn-danger">Restablecer</button></div>
  </div>
  <div class="frames" style="margin-bottom:18px">
    <div id="icon-frames" class="frames"></div>
    <button id="frame-add" class="btn btn-sm">+ Fotograma</button>
    <button id="frame-play" class="btn btn-sm">▶ Play</button>
    <button id="frame-play-device" class="btn btn-sm" title="Reproduce este icono en la primera fila del panel">Ver en el panel</button>
  </div>
  <div class="icon-edit">
    <div id="icon-grid"></div>
    <div style="min-width:0">
      <span class="label">Paleta · elige un color y pinta</span>
      <div id="palette" style="margin-top:10px"></div>
    </div>
  </div>
</section>

<!-- ═════════════════════════ Jitter ═════════════════════════ -->
<header class="page-head" data-tab="jitter">
  <h1>Jitter</h1>
  <p>El panel se anuncia por Bluetooth como un ratón y mueve el cursor unos píxeles para que tu Mac no entre en reposo. También se controla desde la app de la barra de menús (carpeta <code>mac/</code>).</p>
</header>
<section class="card" data-tab="jitter">
  <div class="card-head">
    <div><h2>Ratón Bluetooth</h2><p>Emparéjalo desde Ajustes → Bluetooth en macOS. El estado se recuerda tras reiniciar.</p></div>
    <span id="jitter-ble-status" class="pill pill-mute">—</span>
  </div>
  <div class="settings">
    <label class="setting"><div class="t"><b>Jitter activo</b><span>Mueve el cursor cada intervalo</span></div><span class="toggle"><input type="checkbox" id="jitter-en"><span class="toggle-slider"></span></span></label>
  </div>
  <div class="fields" style="margin-top:14px">
    <label class="field"><span>Intervalo</span>
      <select id="jitter-interval">
        <option value="250">250 ms</option><option value="500">500 ms</option><option value="1000">1 s</option><option value="2000">2 s</option>
        <option value="5000">5 s</option><option value="10000">10 s</option><option value="30000">30 s</option><option value="60000">60 s</option>
      </select>
    </label>
    <label class="field"><span>Distancia máxima</span>
      <select id="jitter-step">
        <option value="2">2 px</option><option value="3">3 px</option><option value="4">4 px</option><option value="6">6 px</option><option value="10">10 px</option>
      </select>
    </label>
    <label class="field"><span>Nombre Bluetooth</span><input type="text" id="jitter-name" maxlength="29" placeholder="Pixelario Jitter"/><small>Se aplica al reiniciar. macOS puede mostrar el nombre antiguo hasta que lo olvides y lo vuelvas a emparejar.</small></label>
  </div>
  <div class="btn-row" style="margin-top:16px"><button id="jitter-apply" class="btn btn-primary">Aplicar</button></div>
</section>

<!-- ═════════════════════════ Sistema ═════════════════════════ -->
<header class="page-head" data-tab="system">
  <h1>Sistema</h1>
  <p>Red, botones físicos, actualizaciones y copias de seguridad.</p>
</header>
<section class="card" data-tab="system">
  <div class="card-head">
    <div><h2>WiFi</h2></div>
    <div class="actions"><button id="scan" class="btn btn-sm">Buscar redes</button></div>
  </div>
  <div id="wifi-status" class="wifi-status">—</div>
  <div id="nets"></div>
  <div class="fields">
    <label class="field"><span>Red (SSID)</span><input id="ssid" autocomplete="off"/></label>
    <label class="field"><span>Contraseña</span><input id="pwd" type="password"/></label>
  </div>
  <div class="btn-row" style="margin-top:14px"><button id="connect" class="btn btn-primary">Conectar y reiniciar</button></div>
  <div data-save style="margin-top:18px;padding-top:16px;border-top:1px solid var(--line)">
    <div class="settings">
      <label class="setting"><div class="t"><b>DHCP</b><span>Recomendado. Desactívalo para fijar la IP.</span></div><span class="toggle"><input id="wifi-dhcp" type="checkbox" checked/><span class="toggle-slider"></span></span></label>
    </div>
    <div id="wifi-static-fields" style="display:none;margin-top:12px">
      <div class="fields">
        <label class="field"><span>IP</span><input id="wifi-ip" class="mono" placeholder="192.168.1.50"/></label>
        <label class="field"><span>Puerta de enlace</span><input id="wifi-gw" class="mono" placeholder="192.168.1.1"/></label>
        <label class="field"><span>Máscara</span><input id="wifi-sn" class="mono" placeholder="255.255.255.0"/></label>
        <label class="field"><span>DNS 1</span><input id="wifi-dns1" class="mono" placeholder="1.1.1.1"/></label>
        <label class="field"><span>DNS 2 (opcional)</span><input id="wifi-dns2" class="mono"/></label>
      </div>
      <p class="note">Se aplica en el siguiente arranque. Si algún valor no es válido, vuelve a DHCP solo.</p>
    </div>
  </div>
</section>

<section class="card" data-tab="system" data-save>
  <div class="card-head"><div><h2>Botones táctiles</h2><p>PULLUP para un pulsador a GND, INPUT para sensores TTP223. Si un lado se dispara solo, desactívalo: el mando de Resumen sigue funcionando. Evita A1: es un pin de arranque del ESP32-S3 y puede impedir que encienda.</p></div></div>
  <div id="ttp-config" class="list"></div>
</section>

<section class="card" data-tab="system">
  <div class="card-head"><div><h2>Actualizaciones</h2><p>El panel busca la última versión publicada en GitHub y se actualiza solo, con un aviso en pantalla.</p></div></div>
  <div data-save>
    <div class="settings">
      <label class="setting"><div class="t"><b>Actualizar automáticamente</b><span>Al arrancar y cada cierto tiempo</span></div><span class="toggle"><input id="autoupd-en" type="checkbox"/><span class="toggle-slider"></span></span></label>
    </div>
    <div class="fields" style="margin-top:12px">
      <label class="field"><span>Comprobar cada (horas)</span><input id="autoupd-interval" type="number" min="1" max="720" step="1" value="24"/></label>
    </div>
  </div>
  <div class="btn-row" style="margin-top:14px">
    <button id="autoupd-now" type="button" class="btn">Buscar ahora</button>
    <span id="autoupd-status" class="text-muted" style="font-size:13px"></span>
  </div>
  <div style="margin-top:18px;padding-top:16px;border-top:1px solid var(--line)">
    <span class="label">Subir firmware a mano</span>
    <div class="btn-row" style="margin-top:8px">
      <input id="ota-file" type="file" accept=".bin"/>
      <button id="ota-upload" class="btn btn-primary">Subir y reiniciar</button>
    </div>
    <div id="ota-progress" class="hidden"><div id="ota-bar"></div></div>
    <p class="note">El <code>.bin</code> está en <code>.pio/build/matrixportal_s3/firmware.bin</code>. Tarda alrededor de un minuto.</p>
  </div>
</section>

<div class="split" data-tab="system">
  <section class="card">
    <div class="card-head"><div><h2>Copia de seguridad</h2><p>Ciudades, brillo, modo noche, paleta, iconos y el resto de ajustes. No incluye la contraseña del WiFi.</p></div></div>
    <div class="btn-row">
      <button id="cfg-export" class="btn">Descargar</button>
      <button id="cfg-import-btn" class="btn">Restaurar…</button>
      <input id="cfg-import" type="file" accept="application/json,.json" class="hidden"/>
    </div>
  </section>
  <section class="card">
    <div class="card-head"><div><h2>Reiniciar</h2><p>La página se recarga sola cuando el panel vuelve.</p></div></div>
    <button id="reset-dev" class="btn btn-danger">Reiniciar el panel</button>
  </section>
</div>

</div>
</main>
</div>

<div class="savebar" id="savebar" role="region" aria-label="Cambios sin guardar">
  <i class="dot"></i><span>Cambios sin guardar</span>
  <button id="reload" class="btn btn-sm btn-ghost">Descartar</button>
  <button id="save" class="btn btn-sm btn-primary">Guardar</button>
</div>
<div id="msg" role="status" aria-live="polite"></div>
<script>
const $ = s => document.querySelector(s);

// ── Secciones: se muestran los elementos con data-tab de la elegida. La
// elegida persiste en localStorage; un valor de versiones anteriores que ya
// no existe cae en Resumen.
const TABS = ['home','display','modes','claude','life','fire','plasma','moire','nyan','image','weather','icons','jitter','system'];
// Subapartados: la pestaña padre se marca y, en movil, se despliegan sus hijos.
const TAB_PARENT = {claude:'modes', life:'modes', fire:'modes', plasma:'modes', moire:'modes', nyan:'modes', image:'modes', icons:'weather'};
// Indice = valor del modo en el firmware (startup_mode, programaciones).
const MODE_NAMES = ['4 filas', 'Focus', 'Claude', 'Game of Life', 'Imagen', 'Llama', 'Plasma', 'Moiré', 'Nyan Cat'];
const modeOptions = sel => MODE_NAMES.map((n, i) => `<option value="${i}" ${i == sel ? 'selected' : ''}>${i + 1} · ${n}</option>`).join('');
// Efectos con paleta propia: id de los controles = prefijo de las claves de config.
const DEMO_PALS = ['fire', 'plasma', 'moire'];
function activateTab(t) {
  if (!TABS.includes(t)) t = 'home';
  document.querySelectorAll('.content [data-tab]').forEach(el => {
    el.hidden = el.dataset.tab !== t;
  });
  const group = TAB_PARENT[t] || t;
  $('.nav').dataset.group = group;
  document.querySelectorAll('.nav button[data-tab-btn]').forEach(b => {
    const on = b.dataset.tabBtn === t;
    b.classList.toggle('active', on);
    b.classList.toggle('open', !on && b.dataset.tabBtn === group);
    if (on) b.setAttribute('aria-current', 'page'); else b.removeAttribute('aria-current');
    if (on && b.scrollIntoView && window.innerWidth < 900) b.scrollIntoView({block:'nearest', inline:'center'});
  });
  try { localStorage.setItem('activeTab', t); } catch (e) {}
  if (location.hash !== '#' + t) history.replaceState(null, '', '#' + t);
  document.dispatchEvent(new CustomEvent('tabchange', {detail: t}));
}
document.querySelectorAll('.nav button[data-tab-btn]').forEach(b => {
  b.onclick = () => { activateTab(b.dataset.tabBtn); window.scrollTo({top: 0}); };
});
{
  let initial = location.hash.slice(1);
  if (!TABS.includes(initial)) { try { initial = localStorage.getItem('activeTab') || 'home'; } catch (e) { initial = 'home'; } }
  activateTab(initial);
}

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
let jitterTimer = null;
let holaTimer = null;
function startPolls() {
  if (!statusTimer) statusTimer = setInterval(loadStatus, 5000);
  if (!weatherTimer) weatherTimer = setInterval(loadWeather, 10000);
  if (!jitterTimer) jitterTimer = setInterval(loadJitter, 4000);
  if (!holaTimer) holaTimer = setInterval(loadHola, 5000);
}
function stopPolls() {
  if (statusTimer) { clearInterval(statusTimer); statusTimer = null; }
  if (weatherTimer) { clearInterval(weatherTimer); weatherTimer = null; }
  if (jitterTimer) { clearInterval(jitterTimer); jitterTimer = null; }
  if (holaTimer) { clearInterval(holaTimer); holaTimer = null; }
  abortPolls();
}

// Aviso flotante unico: cada llamada lo reemplaza (el OTA lo llama en cada
// % de progreso). Sin tipo se queda fijo; con tipo se oculta solo.
let msgTimer = null;
function setMsg(t, kind){
  const m = $('#msg');
  clearTimeout(msgTimer);
  m.textContent = t || '';
  m.className = t ? 'show' : '';
  if (kind === 'ok') m.classList.add('msg-ok');
  else if (kind === 'err') m.classList.add('msg-err');
  else if (kind === 'warn') m.classList.add('msg-warn');
  if (t && kind) msgTimer = setTimeout(() => m.classList.remove('show'), kind === 'err' ? 7000 : 3500);
}
function fmtUp(s){ s=Math.floor(s); if(s<60)return s+'s'; if(s<3600)return Math.floor(s/60)+'m '+(s%60)+'s'; return Math.floor(s/3600)+'h '+Math.floor((s%3600)/60)+'m'; }
function intToHex(n){ return '#'+(n|0).toString(16).padStart(6,'0'); }
function hexToInt(h){ return parseInt(h.replace('#',''),16); }
function minsToHHMM(m){ const h=Math.floor(m/60),mm=m%60; return String(h).padStart(2,'0')+':'+String(mm).padStart(2,'0'); }
function hhmmToMins(s){ const [h,m]=s.split(':').map(Number); return h*60+m; }
function rssiClass(r){ return r >= -60 ? 'rssi-good' : r >= -75 ? 'rssi-mid' : 'rssi-bad'; }

function rssiLevel(r){ return !r ? 0 : r >= -60 ? 4 : r >= -70 ? 3 : r >= -80 ? 2 : 1; }
const MOON_NAMES = {NEW:'Luna nueva', WAXING:'Creciente', FULL:'Luna llena', WANING:'Menguante'};
let lastStatus = null;
async function loadStatus(){
  try{
    const r = await fetch('/api/status', {signal: pollSignal()}); const d = await r.json();
    lastStatus = d;
    const bars = `<span class="bars" data-l="${rssiLevel(d.rssi)}"><i></i><i></i><i></i><i></i></span>`;
    $('#sb-ip').textContent = d.ip || '—';
    $('#sb-rssi').innerHTML = d.rssi ? `${bars} ${d.rssi} dBm` : '—';
    $('#sb-up').textContent = fmtUp(d.uptime_sec);
    $('#sb-heap').textContent = (d.heap_free/1024).toFixed(0) + ' KB';
    $('#brand-fw').textContent = d.fw_version || '—';
    $('#ov-ip').textContent = d.ip || '—';
    $('#ov-up').textContent = fmtUp(d.uptime_sec);
    $('#ov-heap').textContent = `${(d.heap_free/1024).toFixed(0)} KB de memoria libre`;
    $('#ov-fw').textContent = d.fw_version || '—';
    if (d.rssi) $('#ov-conn-sub').innerHTML = `${bars} ${d.rssi} dBm · ${['','débil','justa','buena','excelente'][rssiLevel(d.rssi)]}`;
    if (d.moon) {
      $('#ov-moon').textContent = MOON_NAMES[d.moon.phase] || d.moon.phase || '—';
      $('#ov-moon-sub').textContent = `${Math.round(d.moon.age_days)} días de ${Math.round(d.moon.synodic_days * 10) / 10}`;
    }
  }catch(e){}
}
async function loadWifi(){
  try{
    const r = await fetch('/api/wifi'); const d = await r.json();
    let html = '';
    if (d.mode === 'sta') html = `<span class="pill pill-ok"><span class="pill-dot"></span>Conectado</span><span>a <b>${d.current_ssid}</b> · <code>${d.ip}</code></span>`;
    else if (d.mode === 'ap') html = `<span class="pill pill-warn"><span class="pill-dot"></span>Punto de acceso</span><span>red <b>${d.ap_ssid}</b> · <code>${d.ip}</code></span>`;
    else html = `<span class="pill pill-err">Sin conexión</span>`;
    $('#wifi-status').innerHTML = html;
    $('#brand-host').textContent = d.hostname ? d.hostname + '.local' : (d.ip || '');
    $('#ov-host').textContent = d.hostname ? d.hostname + '.local' : '';
    $('#ov-conn').textContent = d.mode === 'sta' ? (d.current_ssid || 'WiFi') : d.mode === 'ap' ? 'Punto de acceso' : 'Sin conexión';
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
    $('#hola-en').checked = !!cfg.claude_auto_hola_enabled;
    {
      const hh = String(cfg.claude_auto_hola_hour || 0).padStart(2, '0');
      const mm = String(cfg.claude_auto_hola_minute || 0).padStart(2, '0');
      $('#hola-time').value = hh + ':' + mm;
    }
    $('#keepawake-en').checked = !!cfg.claude_keep_awake_enabled;
    {
      const off = new Set(Array.isArray(cfg.claude_anims_off) ? cfg.claude_anims_off : []);
      document.querySelectorAll('[data-anim]').forEach(cb => { cb.checked = !off.has(cb.dataset.anim); });
    }
    $('#autoupd-en').checked = cfg.auto_update_enabled !== false;
    $('#autoupd-interval').value = cfg.auto_update_check_interval_h || 24;
    $('#startup-mode').innerHTML = modeOptions(cfg.startup_mode != null ? cfg.startup_mode : 0);
    $('#life-color').value   = intToHex(cfg.life_color != null ? cfg.life_color : 0x80C0FF);
    $('#life-rainbow').checked = !!cfg.life_rainbow;
    $('#life-color-row').style.display = cfg.life_rainbow ? 'none' : '';
    DEMO_PALS.forEach(id => {
      const def = cfg[id + '_use_default'] !== false;
      $('#' + id + '-default').checked = def;
      $('#' + id + '-color').value = intToHex(cfg[id + '_color'] != null ? cfg[id + '_color'] : 0xFF6000);
      $('#' + id + '-color-row').style.display = def ? 'none' : '';
    });
    const lifeStep = cfg.life_step_ms || 150;
    $('#life-step').value = lifeStep;
    $('#life-step-val').textContent = lifeStep;
    // Programaciones (lista de hasta 10). Renderizamos siempre 10 filas:
    // las activas con sus valores, las vacias con defaults.
    const sched = Array.isArray(cfg.schedule) ? cfg.schedule : [];
    const SCHED_MAX = 10;
    const schedList = $('#schedule-list');
    schedList.innerHTML = '';
    for (let i = 0; i < SCHED_MAX; i++) {
      const s = sched[i] || {enabled:false, hour:0, minute:0, mode:0};
      const hhmm = String(s.hour).padStart(2,'0') + ':' + String(s.minute).padStart(2,'0');
      const row = document.createElement('div');
      row.className = 'list-row';
      row.innerHTML = `
        <span class="idx">${i+1}</span>
        <label class="toggle"><input type="checkbox" data-sched="${i}" data-k="enabled" ${s.enabled?'checked':''} aria-label="Activar programación ${i+1}"/><span class="toggle-slider"></span></label>
        <input type="time" data-sched="${i}" data-k="time" value="${hhmm}"/>
        <select data-sched="${i}" data-k="mode">${modeOptions(s.mode)}</select>
      `;
      schedList.appendChild(row);
    }
    // Configuracion de los 3 botones tactiles (enabled + pin + pullup).
    const ttpEn  = Array.isArray(cfg.ttp_enabled) ? cfg.ttp_enabled : [true,true,true];
    const ttpPin = Array.isArray(cfg.ttp_pin)     ? cfg.ttp_pin     : [9,10,11];
    // Modo nuevo (0=INPUT, 1=PULLUP, 2=PULLDOWN). Migra del legacy bool si falta.
    const ttpPm = Array.isArray(cfg.ttp_pinmode) ? cfg.ttp_pinmode
                : Array.isArray(cfg.ttp_pullup)  ? cfg.ttp_pullup.map(b => b ? 1 : 0)
                : [1,1,1];
    const ttpLabels = ['Izquierda', 'Centro', 'Derecha'];
    const pinOpts = [
      {v: 3,  t: 'A1 (GPIO 3) · pin de arranque'},
      {v: 9,  t: 'A2 (GPIO 9)'},
      {v: 10, t: 'A3 (GPIO 10)'},
      {v: 11, t: 'A4 (GPIO 11)'},
    ];
    const wrap = $('#ttp-config');
    wrap.innerHTML = '';
    for (let i = 0; i < 3; i++) {
      const en  = ttpEn[i]  !== false;
      const pin = ttpPin[i] || [9,10,11][i];
      const pm  = (typeof ttpPm[i] === 'number') ? ttpPm[i] : 1;
      const row = document.createElement('div');
      row.className = 'list-row';
      row.innerHTML = `
        <label class="toggle"><input type="checkbox" data-ttp="${i}" data-k="en" ${en?'checked':''} aria-label="Activar botón ${ttpLabels[i]}"/><span class="toggle-slider"></span></label>
        <span class="name">${ttpLabels[i]}</span>
        <select data-ttp="${i}" data-k="pin">
          ${pinOpts.map(o => `<option value="${o.v}" ${pin==o.v?'selected':''}>${o.t}</option>`).join('')}
        </select>
        <select data-ttp="${i}" data-k="pm">
          <option value="1" ${pm==1?'selected':''}>PULLUP (pulsador a GND)</option>
          <option value="2" ${pm==2?'selected':''}>PULLDOWN (pulsador a 3V3)</option>
          <option value="0" ${pm==0?'selected':''}>INPUT (TTP223 push-pull)</option>
        </select>
      `;
      wrap.appendChild(row);
    }
    // WiFi DHCP / static
    const useDhcp = cfg.wifi_use_dhcp !== false;
    $('#wifi-dhcp').checked = useDhcp;
    $('#wifi-static-fields').style.display = useDhcp ? 'none' : '';
    $('#wifi-ip').value   = cfg.wifi_static_ip      || '';
    $('#wifi-gw').value   = cfg.wifi_static_gateway || '';
    $('#wifi-sn').value   = cfg.wifi_static_subnet  || '';
    $('#wifi-dns1').value = cfg.wifi_static_dns1    || '';
    $('#wifi-dns2').value = cfg.wifi_static_dns2    || '';
    $('#trend-extras').style.display = $('#trend-en').checked ? '' : 'none';
    $('#refresh').value = cfg.weather_refresh_sec;
    $('#rgb-order').value = cfg.rgb_order || 'RGB';
    initialRgbOrder = $('#rgb-order').value;
    syncRanges();
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
      <input type="color" data-i="${i}" data-k="color" value="${intToHex(c.color)}" aria-label="Color fila ${i+1}"/>
      <input data-i="${i}" data-k="name" value="${c.name||''}" maxlength="6" class="mono" aria-label="Nombre fila ${i+1}"/>
      <input data-i="${i}" data-k="lat" type="number" step="0.000001" value="${c.lat}" placeholder="lat" aria-label="Latitud fila ${i+1}"/>
      <input data-i="${i}" data-k="lon" type="number" step="0.000001" value="${c.lon}" placeholder="lon" aria-label="Longitud fila ${i+1}"/>`;
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
        markDirty();
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
  markDirty();
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
    if (i === 0) b.className = 'transp'; else b.style.background = hex;
    if (i === curColor) b.classList.add('sel');
    b.title = i === 0 ? 'Transparente' : 'Color '+i;
    b.onclick = () => { curColor = i; renderPalette(); };
    w.appendChild(b);
    if (i > 0) {
      const ed = document.createElement('input');
      ed.type = 'color'; ed.value = hex;
      ed.oninput = e => {
        cfg.palette[i] = hexToInt(e.target.value);
        b.style.background = e.target.value;
        markDirty();
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
    if (v === 0) c.className = 'transp'; else c.style.background = pal[v];
    c.setAttribute('aria-label', `Píxel ${x+1},${y+1}`);
    c.onclick = () => { curFrameObj().px[y][x] = curColor; renderIconGrid(); markDirty(); };
    g.appendChild(c);
  }
}

function stopPlay(){
  if (playTimer) clearTimeout(playTimer);
  playTimer = null;
  $('#frame-play').textContent = '▶ Play';
}
function startPlay(){
  const fr = curFrames();
  if (!fr || fr.length < 2) { stopPlay(); return; }
  $('#frame-play').textContent = '❚❚ Pausa';
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
    $('#frame-play-device').textContent = '■ Parar en el panel';
    setMsg(`Preview en device (${d.frames} frames, ${d.duration_ms/1000}s max)`, 'ok');
  }catch(e){ setMsg('Error: '+e.message, 'err'); }
}
async function stopDevicePlay(){
  try{
    await fetch('/api/icons/preview/stop', {method:'POST'});
  }catch(e){}
  devicePlaying = false;
  $('#frame-play-device').textContent = 'Ver en el panel';
  setMsg('Preview detenido', 'ok');
}
$('#frame-play-device').addEventListener('click', () => {
  devicePlaying ? stopDevicePlay() : startDevicePlay();
});

async function loadWeather(){
  try{
    const r = await fetch('/api/weather', {signal: pollSignal()}); const d = await r.json();
    if (d.cities && d.cities[0] && d.utc_now) ledBase = {utc: d.utc_now, at: Date.now(), off: d.cities[0].offset_sec || 0, ok: !!d.cities[0].has_data};
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
    claude_auto_hola_enabled: $('#hola-en').checked,
    claude_auto_hola_hour:    parseInt(($('#hola-time').value || '09:00').split(':')[0], 10) || 0,
    claude_auto_hola_minute:  parseInt(($('#hola-time').value || '09:00').split(':')[1], 10) || 0,
    claude_keep_awake_enabled: $('#keepawake-en').checked,
    auto_update_enabled:    $('#autoupd-en').checked,
    auto_update_check_interval_h: Math.max(1, Math.min(720, parseInt($('#autoupd-interval').value, 10) || 24)),
    ttp_enabled: (function() {
      return [0,1,2].map(i => document.querySelector(`[data-ttp="${i}"][data-k="en"]`).checked);
    })(),
    ttp_pin: (function() {
      return [0,1,2].map(i => parseInt(document.querySelector(`[data-ttp="${i}"][data-k="pin"]`).value, 10));
    })(),
    ttp_pinmode: (function() {
      return [0,1,2].map(i => parseInt(document.querySelector(`[data-ttp="${i}"][data-k="pm"]`).value, 10));
    })(),
    startup_mode: parseInt($('#startup-mode').value, 10) || 0,
    life_color:   hexToInt($('#life-color').value),
    life_rainbow: $('#life-rainbow').checked,
    life_step_ms: parseInt($('#life-step').value, 10) || 150,
    schedule: (function() {
      const rows = document.querySelectorAll('#schedule-list > div');
      const out = [];
      rows.forEach((row, i) => {
        const en = row.querySelector('[data-k="enabled"]').checked;
        const t  = row.querySelector('[data-k="time"]').value || '00:00';
        const md = parseInt(row.querySelector('[data-k="mode"]').value, 10) || 0;
        const [hh, mm] = t.split(':').map(x => parseInt(x, 10) || 0);
        out.push({enabled: en, hour: hh, minute: mm, mode: md});
      });
      return out;
    })(),
    wifi_use_dhcp:          $('#wifi-dhcp').checked,
    wifi_static_ip:         $('#wifi-ip').value.trim(),
    wifi_static_gateway:    $('#wifi-gw').value.trim(),
    wifi_static_subnet:     $('#wifi-sn').value.trim(),
    wifi_static_dns1:       $('#wifi-dns1').value.trim(),
    wifi_static_dns2:       $('#wifi-dns2').value.trim(),
    cities: cfg.cities,
    night_mode: {
      enabled: $('#nm-en').checked,
      start_mins: hhmmToMins($('#nm-start').value),
      end_mins: hhmmToMins($('#nm-end').value),
      brightness: $('#nm-bright').value/100,
    },
    palette: cfg.palette,
    icons: cfg.icons,
    ...Object.fromEntries(DEMO_PALS.flatMap(id => [
      [id + '_use_default', $('#' + id + '-default').checked],
      [id + '_color', hexToInt($('#' + id + '-color').value)],
    ])),
    claude_anims_off: [...document.querySelectorAll('[data-anim]')].filter(cb => !cb.checked).map(cb => cb.dataset.anim),
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
    clearDirty();
    setMsg('Guardado' + (d.cities_changed ? ' · actualizando el tiempo' : ''), 'ok');
    loadWeather();
  }catch(e){ setMsg('Error: '+e.message, 'err'); }
};

// Toggle DHCP/static
$('#wifi-dhcp').onchange = () => {
  $('#wifi-static-fields').style.display = $('#wifi-dhcp').checked ? 'none' : '';
};
// Modo imagen: editor con crop ajustable + upload a 64x23.
// State: srcImg, srcDrawW/H/X/Y (la imagen escalada para caber en el src
// canvas), y cropX/Y/W/H en COORDENADAS DE LA IMAGEN ORIGINAL.
let userImgBuffer = null;
let srcImg = null;
let srcDrawX = 0, srcDrawY = 0, srcDrawW = 0, srcDrawH = 0;
let cropX = 0, cropY = 0, cropW = 0, cropH = 0;
const TGT_W = 64, TGT_H = 23;
const TGT_AR = TGT_W / TGT_H;

function maxCropDims() {
  // Maximo rect 64:23 que cabe dentro de la imagen original.
  const imgAR = srcImg.width / srcImg.height;
  let mw, mh;
  if (imgAR > TGT_AR) { mh = srcImg.height; mw = mh * TGT_AR; }
  else                { mw = srcImg.width;  mh = mw / TGT_AR; }
  return {mw, mh};
}

function clampCrop() {
  if (cropW > srcImg.width)  cropW = srcImg.width;
  if (cropH > srcImg.height) cropH = srcImg.height;
  if (cropX < 0) cropX = 0;
  if (cropY < 0) cropY = 0;
  if (cropX + cropW > srcImg.width)  cropX = srcImg.width  - cropW;
  if (cropY + cropH > srcImg.height) cropY = srcImg.height - cropH;
}

function applyZoomPercent(p) {
  // p=100 → crop ocupa el max posible (= cover). p=20 → crop pequeño = zoom.
  const {mw, mh} = maxCropDims();
  const centerX = cropX + cropW / 2;
  const centerY = cropY + cropH / 2;
  cropW = mw * (p / 100);
  cropH = mh * (p / 100);
  cropX = centerX - cropW / 2;
  cropY = centerY - cropH / 2;
  clampCrop();
}

function redrawUserImg() {
  if (!srcImg) return;
  const sc = $('#userimg-src');
  const sctx = sc.getContext('2d');
  sctx.fillStyle = '#222';
  sctx.fillRect(0, 0, sc.width, sc.height);
  sctx.imageSmoothingEnabled = true;
  sctx.drawImage(srcImg, srcDrawX, srcDrawY, srcDrawW, srcDrawH);
  // Rectangulo de crop en coords del canvas
  const r = srcDrawW / srcImg.width;
  const dx = srcDrawX + cropX * r;
  const dy = srcDrawY + cropY * r;
  const dw = cropW * r;
  const dh = cropH * r;
  // Oscurecer fuera del crop
  sctx.fillStyle = 'rgba(0,0,0,0.55)';
  sctx.fillRect(0, 0, sc.width, dy);                                    // top
  sctx.fillRect(0, dy + dh, sc.width, sc.height - (dy + dh));           // bottom
  sctx.fillRect(0, dy, dx, dh);                                          // left
  sctx.fillRect(dx + dw, dy, sc.width - (dx + dw), dh);                  // right
  sctx.strokeStyle = '#fb923c';
  sctx.lineWidth = 2;
  sctx.strokeRect(dx, dy, dw, dh);

  // Renderizar a 64x23 a partir del crop y construir el buffer.
  const off = document.createElement('canvas');
  off.width = TGT_W; off.height = TGT_H;
  const octx = off.getContext('2d');
  octx.imageSmoothingEnabled = true;
  octx.drawImage(srcImg, cropX, cropY, cropW, cropH, 0, 0, TGT_W, TGT_H);
  const px = octx.getImageData(0, 0, TGT_W, TGT_H).data;
  const buf = new Uint8Array(TGT_W * TGT_H * 2);
  for (let i = 0, j = 0; i < px.length; i += 4) {
    const v = ((px[i] & 0xF8) << 8) | ((px[i+1] & 0xFC) << 3) | (px[i+2] >> 3);
    buf[j++] = v & 0xFF;
    buf[j++] = (v >> 8) & 0xFF;
  }
  userImgBuffer = buf;

  // Preview x4
  const pv = $('#userimg-preview');
  const pctx = pv.getContext('2d');
  pctx.imageSmoothingEnabled = false;
  pctx.fillStyle = '#000';
  pctx.fillRect(0, 0, pv.width, pv.height);
  pctx.drawImage(off, 0, 0, pv.width, pv.height);
}

function loadUserImage(img) {
  srcImg = img;
  const sc = $('#userimg-src');
  const r = Math.min(sc.width / img.width, sc.height / img.height);
  srcDrawW = img.width  * r;
  srcDrawH = img.height * r;
  srcDrawX = (sc.width  - srcDrawW) / 2;
  srcDrawY = (sc.height - srcDrawH) / 2;
  const {mw, mh} = maxCropDims();
  cropW = mw; cropH = mh;
  cropX = (img.width  - cropW) / 2;
  cropY = (img.height - cropH) / 2;
  $('#userimg-zoom').value = 100; syncRange($('#userimg-zoom'));
  $('#userimg-zoom-val').textContent = '100%';
  redrawUserImg();
}

$('#userimg-file').onchange = e => {
  const f = e.target.files && e.target.files[0];
  if (!f) return;
  const img = new Image();
  img.onload = () => { loadUserImage(img); URL.revokeObjectURL(img.src); };
  img.src = URL.createObjectURL(f);
};

// Drag para mover el rectangulo de crop dentro de la imagen.
(() => {
  const sc = $('#userimg-src');
  let dragging = false, lastX = 0, lastY = 0;
  sc.addEventListener('pointerdown', e => {
    if (!srcImg) return;
    dragging = true; lastX = e.offsetX; lastY = e.offsetY;
    sc.setPointerCapture(e.pointerId);
  });
  sc.addEventListener('pointermove', e => {
    if (!dragging) return;
    const dx = e.offsetX - lastX;
    const dy = e.offsetY - lastY;
    lastX = e.offsetX; lastY = e.offsetY;
    const r = srcImg.width / srcDrawW;     // canvas-px → image-px
    cropX += dx * r;
    cropY += dy * r;
    clampCrop();
    redrawUserImg();
  });
  const end = () => { dragging = false; };
  sc.addEventListener('pointerup', end);
  sc.addEventListener('pointercancel', end);
  sc.addEventListener('pointerleave', end);
})();

$('#userimg-zoom').oninput = e => {
  if (!srcImg) return;
  const p = parseInt(e.target.value, 10) || 100;
  $('#userimg-zoom-val').textContent = p + '%';
  applyZoomPercent(p);
  redrawUserImg();
};

$('#userimg-upload').onclick = async () => {
  const s = $('#userimg-status');
  if (!userImgBuffer) { s.textContent = 'selecciona una imagen primero'; return; }
  s.textContent = 'subiendo...';
  try {
    const fd = new FormData();
    fd.append('image', new Blob([userImgBuffer]), 'img.bin');
    const r = await fetch('/api/userimg', {method: 'POST', body: fd});
    const d = await r.json();
    s.textContent = d.ok ? 'subida ok. Modo imagen ya la usa.' : ('error: ' + (d.error || 'desconocido'));
  } catch (e) {
    s.textContent = 'error: ' + e.message;
  }
};

// Toggle rainbow (oculta el color picker cuando esta activo)
$('#life-rainbow').onchange = () => {
  $('#life-color-row').style.display = $('#life-rainbow').checked ? 'none' : '';
};
// Live label del slider de velocidad
$('#life-step').oninput = () => {
  $('#life-step-val').textContent = $('#life-step').value;
};
// Paleta clasica: oculta el selector de color mientras esta activa.
DEMO_PALS.forEach(id => {
  $('#' + id + '-default').onchange = () => {
    $('#' + id + '-color-row').style.display = $('#' + id + '-default').checked ? 'none' : '';
  };
});

// Botones simulados: POST /api/button?b=left|center|right
async function pressButton(side) {
  try { await fetch('/api/button?b=' + side, {method:'POST'}); }
  catch (e) { /* swallow, no es critico */ }
}
$('#btn-sim-left').onclick   = () => pressButton('left');
$('#btn-sim-center').onclick = () => pressButton('center');
$('#btn-sim-right').onclick  = () => pressButton('right');

$('#autoupd-now').onclick = async () => {
  const s = $('#autoupd-status');
  s.textContent = 'pidiendo check...';
  try {
    const r = await fetch('/api/autoupdate/check', {method:'POST'});
    const d = await r.json();
    if (d.ok) {
      s.textContent = 'check disparado. Si hay nueva release, el panel mostrara la descarga; el device reiniciara solo al acabar.';
    } else {
      s.textContent = 'error: ' + (d.error || 'unknown');
    }
  } catch (e) { s.textContent = 'error: ' + e.message; }
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
    a.download = `pixelario_config_${ts}.json`;
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

// Descartar: vuelve a cargar la config del panel y deshace el brillo que se
// haya aplicado en vivo mientras se movia el slider.
$('#reload').onclick = async () => {
  await loadConfig();
  if (cfg) fetch('/api/brightness', {method:'POST', headers:{'Content-Type':'application/json'},
    body: JSON.stringify({brightness: cfg.brightness, night_brightness: cfg.night_mode.brightness})}).catch(()=>{});
  clearDirty();
  setMsg('Cambios descartados', 'ok');
};
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

// --- Jitter (raton BLE) ---------------------------------------------------
// jitterDirty: si el usuario toca un control y aun no ha pulsado "Aplicar", el
// poll de /api/jitter no le pisa la seleccion.
let jitterDirty = false;
let jitterNameLoaded = '';
['jitter-en','jitter-interval','jitter-step','jitter-name'].forEach(id => {
  const el = $('#'+id);
  if (el) el.addEventListener('input', () => { jitterDirty = true; });
  if (el) el.addEventListener('change', () => { jitterDirty = true; });
});
async function loadJitter(){
  try{
    const r = await fetch('/api/jitter', {signal: pollSignal()}); const d = await r.json();
    const s = $('#jitter-ble-status');
    if (s) {
      s.className = 'pill ' + (d.ble_connected ? 'pill-ok' : 'pill-mute');
      s.innerHTML = d.ble_connected ? '<span class="pill-dot"></span>Mac conectado' : 'Esperando emparejamiento';
    }
    if (!jitterDirty){
      $('#jitter-en').checked = !!d.enabled;
      $('#jitter-name').value = d.name || '';
      jitterNameLoaded = d.name || '';
      if ([...$('#jitter-interval').options].some(o => +o.value === d.interval_ms)) $('#jitter-interval').value = d.interval_ms;
      if ([...$('#jitter-step').options].some(o => +o.value === d.max_step)) $('#jitter-step').value = d.max_step;
    }
  }catch(e){}
}
$('#jitter-apply').onclick = async () => {
  const patch = {
    jitter_enabled: $('#jitter-en').checked,
    jitter_interval_ms: parseInt($('#jitter-interval').value, 10) || 1000,
    jitter_max_step: parseInt($('#jitter-step').value, 10) || 4,
    jitter_name: $('#jitter-name').value.trim() || 'Pixelario Jitter',
  };
  const nameChanged = patch.jitter_name !== jitterNameLoaded;
  try{
    const r = await fetch('/api/config', {method:'POST', headers:{'Content-Type':'application/json'},
      body: JSON.stringify(patch)});
    const d = await r.json();
    if (d.error) throw new Error(d.error);
    jitterDirty = false;
    setMsg('Jitter ' + (patch.jitter_enabled ? 'activado' : 'desactivado') + '.', 'ok');
    if (nameChanged && confirm('El nuevo nombre Bluetooth se aplica al reiniciar. ¿Reiniciar ahora?')){
      try{ await fetch('/api/reset', {method:'POST'}); }catch(e){}
      setMsg('Reiniciando…', 'ok');
      return;
    }
    loadJitter();
  }catch(e){ setMsg('Error: '+e.message, 'err'); }
};

// --- Auto "hola" (abrir sesion 5h) ----------------------------------------
async function loadHola(){
  try{
    const r = await fetch('/api/claude/hola', {signal: pollSignal()}); const d = await r.json();
    const s = $('#hola-status');
    if (!s) return;
    renderUsage(d);
    if (!d.configured){ s.innerHTML = '<span class="text-muted">sin sessionKey</span>'; return; }
    const map = {
      none:    '<span class="text-muted">sin envios aun</span>',
      pending: '<span class="msg-warn">enviando…</span>',
      ok:      '<span class="msg-ok">OK — ventana abierta</span>',
      fail:    '<span class="msg-err">error: ' + (d.error || 'desconocido') + '</span>',
    };
    s.innerHTML = map[d.status] || '&mdash;';
  }catch(e){}
}
$('#hola-now').onclick = async () => {
  try{
    const r = await fetch('/api/claude/hola', {method:'POST'});
    const d = await r.json();
    if (d.error) throw new Error(d.error);
    setMsg('Enviando «hola»…', 'ok');
    setTimeout(loadHola, 800);
  }catch(e){ setMsg('Error: '+e.message, 'err'); }
};

// Parte rellenada de los sliders (WebKit no la pinta solo).
function syncRange(r){ const min = +r.min || 0, max = +r.max || 100; r.style.setProperty('--fill', ((r.value - min) / (max - min) * 100) + '%'); }
function syncRanges(){ document.querySelectorAll('input[type=range]').forEach(syncRange); }
document.addEventListener('input', e => { if (e.target.type === 'range') syncRange(e.target); });

// ── Cambios sin guardar ──────────────────────────────────────────────────
// Todo lo que va en el POST de "Guardar" vive dentro de un [data-save]; al
// tocarlo aparece la barra de guardado. Lo que tiene su propio boton
// (WiFi, proveedor, jitter, imagen, OTA) queda fuera.
let dirty = false;
function markDirty(){ dirty = true; $('#savebar').classList.add('show'); }
function clearDirty(){ dirty = false; $('#savebar').classList.remove('show'); }
['input','change'].forEach(ev => document.addEventListener(ev, e => {
  const t = e.target;
  if (!t.closest || !t.closest('[data-save]') || t.closest('[data-nodirty]') || t.type === 'file') return;
  markDirty();
}));
window.addEventListener('beforeunload', e => { if (dirty) { e.preventDefault(); e.returnValue = ''; } });

// ── Tema: automatico (sistema), claro u oscuro ───────────────────────────
function applyTheme(t){
  if (t === 'light' || t === 'dark') document.documentElement.dataset.theme = t;
  else delete document.documentElement.dataset.theme;
  document.querySelectorAll('[data-theme-btn]').forEach(b => b.classList.toggle('on', b.dataset.themeBtn === (t || 'auto')));
  try { if (t === 'light' || t === 'dark') localStorage.setItem('theme', t); else localStorage.removeItem('theme'); } catch (e) {}
}
document.querySelectorAll('[data-theme-btn]').forEach(b => b.onclick = () => applyTheme(b.dataset.themeBtn));
{ let t = 'auto'; try { t = localStorage.getItem('theme') || 'auto'; } catch (e) {} applyTheme(t); }

// ── Reloj LED de la cabecera: hora local de la primera ciudad, como en el
// panel. Se sincroniza con utc_now de /api/weather y corre con el reloj
// del navegador entre medias.
let ledBase = null;
const LED_FONT = {
  '0':['111','101','101','101','111'], '1':['010','110','010','010','111'], '2':['111','001','111','100','111'],
  '3':['111','001','111','001','111'], '4':['101','101','111','001','001'], '5':['111','100','111','001','111'],
  '6':['111','100','111','101','111'], '7':['111','001','001','010','010'], '8':['111','101','111','101','111'],
  '9':['111','101','111','001','111'], '-':['000','000','111','000','000'],
};
function drawLed(){
  const c = $('#led'); if (!c) return;
  const COLS = 26, ROWS = 9, cell = c.width / COLS;
  const ctx = c.getContext('2d');
  ctx.clearRect(0, 0, c.width, c.height);
  const on = new Set();
  let txt = '--:--', colon = true;
  if (ledBase && ledBase.ok) {
    const t = ledBase.utc + Math.floor((Date.now() - ledBase.at) / 1000) + ledBase.off;
    const d = new Date(t * 1000);
    txt = String(d.getUTCHours()).padStart(2, '0') + ':' + String(d.getUTCMinutes()).padStart(2, '0');
    colon = (t % 2) === 0;
  }
  // HH:MM = 3+1+3+1+1+1+3+1+3 = 17 columnas, centrado.
  let x = 4;
  for (const ch of txt) {
    if (ch === ':') { if (colon) { on.add(x + ',3'); on.add(x + ',5'); } x += 2; continue; }
    LED_FONT[ch].forEach((r, yy) => [...r].forEach((b, xx) => { if (b === '1') on.add((x + xx) + ',' + (yy + 2)); }));
    x += 4;
  }
  // Segundero: un punto que recorre la ultima fila de izquierda a derecha.
  const secX = ledBase && ledBase.ok
    ? Math.floor(((ledBase.utc + Math.floor((Date.now() - ledBase.at) / 1000)) % 60) * COLS / 60) : -1;
  const amber = '#ffb23e', off = getComputedStyle(document.documentElement).getPropertyValue('--led-off').trim() || '#16181d';
  for (let yy = 0; yy < ROWS; yy++) for (let xx = 0; xx < COLS; xx++) {
    const sec = yy === ROWS - 1 && xx === secX;
    const lit = sec || on.has(xx + ',' + yy);
    ctx.fillStyle = sec ? '#5aa9f0' : lit ? amber : off;
    ctx.beginPath();
    ctx.arc(xx * cell + cell / 2, yy * cell + cell / 2, cell * (lit ? .4 : .32), 0, Math.PI * 2);
    ctx.fill();
  }
}
setInterval(drawLed, 1000);
drawLed();

// ── Ventana de 5h de Claude ──────────────────────────────────────────────
function renderUsage(d){
  const fh = d && d.five_hour;
  if (!d || !d.configured) {
    $('#cl-use-val').textContent = '—'; $('#cl-use-bar').style.width = '0';
    $('#cl-use-sub').textContent = 'Configura la sessionKey para ver el uso.';
    return;
  }
  if (!fh || !fh.valid) {
    $('#cl-use-val').textContent = '0%'; $('#cl-use-bar').style.width = '0';
    $('#cl-use-sub').textContent = 'Ventana cerrada: se abre con el próximo mensaje.';
    return;
  }
  const pct = Math.max(0, Math.min(100, Math.round(fh.utilization)));
  $('#cl-use-val').textContent = pct + '%';
  $('#cl-use-bar').style.width = pct + '%';
  const left = Math.max(0, (fh.resets_at || 0) - (fh.now || 0));
  const h = Math.floor(left / 3600), m = Math.floor((left % 3600) / 60);
  $('#cl-use-sub').textContent = left > 0 ? `Se renueva en ${h ? h + ' h ' : ''}${m} min.` : 'Renovándose…';
}

// ── Previews de Clawd ────────────────────────────────────────────────────
// Cada tarjeta ejecuta la misma logica que el firmware (copiada del
// simulador por tools/sync-clawd-preview.py) en su propio estado, forzando
// su animacion en bucle. Solo se anima la seccion visible.
function makeClawd(px){
/*CLAWD-LOGIC-BEGIN*/
  // ── Port de la logica del firmware (src/Display.cpp, drawClawd) ──────
  // Zona de Clawd: columna derecha x=43..63, filas 18..31. La
  // barra de segundos se queda en la columna izquierda y Clawd pisa el borde
  // inferior del panel, con 4 filas libres encima de la cabeza.
  const CLAWD_ACTS = [
    { act: 'WAVE', w: 24, min: 3000, max: 6000 },
    { act: 'WALK', w: 24, min: 0, max: 0 },
    { act: 'HOP', w: 14, min: 0, max: 0 },
    { act: 'DANCE', w: 14, min: 0, max: 0 },
    { act: 'SURPRISED', w: 8, min: 900, max: 900 },
    { act: 'NAP', w: 16, min: 5000, max: 9000 },
    { act: 'THINK', w: 12, min: 3000, max: 5000 },
    { act: 'HEART', w: 10, min: 2400, max: 3600 },
    { act: 'SPIN', w: 10, min: 0, max: 0 },
    { act: 'NOD', w: 8, min: 0, max: 0 },
    { act: 'FIREFLY', w: 8, min: 6000, max: 8000 },
    { act: 'JUGGLE', w: 8, min: 4000, max: 6000 },
    { act: 'PEEK', w: 6, min: 0, max: 0 },
    { act: 'CODE', w: 10, min: 4000, max: 7000 },
    { act: 'JACKS', w: 8, min: 0, max: 0 },
    { act: 'SNEEZE', w: 6, min: 2400, max: 2400 },
  ];
  const IDLE_MIN = 4000, IDLE_MAX = 9000, STEP_MS = 180, HOP_MS = 600, BEAT_MS = 300;
  const SPIN_FRAME_MS = 110, NOD_MS = 4500, FLY_MOVE_MS = 160, CATCH_MS = 900;
  const JUGGLE_CYCLE_MS = 900, JUGGLE_PATH_MS = 630, JACK_MS = 300, TYPE_MS = 110;
  const PEEK_OUT = 14, PEEK_SHOW = 9, PEEK_HIDE_MS = 1500, PEEK_HOLD_MS = 1600, PEEK_IN_MS = 120;
  const LEGS_STAND = 0, LEGS_A = 1, LEGS_B = 2, LEGS_TUCKED = 3, LEGS_SIDE_A = 4, LEGS_SIDE_B = 5, LEGS_SPREAD = 6, LEGS_NONE = 7;
  const NAMES = { IDLE: 'Reposo', WAVE: 'Saludar', WALK: 'Pasear', HOP: 'Saltitos', DANCE: 'Bailar', SURPRISED: 'Sobresalto', NAP: 'Siesta', SLEEP: 'Dormir',
                  THINK: 'Pensar', HEART: 'Corazón', SPIN: 'Girar', NOD: 'Cabecear', FIREFLY: 'Luciérnaga',
                  JUGGLE: 'Malabares', PEEK: 'Asomarse', CODE: 'Programar', JACKS: 'Saltos de tijera', SNEEZE: 'Estornudo' };
  const NEW_ACTS = new Set(CLAWD_ACTS.filter(d => d.isNew).map(d => d.act));
  // Vuelta completa: frente, reojo, perfil, espalda, perfil, reojo, frente.
  const SPIN_SEQ = ['front', 'lookR', 'sideR', 'back', 'sideL', 'lookL'];

  const rand = (lo, hi) => hi <= lo ? lo : lo + Math.floor(Math.random() * (hi - lo + 1));
  const s = { act: 'IDLE', startMs: 0, durMs: 0, dx: 0, walkTarget: 0, lastStepMs: 0, steps: 0, lastFiveUsed: -1, lastMinute: -1,
              side: 1, fly: { x: 0, y: 0, lastMove: 0 }, phase: 0, phaseStart: 0, extra: 0 };
  const look = { next: 0, start: 0, dur: 0, off: 0 };
  const blinkT = { next: 0, start: 0, rem: 0 };

  function start(act, now, g) {
    s.act = act; s.startMs = now; s.steps = 0; s.lastStepMs = now;
    s.phase = 0; s.phaseStart = now; s.extra = 0;
    const { minDx, maxDx } = g;
    if (act === 'IDLE') s.durMs = rand(IDLE_MIN, IDLE_MAX);
    else if (act === 'WALK') {
      let t = rand(0, maxDx - minDx) + minDx;
      if (Math.abs(t - s.dx) < 2) t = (s.dx > Math.trunc((minDx + maxDx) / 2)) ? minDx : maxDx;
      s.walkTarget = t; s.durMs = 0;
    } else if (act === 'HOP') s.durMs = HOP_MS * rand(2, 3);
    else if (act === 'SLEEP') s.durMs = 0;
    else if (act === 'DANCE') { const bar = 4 * BEAT_MS; s.durMs = Math.ceil(rand(3000, 5000) / bar) * bar; }
    else if (act === 'SPIN') s.durMs = SPIN_SEQ.length * SPIN_FRAME_MS * rand(1, 2);
    else if (act === 'NOD') s.durMs = NOD_MS;
    else if (act === 'JACKS') s.durMs = 2 * JACK_MS * rand(4, 6);
    else if (act === 'PEEK') s.durMs = 0;
    else { const d = CLAWD_ACTS.find(d => d.act === act); s.durMs = rand(d.min, d.max); }
    // Lado con sitio para bocadillos (puntos, corazones): el mismo criterio que la z.
    const bx = g.x0 + s.dx;
    s.side = (bx + 17 <= g.colR) ? 1 : -1;
    if (act === 'FIREFLY') {
      // Aparece arriba del todo, en el lado contrario.
      s.fly.x = s.side > 0 ? g.colL : g.colR;
      s.fly.y = g.top; s.fly.lastMove = now;
    }
  }
  function pick(allowNew) {
    const list = CLAWD_ACTS.filter(d => allowNew || !d.isNew);
    const total = list.reduce((a, d) => a + d.w, 0);
    let r = Math.floor(Math.random() * total);
    for (const d of list) { if (r < d.w) return d.act; r -= d.w; }
    return 'WAVE';
  }
  function lookOffset(now) {
    if (look.start === 0) {
      if (look.next === 0) look.next = now + rand(4000, 10000);
      if (now < look.next) return 0;
      look.off = Math.random() < .5 ? -1 : 1; look.start = now; look.dur = rand(800, 2300);
    }
    if (now - look.start >= look.dur) { look.start = 0; look.next = now + rand(4000, 10000); return 0; }
    return look.off;
  }
  function blinkRows(now) {
    if (blinkT.start === 0) {
      if (blinkT.next === 0) blinkT.next = now + rand(6000, 15000);
      if (now < blinkT.next) return 0;
      blinkT.rem = 2; blinkT.start = now;
    }
    let e = now - blinkT.start;
    if (e >= 300) {
      if (--blinkT.rem > 0) { blinkT.start = now; e = 0; }
      else { blinkT.start = 0; blinkT.next = now + rand(6000, 15000); return 0; }
    }
    if (e < 60) return 1; if (e < 180) return 2; if (e < 240) return 1; return 0;
  }

  // Mueve la luciérnaga un paso al azar sin meterse dentro de Clawd.
  function moveFly(now, g, bx, by) {
    if (now - s.fly.lastMove < FLY_MOVE_MS) return;
    s.fly.lastMove = now;
    const inside = (x, y) => x >= bx && x <= bx + 13 && y >= by && y <= by + 9;
    const cand = [];
    for (let ddx = -1; ddx <= 1; ddx++) for (let ddy = -1; ddy <= 1; ddy++) {
      const x = s.fly.x + ddx, y = s.fly.y + ddy;
      if (x < g.colL || x > g.colR || y < g.top || y > g.bottom - 2 || inside(x, y)) continue;
      cand.push({ x, y, w: ddy < 0 ? 2 : 1 });   // tiende a subir: no se queda en el suelo
    }
    if (!cand.length) return;
    const total = cand.reduce((a, o) => a + o.w, 0);
    let r = Math.random() * total;
    for (const o of cand) { if ((r -= o.w) < 0) { s.fly.x = o.x; s.fly.y = o.y; return; } }
  }

  // Altura de un salto (px hacia arriba) en funcion de u ∈ [0,1]: parabola.
  const arc = (u, h) => Math.round(h * 4 * u * (1 - u));

  function tick(now, env, g, opts) {
    const { minDx, maxDx } = g;
    if (s.startMs === 0) start('IDLE', now, g);
    if (env.night) { if (s.act !== 'SLEEP') start('SLEEP', now, g); }
    else if (s.act === 'SLEEP') start('SURPRISED', now, g);
    if (env.fiveUsed >= 0) {
      if (s.lastFiveUsed >= 0 && env.fiveUsed > s.lastFiveUsed + 1e-6 && s.act !== 'SLEEP') start('HOP', now, g);
      s.lastFiveUsed = env.fiveUsed;
    }
    if (opts.hourDance) {
      if (s.lastMinute >= 0 && env.minute === 0 && s.lastMinute !== 0 && s.act !== 'SLEEP') start('DANCE', now, g);
      s.lastMinute = env.minute;
    }
    if (s.act !== 'SLEEP' && s.act !== 'WALK' && s.act !== 'PEEK' && now - s.startMs >= s.durMs)
      start(s.act === 'IDLE' ? pick(opts.newActs) : 'IDLE', now, g);
    s.dx = Math.max(minDx, Math.min(maxDx, s.dx));
    const lk = lookOffset(now);

    // armL/armR: 0 abajo, 1 levantado, 2 muy arriba (mano por encima de la cabeza).
    const p = { dx: 0, dy: 0, legs: LEGS_STAND, eyeOff: 0, blinkable: false, happy: false, surprised: false, eyesClosed: false,
                armL: 0, armR: 0, zzz: false, eyesUp: false, halfClosed: false, view: 'front',
                think: 0, heart: -1, fly: null, extra: 0, eyesDown: false, laptop: false, typeL: false, typeR: false,
                balls: null, spray: -1, check: false };
    const e = now - s.startMs;
    const walkLegs = () => (s.steps & 1) ? LEGS_A : LEGS_B;
    switch (s.act) {
      case 'IDLE': p.eyeOff = lk; p.blinkable = true; break;
      case 'WAVE':
        // Brazo arriba todo el rato; la mano sube y baja 1 px (saludo).
        p.happy = true; p.armR = (Math.floor(e / 250) & 1) ? 1 : 2;
        break;
      case 'WALK': {
        const dir = Math.sign(s.walkTarget - s.dx);
        if (now - s.lastStepMs >= STEP_MS) {
          if (dir === 0) { start('IDLE', now, g); p.blinkable = true; break; }
          s.dx += dir; s.steps++; s.lastStepMs = now;
        }
        // Balanceo: en cada paso el cuerpo sube 1 px mientras la pata esta en el aire.
        p.eyeOff = dir; p.legs = walkLegs(); p.blinkable = true;
        p.dy = (now - s.lastStepMs < STEP_MS / 2) ? -1 : 0;
        if (p.dy < 0) p.legs = LEGS_STAND;
        break;
      }
      case 'HOP': {
        const t = e % HOP_MS; p.happy = true;
        if (t < 120) { p.dy = 1; p.legs = LEGS_TUCKED; }
        else if (t < 480) { p.dy = -arc((t - 120) / 360, 3); p.legs = LEGS_TUCKED; p.armL = p.armR = 2; }
        else if (t < 560) { p.dy = 1; p.legs = LEGS_TUCKED; }
        break;
      }
      case 'DANCE': {
        const beat = Math.floor(e / BEAT_MS) & 3; p.happy = true;
        const tb = e % BEAT_MS;
        if (beat === 0 || beat === 2) {
          // Saltito con un brazo arriba en los tiempos fuertes.
          if (beat === 0) p.armL = 2; else p.armR = 2;
          p.dy = -arc(tb / BEAT_MS, 2);
          p.legs = p.dy < 0 ? LEGS_TUCKED : (beat === 0 ? LEGS_A : LEGS_B);
        } else { p.dy = 1; p.legs = LEGS_TUCKED; p.armL = p.armR = 1; }
        break;
      }
      case 'SURPRISED':
        // Bote del susto: se encoge, salta 2 px con los brazos arriba y aterriza.
        if (e < 100) { p.dy = 1; p.legs = LEGS_TUCKED; }
        else if (e < 400) { p.dy = -arc((e - 100) / 300, 2); p.legs = LEGS_TUCKED; }
        p.surprised = true; p.armL = p.armR = e < 400 ? 2 : 1; break;
      case 'NAP': case 'SLEEP':
        // Respira: cada 2,4 s se hunde 1 px durante un momento.
        p.eyesClosed = true; p.zzz = true;
        if ((now % 2400) > 1600) { p.dy = 1; p.legs = LEGS_TUCKED; }
        break;

      case 'THINK':
        // Mira arriba hacia el bocadillo y se rasca la cabeza.
        p.eyesUp = true; p.eyeOff = s.side;
        p.think = Math.min(3, Math.floor(e / 400));
        if (s.side > 0) p.armR = (Math.floor(e / 300) & 1) ? 1 : 2; else p.armL = (Math.floor(e / 300) & 1) ? 1 : 2;
        break;
      case 'HEART':
        // Abrazo (brazos a media altura) y un corazon que late sobre la cabeza.
        p.happy = true; p.armL = p.armR = 1; p.heart = e; break;
      case 'SPIN': {
        // Se da la vuelta pisando en el sitio; al pasar de espaldas da un saltito.
        const f = Math.floor(e / SPIN_FRAME_MS);
        p.view = SPIN_SEQ[f % SPIN_SEQ.length];
        const side = p.view === 'sideR' || p.view === 'sideL';
        p.legs = side ? ((f & 1) ? LEGS_SIDE_A : LEGS_SIDE_B) : ((f & 1) ? LEGS_A : LEGS_B);
        if (p.view === 'back') { p.dy = -1; p.legs = LEGS_TUCKED; }
        break;
      }
      case 'NOD': {
        // Ojos a media asta, dos cabezadas (cada vez mas hondas) y se despierta de un bote.
        if (e >= NOD_MS - 700) {
          const t = e - (NOD_MS - 700);
          p.surprised = true; p.armL = p.armR = 2;
          if (t < 300) { p.dy = -arc(t / 300, 2); p.legs = LEGS_TUCKED; }
        } else if (e >= 1500 && ((e - 1500) % 1000) < 400) {
          p.eyesClosed = true; p.legs = LEGS_TUCKED;
          p.dy = ((e - 1500) >= 1000) ? 2 : 1;
          if (p.dy === 2) p.legs = LEGS_NONE;
        } else p.halfClosed = true;
        break;
      }
      case 'FIREFLY': {
        const bx = g.x0 + s.dx, by = g.y0, cx = bx + 6.5;
        const catching = e >= s.durMs - CATCH_MS;
        if (!catching) {
          moveFly(now, g, bx, by);
          const dir = Math.sign(s.fly.x - cx);
          p.eyeOff = dir; p.eyesUp = s.fly.y < by + 2;
          if (Math.abs(s.fly.x - cx) > 8 && now - s.lastStepMs >= STEP_MS) {
            const nd = s.dx + dir;
            if (nd >= minDx && nd <= maxDx) { s.dx = nd; s.steps++; s.lastStepMs = now; }
          }
          p.legs = (now - s.lastStepMs < STEP_MS && s.steps) ? walkLegs() : LEGS_STAND;
          // Si la tiene justo encima, intenta alcanzarla con la mano.
          if (s.fly.y < by && Math.abs(s.fly.x - cx) < 5) { if (s.fly.x > cx) p.armR = 2; else p.armL = 2; }
          p.fly = { x: s.fly.x, y: s.fly.y };
          p.blinkable = true;
        } else {
          // Baja sobre su cabeza y la caza de un salto de 3 px.
          const t = e - (s.durMs - CATCH_MS);
          const flyTop = { x: Math.round(cx), y: g.top };
          if (t < 250) { p.fly = flyTop; p.eyesUp = true; p.dy = 1; p.legs = LEGS_TUCKED; }
          else if (t < 600) {
            p.dy = -arc((t - 250) / 350, 3); p.legs = LEGS_TUCKED; p.armL = p.armR = 2; p.happy = true;
            if (t < 400) p.fly = flyTop;
          } else p.happy = true;
        }
        break;
      }
      case 'JUGGLE': {
        // Cascada tipo "shower": cada bola sale de la mano izquierda en parabola
        // por encima de la cabeza y cae en la derecha; la vuelta va por detras.
        p.eyesUp = true; p.balls = [];
        let lead = null;
        for (let i = 0; i < 3; i++) {
          const t = (e + i * (JUGGLE_CYCLE_MS / 3)) % JUGGLE_CYCLE_MS;
          if (t < 120 || t > JUGGLE_CYCLE_MS - 80) p.armL = 1;
          if (t >= JUGGLE_PATH_MS - 120 && t < JUGGLE_PATH_MS + 60) p.armR = 1;
          if (t < JUGGLE_PATH_MS) {
            const u = t / JUGGLE_PATH_MS;
            p.balls.push({ u, i });
            if (lead === null || u > lead) lead = u;
          }
        }
        p.eyeOff = lead === null ? 0 : (lead < 0.4 ? -1 : lead > 0.6 ? 1 : 0);
        break;
      }
      case 'CODE': {
        p.laptop = true;
        if (e >= s.durMs - 900) {
          // Compila a la primera: check verde encima y saltito.
          const t = e - (s.durMs - 900);
          p.happy = true; p.armL = p.armR = 2; p.check = true;
          if (t < 300) { p.dy = -arc(t / 300, 1); }
        } else {
          p.eyesDown = true;
          const k = Math.floor(e / TYPE_MS);
          const r = (k * 2654435761) >>> 0;   // pseudo-aleatorio estable por tecla
          p.typeL = (r & 3) !== 0 && (k & 1) === 0;
          p.typeR = (r & 12) !== 0 && (k & 1) === 1;
          // De vez en cuando levanta la vista a pensar.
          if ((e % 2600) > 2100) { p.eyesDown = false; p.eyesUp = true; p.typeL = p.typeR = false; }
        }
        break;
      }
      case 'JACKS': {
        // Cada cambio de postura es un saltito de 1 px.
        const half = Math.floor(e / JACK_MS), t = e % JACK_MS;
        const open = (half & 1) === 1;
        if (t < 100) { p.dy = -1; p.legs = LEGS_TUCKED; p.armL = p.armR = 1; }
        else if (open) { p.armL = p.armR = 2; p.legs = LEGS_SPREAD; }
        break;
      }
      case 'SNEEZE':
        // a… a… (se echa hacia atras) — ¡ACHÍS! (se encoge de golpe).
        if (e < 1000) { p.eyesUp = true; p.halfClosed = (Math.floor(e / 250) & 1) === 0; p.eyesClosed = !p.halfClosed; p.dy = e > 500 ? -1 : 0; if (p.dy) p.legs = LEGS_TUCKED; }
        else if (e < 1300) { p.eyesClosed = true; p.armL = p.armR = 2; p.dy = -2; p.legs = LEGS_TUCKED; }
        else if (e < 1600) { p.eyesClosed = true; p.dy = 1; p.legs = LEGS_TUCKED; }
        if (e >= 1300 && e < 1900) p.spray = Math.floor((e - 1300) / 100);
        break;
      case 'PEEK': {
        // Sale por el borde derecho del panel, asoma medio ojo y vuelve.
        const stepDue = now - s.lastStepMs >= STEP_MS;
        const next = ph => { s.phase = ph; s.phaseStart = now; s.lastStepMs = now; };
        const pe = now - s.phaseStart;
        if (s.phase === 0) {                       // hasta el borde
          if (s.dx >= maxDx) next(1);
          else if (stepDue) { s.dx++; s.steps++; s.lastStepMs = now; }
          p.eyeOff = 1; p.legs = walkLegs();
        } else if (s.phase === 1) {                // sale del panel
          if (s.extra >= PEEK_OUT) next(2);
          else if (stepDue) { s.extra++; s.steps++; s.lastStepMs = now; }
          p.eyeOff = 1; p.legs = walkLegs();
        } else if (s.phase === 2) {                // escondido
          if (pe >= PEEK_HIDE_MS) next(3);
        } else if (s.phase === 3) {                // asoma
          if (s.extra <= PEEK_SHOW) next(4);
          else if (now - s.lastStepMs >= PEEK_IN_MS) { s.extra--; s.lastStepMs = now; }
          p.eyeOff = -1;
        } else if (s.phase === 4) {                // mira, parpadea y saluda con la mano
          p.eyeOff = -1;
          p.halfClosed = pe > 700 && pe < 820;
          if (pe > 900) p.armL = (Math.floor(pe / 250) & 1) ? 1 : 2;
          if (pe >= PEEK_HOLD_MS) next(5);
        } else {                                   // vuelve a entrar
          if (s.extra <= 0) { start('IDLE', now, g); p.blinkable = true; break; }
          if (stepDue) { s.extra--; s.steps++; s.lastStepMs = now; }
          p.eyeOff = -1; p.legs = walkLegs();
        }
        p.extra = s.extra;
        break;
      }
    }
    p.dx = s.dx;
    return p;
  }

  const BODY = [0b00111111111100, 0b00111111111100, 0b00111111111100, 0b00111111111100,
                0b11111111111111, 0b11111111111111, 0b00111111111100, 0b00111111111100];
  const LEGS_FULL = 0b00110100101100, LEGS_A_LOW = 0b00000100001100, LEGS_B_LOW = 0b00110000100000;
  const LEGS_SIDE = 0b00001100110000, LEGS_SIDE_A_LOW = 0b00001100000000, LEGS_SIDE_B_LOW = 0b00000000110000;
  // Piernas abiertas: las interiores se quedan, las exteriores se abren en diagonal.
  const LEGS_SPREAD_HI = 0b01100100100110, LEGS_SPREAD_LOW = 0b11000000000011;
  const ORANGE = '#e07a2f', BLACK = null, ZC = '#5070b0', HEART_C = '#ff4d8d';
  const FLY_ON = '#f5e663', FLY_DIM = '#6b6420', CLOUD_C = '#c9c9c9', CLOUD_DOT = '#555a63';
  const BALL_C = ['#e8e8e8', '#5ad1e6', '#ff4d8d'], LAPTOP_C = '#8c93a0', LAPTOP_EDGE_C = '#5d636e', LOGO_C = '#dfe3ea';
  const SPRAY_C = '#bfe6ff', CHECK_C = '#46c46a';

  function blit(fb, rows, w, x, y, col) {
    rows.forEach((r, yy) => { for (let xx = 0; xx < w; xx++) if (r & (1 << (w - 1 - xx))) px(fb, x + xx, y + yy, col); });
  }

  function drawClawd(fb, now, g, env, opts) {
    const { x0, y0, colR } = g;
    const p = tick(now, env, g, opts);
    let blink = blinkRows(now);
    if (!p.blinkable) blink = 0;
    if (p.halfClosed) blink = 1;
    const bx = x0 + p.dx + p.extra, by = y0 + p.dy;
    const row = (bits, y) => { for (let xx = 0; xx < 14; xx++) if (bits & (0x2000 >> xx)) px(fb, bx + xx, y, ORANGE); };
    for (let yy = 0; yy < 8; yy++) row(BODY[yy], by + yy);
    if (p.legs !== LEGS_NONE) {
      const sideLegs = p.legs === LEGS_SIDE_A || p.legs === LEGS_SIDE_B;
      row(sideLegs ? LEGS_SIDE : p.legs === LEGS_SPREAD ? LEGS_SPREAD_HI : LEGS_FULL, by + 8);
      if (p.legs === LEGS_SPREAD) row(LEGS_SPREAD_LOW, by + 9);
      if (p.legs === LEGS_SIDE_A) row(LEGS_SIDE_A_LOW, by + 9);
      if (p.legs === LEGS_SIDE_B) row(LEGS_SIDE_B_LOW, by + 9);
      if (p.legs === LEGS_STAND) row(LEGS_FULL, by + 9);
      if (p.legs === LEGS_A) row(LEGS_A_LOW, by + 9);
      if (p.legs === LEGS_B) row(LEGS_B_LOW, by + 9);
    }
    // Brazo: nivel 1 = punta en filas 3-4, nivel 2 = punta en filas 2-3.
    const arm = (lvl, cx) => {
      if (!lvl) return;
      px(fb, cx, by + 5, BLACK);
      if (lvl === 2) { px(fb, cx, by + 4, BLACK); px(fb, cx, by + 2, ORANGE); }
      px(fb, cx, by + 3, ORANGE);
    };
    arm(p.armL, bx); arm(p.armR, bx + 13);
    // Teclear: la punta de la mano baja a la fila 6, sobre el teclado.
    if (p.typeL) { px(fb, bx, by + 4, BLACK); px(fb, bx, by + 6, ORANGE); }
    if (p.typeR) { px(fb, bx + 13, by + 4, BLACK); px(fb, bx + 13, by + 6, ORANGE); }

    // De perfil el brazo del fondo queda oculto tras el cuerpo.
    if (p.view === 'sideR') for (const y of [4, 5]) { px(fb, bx, by + y, BLACK); px(fb, bx + 1, by + y, BLACK); }
    if (p.view === 'sideL') for (const y of [4, 5]) { px(fb, bx + 12, by + y, BLACK); px(fb, bx + 13, by + y, BLACK); }

    const eyeShift = p.view === 'lookR' ? 1 : p.view === 'lookL' ? -1 : 0;
    const eL = bx + 4 + p.eyeOff + eyeShift, eR = bx + 9 + p.eyeOff + eyeShift;
    if (p.view === 'back') {
      // de espaldas: sin ojos
    } else if (p.view === 'sideR' || p.view === 'sideL') {
      const ex = p.view === 'sideR' ? bx + 10 : bx + 3;
      px(fb, ex, by + 2, BLACK); px(fb, ex, by + 3, BLACK);
    } else if (p.eyesClosed) {
      px(fb, eL - 1, by + 3, BLACK); px(fb, eL, by + 3, BLACK); px(fb, eR, by + 3, BLACK); px(fb, eR + 1, by + 3, BLACK);
    } else if (p.happy) {
      px(fb, eL, by + 2, BLACK); px(fb, eR, by + 2, BLACK);
      px(fb, eL - 1, by + 3, BLACK); px(fb, eL + 1, by + 3, BLACK); px(fb, eR - 1, by + 3, BLACK); px(fb, eR + 1, by + 3, BLACK);
    } else {
      const base = p.eyesUp ? 1 : p.eyesDown ? 3 : 2;
      const top = p.surprised ? 1 : base + (blink >= 1 ? 1 : 0), bottom = blink >= 2 ? base : base + 1;
      for (let yy = top; yy <= bottom; yy++) { px(fb, eL, by + yy, BLACK); px(fb, eR, by + yy, BLACK); }
    }

    if (p.laptop) {
      // Tapa del portatil vista por detras (tapa las patas) y base mas ancha.
      for (let yy = 6; yy <= 8; yy++) for (let xx = 2; xx <= 11; xx++) px(fb, bx + xx, by + yy, LAPTOP_C);
      for (let xx = 1; xx <= 12; xx++) px(fb, bx + xx, by + 9, LAPTOP_EDGE_C);
      px(fb, bx + 6, by + 7, LOGO_C); px(fb, bx + 7, by + 7, LOGO_C);
    }

    // Todo lo que flota se ancla a la posicion de reposo (y0), no al salto,
    // para que no se mueva con el cuerpo. Techo: g.top (fila 18).
    const sideX = s.side > 0 ? bx + 15 : bx - 4;
    const restTop = y0;
    if (p.zzz) {
      // Dos "z" que suben desde la cabeza hasta el techo, desfasadas.
      for (let k = 0; k < 2; k++) {
        const ph = (now + k * 1400) % 2800;
        const zy = restTop + 3 - Math.floor(ph / 350);
        if (zy >= g.top) blit(fb, [0b111, 0b010, 0b111], 3, sideX, zy, ZC);
      }
    }
    if (p.think) {
      // Puntito junto a la esquina de la cabeza y nube con "..." que se van
      // encendiendo, como el indicador de que Claude esta pensando.
      const dotX = s.side > 0 ? bx + 13 : bx;
      if (p.think >= 1) px(fb, dotX, restTop - 1, CLOUD_C);
      if (p.think >= 2) {
        const cx = s.side > 0 ? bx + 13 : bx - 4;
        blit(fb, [0b01110, 0b11111, 0b01110], 5, cx, g.top, CLOUD_C);
        if (p.think >= 3) {
          const on = Math.floor(now / 250) % 4;
          for (let i = 0; i < 3; i++) if (i < on) px(fb, cx + 1 + i, g.top + 1, CLOUD_DOT);
        }
      }
    }
    if (p.heart >= 0) {
      // Late sobre la cabeza: pequeño, grande, grande, pequeño.
      const big = (Math.floor(p.heart / 300) % 4) !== 0;
      const hx = bx + 5;
      if (big) blit(fb, [0b01010, 0b11111, 0b01110, 0b00100], 5, hx - 1 + 1, g.top, HEART_C);
      else blit(fb, [0b101, 0b111, 0b010], 3, hx + 1, g.top + 1, HEART_C);
    }
    if (p.check) blit(fb, [0b001, 0b001, 0b101, 0b010], 3, bx + 6, g.top, CHECK_C);
    if (p.balls) for (const b of p.balls) {
      // Parabola de mano a mano con el pico en el techo (fila 18).
      const x = bx + Math.round(13 * b.u);
      const y = (restTop + 2) - arc(b.u, (restTop + 2) - g.top);
      px(fb, x, y, BALL_C[b.i]);
    }
    if (p.spray >= 0) {
      const F = [[[0, 3]], [[0, 2], [1, 3], [0, 4]], [[1, 1], [2, 3], [1, 5]], [[2, 1], [3, 3], [2, 5]], [[3, 2], [3, 4]], [[3, 3]]];
      for (const [xx, yy] of (F[p.spray] || [])) {
        const x = s.side > 0 ? bx + 14 + xx : bx - 1 - xx;
        px(fb, x, by + yy, SPRAY_C);
      }
    }
    if (p.fly) px(fb, p.fly.x, p.fly.y, (Math.floor(now / 200) % 4) === 3 ? FLY_DIM : FLY_ON);
    return { bx };
  }

/*CLAWD-LOGIC-END*/
  return { start, drawClawd, st: s };
}
const CLAWD_ANIMS = [
  {key:'wave',      act:'WAVE',      name:'Saludar',          desc:'Cara feliz y la mano en alto.'},
  {key:'walk',      act:'WALK',      name:'Pasear',           desc:'Camina por su rincón mirando hacia donde va.'},
  {key:'hop',       act:'HOP',       name:'Saltitos',         desc:'Dos o tres saltos con los brazos arriba.'},
  {key:'dance',     act:'DANCE',     name:'Bailar',           desc:'Saltitos a compás alternando los brazos.'},
  {key:'surprised', act:'SURPRISED', name:'Sobresalto',       desc:'Bote con los ojos muy abiertos.'},
  {key:'nap',       act:'NAP',       name:'Siesta',           desc:'Se queda dormido un rato; suben las «z».'},
  {key:'think',     act:'THINK',     name:'Pensar',           desc:'Se rasca la cabeza bajo una nube con «…».'},
  {key:'heart',     act:'HEART',     name:'Corazón',          desc:'Un abrazo y un corazón que late encima.'},
  {key:'spin',      act:'SPIN',      name:'Girar',            desc:'Vuelta completa pisando en el sitio.'},
  {key:'nod',       act:'NOD',       name:'Cabecear',         desc:'Da cabezadas y se despierta de un bote.'},
  {key:'firefly',   act:'FIREFLY',   name:'Luciérnaga',       desc:'La persigue con la mirada y la caza.'},
  {key:'juggle',    act:'JUGGLE',    name:'Malabares',        desc:'Tres bolas por encima de la cabeza.'},
  {key:'peek',      act:'PEEK',      name:'Asomarse',         desc:'Sale por el borde y asoma un ojo.'},
  {key:'code',      act:'CODE',      name:'Programar',        desc:'Teclea en un portátil hasta que compila.'},
  {key:'jacks',     act:'JACKS',     name:'Saltos de tijera', desc:'Brazos arriba y piernas abiertas.'},
  {key:'sneeze',    act:'SNEEZE',    name:'Estornudo',        desc:'a… a… ¡achís!'},
];
const CLAWD_REACTS = [
  {key:'react_usage', act:'HOP',   name:'Celebrar tu uso', desc:'Da saltitos cuando sube el % de la ventana de 5 h.'},
  {key:'hour_dance',  act:'DANCE', name:'Hora en punto',   desc:'Baila cuando el reloj llega a :00.'},
  {key:'night_sleep', act:'SLEEP', name:'Dormir de noche', desc:'Duerme mientras dure el modo noche.', night:true},
];
const ZX = 43, ZY = 18, ZW = 21, ZH = 14, CELL = 8;
const players = [];
function makePlayer(canvas, def){
  const fb = new Array(ZW * ZH).fill(null);
  const c = makeClawd((b, x, y, col) => {
    x -= ZX; y -= ZY;
    if (x >= 0 && x < ZW && y >= 0 && y < ZH) fb[y * ZW + x] = col;
  });
  const G = {x0: 46, y0: 22, top: 18, bottom: 31, colL: 43, colR: 63, minDx: -3, maxDx: 4};
  const env = {night: !!def.night, fiveUsed: -1, tempC: 21, minute: 30};
  const opts = {newActs: true, hourDance: false};
  // Desfase por tarjeta para que no vayan todas sincronizadas.
  let now = 100000 + Math.floor(Math.random() * 60000), idleSince = 0;
  c.start(def.act, now, G);
  canvas.width = ZW * CELL; canvas.height = ZH * CELL;
  const ctx = canvas.getContext('2d');
  const p = {
    visible: false,
    step(){
      now += 50;
      fb.fill(null);
      c.drawClawd(fb, now, G, env, opts);
      if (def.act !== 'SLEEP' && c.st.act !== def.act) {
        if (!idleSince) idleSince = now;
        else if (now - idleSince > 900) { c.start(def.act, now, G); idleSince = 0; }
      }
    },
    paint(){
      ctx.fillStyle = '#07080a'; ctx.fillRect(0, 0, canvas.width, canvas.height);
      for (let y = 0; y < ZH; y++) for (let x = 0; x < ZW; x++) {
        const col = fb[y * ZW + x];
        ctx.fillStyle = col || '#14161a';
        ctx.fillRect(x * CELL + 1, y * CELL + 1, CELL - 2, CELL - 2);
      }
    },
  };
  for (let i = 0; i < 12; i++) p.step();
  p.paint();
  return p;
}
function renderAnimCards(){
  const card = d => `
    <label class="anim">
      <canvas aria-hidden="true"></canvas>
      <span class="row"><b>${d.name}</b><span class="toggle"><input type="checkbox" data-anim="${d.key}" checked aria-label="${d.name}"/><span class="toggle-slider"></span></span></span>
      <small>${d.desc}</small>
    </label>`;
  $('#anim-acts').innerHTML = CLAWD_ANIMS.map(card).join('');
  $('#anim-reacts').innerHTML = CLAWD_REACTS.map(card).join('');
  const defs = [...CLAWD_ANIMS, ...CLAWD_REACTS];
  document.querySelectorAll('.anim canvas').forEach((cv, i) => players.push(Object.assign(makePlayer(cv, defs[i]), {el: cv})));
  if ('IntersectionObserver' in window) {
    const io = new IntersectionObserver(es => es.forEach(e => {
      const p = players.find(p => p.el === e.target); if (p) p.visible = e.isIntersecting;
    }));
    players.forEach(p => io.observe(p.el));
  } else players.forEach(p => p.visible = true);
}
document.querySelectorAll('[data-anim-all]').forEach(b => b.onclick = () => {
  const on = b.dataset.animAll === '1';
  document.querySelectorAll('[data-anim]').forEach(cb => cb.checked = on);
  markDirty();
});
{
  const still = window.matchMedia && matchMedia('(prefers-reduced-motion: reduce)').matches;
  let last = performance.now(), acc = 0;
  const loop = t => {
    const dt = Math.min(250, t - last); last = t;
    if (!still && !document.hidden) {
      acc += dt;
      let stepped = false;
      while (acc >= 50) {
        acc -= 50; stepped = true;
        players.forEach(p => { if (p.visible) p.step(); });
      }
      if (stepped) players.forEach(p => { if (p.visible) p.paint(); });
    }
    requestAnimationFrame(loop);
  };
  requestAnimationFrame(loop);
}

renderAnimCards();
loadStatus(); loadWifi(); loadConfig(); loadWeather(); loadProvider(); loadJitter(); loadHola();
startPolls();
</script>
</body></html>
)WTHTML";

const size_t INDEX_HTML_LEN = sizeof(INDEX_HTML) - 1;
