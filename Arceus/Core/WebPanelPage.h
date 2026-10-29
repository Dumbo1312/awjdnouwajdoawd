#pragma once

// ============================================================================
//  WebPanelPage.h
//  ---------------------------------------------------------------------------
//  Embedded single-file control surface served by WebConfigServer at
//  http://localhost:4890/. Exposes the full engine state: System (game /
//  input / capture / model), Targeting (Normal / ADS / triggerbot), Engine
//  (confidence / overlay / recoil), Input (keybinds & sensitivity) and live
//  Telemetry. Every change is POSTed to /config and written straight into
//  config\<game>.usr by the native app.
//
//  To edit the page without recompiling, drop a file named
//  "webpanel\index.html" next to the executable - it overrides this page.
// ============================================================================

static const char* kWebPanelHtml = R"WEB(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Arceus AI</title>
<style>
:root{
  color-scheme: dark;
  --bg:#070404; --bg2:#120808;
  --surface:rgba(20,8,8,.88); --surface-solid:#120a0a;
  --border:rgba(255,160,160,.10); --border-hover:rgba(200,45,45,.55);
  --text:#f2ecec; --muted:#9c8f8f;
  --danger:#e84242;
  --accent:#b81010; --accent2:#d43a3a;
  --grad:linear-gradient(100deg,#d4583f,#a01010 60%,#6d0a0a);
  --mono:"Cascadia Code","Consolas",monospace;
  --body:"Segoe UI Variable","Segoe UI Variable Display","Segoe UI",system-ui,sans-serif;
}
*{box-sizing:border-box;margin:0;padding:0}
::selection{background:rgba(164,18,18,.45); color:#fff}
html,body{height:100%}
body{
  display:flex; flex-direction:column;
  background:
    radial-gradient(1100px 620px at 85% -12%, rgba(164,18,18,.17), transparent 62%),
    radial-gradient(900px 560px at -8% 112%, rgba(200,45,45,.13), transparent 60%),
    radial-gradient(700px 420px at 55% 120%, rgba(130,12,12,.10), transparent 62%),
    var(--bg);
  color:var(--text);
  font:13.5px/1.55 var(--body);
  overflow:hidden;
  -webkit-font-smoothing:antialiased;
  text-rendering:optimizeLegibility;
}
body::before{
  content:""; position:fixed; inset:0; z-index:-2; pointer-events:none;
  background-image:linear-gradient(rgba(255,255,255,.045) 1px, transparent 1px),
                   linear-gradient(90deg, rgba(255,255,255,.045) 1px, transparent 1px);
  background-size:46px 46px;
  -webkit-mask-image:radial-gradient(ellipse 90% 70% at 50% 0%, #000 25%, transparent 78%);
  mask-image:radial-gradient(ellipse 90% 70% at 50% 0%, #000 25%, transparent 78%);
}
body::after{
  content:""; position:fixed; inset:0; z-index:5; pointer-events:none; opacity:.045;
  background-image:url("data:image/svg+xml;utf8,%3Csvg xmlns='http://www.w3.org/2000/svg' width='140' height='140'%3E%3Cfilter id='n'%3E%3CfeTurbulence type='fractalNoise' baseFrequency='0.8' numOctaves='2' stitchTiles='stitch'/%3E%3C/filter%3E%3Crect width='140' height='140' filter='url(%23n)' opacity='0.6'/%3E%3C/svg%3E");
}
#topbar{
  position:relative; display:flex; align-items:center; gap:18px;
  padding:15px 28px;
  background:rgba(5,6,11,.88);
}
#topbar::after{
  content:""; position:absolute; left:0; right:0; bottom:-1px; height:1px;
  background:linear-gradient(90deg, transparent, var(--accent2), var(--accent), var(--danger), transparent);
}
.brand{display:flex; align-items:baseline; gap:10px; user-select:none}
.brand-logo{height:26px; width:auto; display:block; user-select:none; align-self:center;
  filter:drop-shadow(0 0 12px rgba(164,18,18,.35))}
.brand .logo{font-size:20px; font-weight:800; letter-spacing:.26em;
  background:var(--grad); -webkit-background-clip:text; background-clip:text; color:transparent;
  filter:drop-shadow(0 0 16px rgba(164,18,18,.5))}
.logo-tag{font-size:9.5px; font-weight:600; letter-spacing:.34em; color:var(--muted); text-transform:uppercase}
.discord-pill{border:1px solid rgba(88,101,242,.55); background:rgba(88,101,242,.10); color:#aab3ff;
  font:700 10.5px var(--body); letter-spacing:.18em; padding:7px 16px; border-radius:999px;
  cursor:pointer; transition:border-color .15s, background .15s, color .15s, box-shadow .15s, transform .15s}
.discord-pill:hover{background:rgba(88,101,242,.22); color:#fff;
  box-shadow:0 0 20px rgba(88,101,242,.4); transform:translateY(-1px)}
.sys-pill{display:flex; align-items:center; gap:8px; margin-left:auto; padding:7px 14px;
  border:1px solid var(--border); border-radius:999px; background:rgba(255,255,255,.04);
  font:600 10.5px var(--body); letter-spacing:.18em; color:var(--muted);
  transition:border-color .15s, color .15s}
.sys-pill .dot{width:8px; height:8px; border-radius:50%; background:var(--accent2);
  box-shadow:0 0 10px var(--accent2); animation:pulse 2s infinite}
.sys-pill.live .dot{background:#34d399; box-shadow:0 0 10px #34d399}
.sys-pill.live{color:#a7f3d0; border-color:rgba(52,211,153,.4)}
@keyframes pulse{0%,100%{opacity:1; transform:scale(1)}50%{opacity:.4; transform:scale(.85)}}
#sysClock{font-family:var(--mono); font-size:11px; letter-spacing:.08em; color:var(--muted); padding:7px 0}

#app{flex:1; display:flex; min-height:0}
#nav{
  display:flex; flex-direction:column; gap:8px; padding:18px 12px;
  border-right:1px solid var(--border); background:rgba(5,6,11,.82);
  overflow-y:auto; overflow-x:hidden; scrollbar-width:none;
}
#nav::-webkit-scrollbar{display:none}
.nav{
  position:relative; display:flex; align-items:center; gap:12px;
  width:46px; height:42px; padding:0 12px;
  background:transparent; border:1px solid transparent; border-radius:11px;
  color:var(--muted); cursor:pointer; overflow:hidden; white-space:nowrap;
  transition:width .18s, background .12s, border-color .12s, color .12s, box-shadow .12s;
}
.nav .nav-ic{flex:0 0 auto; display:flex; align-items:center; justify-content:center}
.nav .nav-ic svg{width:20px; height:20px; fill:none; stroke:currentColor; stroke-width:1.6; stroke-linecap:round; stroke-linejoin:round}
.nav .nav-lb{font:600 13px var(--body); letter-spacing:.06em; opacity:0; transition:opacity .12s}
.nav:hover{color:var(--text); background:rgba(255,255,255,.05); border-color:var(--border)}
.nav.open{width:180px; color:var(--text); background:rgba(255,255,255,.06); border-color:rgba(164,18,18,.55); box-shadow:inset 0 0 22px rgba(164,18,18,.08)}
.nav.open .nav-lb{opacity:1}
.nav.active{color:#fff; background:linear-gradient(135deg,#d43a3a,#b81010); border-color:transparent; box-shadow:0 4px 20px rgba(164,18,18,.42)}
.nav.active.open .nav-lb{opacity:1}
.nav::before{content:""; position:absolute; left:0; top:50%; transform:translateY(-50%); width:3px; height:0;
  background:var(--grad); border-radius:2px; opacity:.9; transition:height .15s}
.nav:hover::before, .nav.active::before{height:24px}

#content{flex:1; overflow-y:auto; padding:28px 30px 52px; scrollbar-gutter:stable}
#content::-webkit-scrollbar{width:10px}
#content::-webkit-scrollbar-track{background:transparent}
#content::-webkit-scrollbar-thumb{background:rgba(164,18,18,.28); border-radius:9px; border:2px solid transparent; background-clip:content-box}
#content::-webkit-scrollbar-thumb:hover{background:rgba(164,18,18,.5); background-clip:content-box; border:2px solid transparent}
#pageHead{display:flex; align-items:baseline; gap:14px; margin-bottom:6px}
#pageTitle{font-size:17px; font-weight:700; letter-spacing:.14em; text-transform:uppercase; color:var(--text)}

.cards{display:grid; grid-template-columns:repeat(3,1fr); gap:18px; align-items:stretch}
.card{
  grid-column:span 1; min-width:0;
  background:var(--surface);
  border:1px solid var(--border); border-radius:16px;
  padding:18px 18px 16px;
  position:relative;
  transition:border-color .15s, transform .15s, box-shadow .15s;
  animation:rise .32s ease both;
}
.card::before{content:""; position:absolute; inset:0; border-radius:16px; padding:1px; pointer-events:none;
  background:linear-gradient(160deg, rgba(200,45,45,.35), transparent 30%, transparent 70%, rgba(130,12,12,.35));
  -webkit-mask:linear-gradient(#000 0 0) content-box, linear-gradient(#000 0 0);
  -webkit-mask-composite:xor; mask-composite:exclude; opacity:0; transition:opacity .2s}
.card::after{content:""; position:absolute; top:8px; left:8px; right:8px; bottom:8px; pointer-events:none; opacity:.65;
  background:
    linear-gradient(rgba(200,45,45,.75), rgba(200,45,45,.75)) top left / 14px 2px no-repeat,
    linear-gradient(rgba(200,45,45,.75), rgba(200,45,45,.75)) top left / 2px 14px no-repeat,
    linear-gradient(rgba(130,12,12,.75), rgba(130,12,12,.75)) bottom right / 14px 2px no-repeat,
    linear-gradient(rgba(130,12,12,.75), rgba(130,12,12,.75)) bottom right / 2px 14px no-repeat;
  transition:opacity .2s}
.card:hover::after{opacity:1}
.card.span-2{grid-column:span 2}
.card.span-3{grid-column:span 3}
.cards.half{grid-template-columns:1fr 1fr}
.cards.half .card{grid-column:span 1}
.card:nth-child(2){animation-delay:.04s}
.card:nth-child(3){animation-delay:.08s}
.card:nth-child(4){animation-delay:.12s}
.card:nth-child(5){animation-delay:.16s}
.card:nth-child(6){animation-delay:.20s}
@keyframes rise{from{opacity:0; transform:translateY(12px)}to{opacity:1; transform:translateY(0)}}
.card:hover{border-color:rgba(164,18,18,.65); transform:translateY(-2px);
  box-shadow:0 14px 44px rgba(0,0,0,.45), 0 0 34px rgba(164,18,18,.16)}
.card:hover::before{opacity:1}
.card-head{
  display:flex; align-items:center; justify-content:space-between;
  padding-bottom:12px; margin-bottom:14px; position:relative;
}
.card-head::after{content:""; position:absolute; left:0; right:0; bottom:0; height:1px;
  background:linear-gradient(90deg, rgba(164,18,18,.55), rgba(200,45,45,.35) 55%, transparent)}
.card-head h3{font-size:12px; font-weight:700; letter-spacing:.14em; text-transform:uppercase; color:var(--text)}
.row{display:flex; align-items:center; justify-content:space-between; gap:12px; margin:12px 0}
.row .label{
  flex:0 0 150px; color:var(--muted); font-size:13px; letter-spacing:.02em;
}
.row .label::after{content:"\00a0"; }
input[type=range]{
  flex:1; -webkit-appearance:none; appearance:none; height:4px; border-radius:4px;
  background:linear-gradient(90deg, rgba(200,45,45,.5), rgba(164,18,18,.45));
  outline:none; cursor:pointer;
}
input[type=range]::-webkit-slider-thumb{
  -webkit-appearance:none; appearance:none; width:15px; height:15px; border-radius:50%;
  background:#fff; border:2px solid var(--accent2); box-shadow:0 0 12px rgba(200,45,45,.75);
  cursor:pointer; transition:transform .12s;
}
input[type=range]::-webkit-slider-thumb:hover{transform:scale(1.2); border-color:#b81010}
input[type=range]::-moz-range-thumb{
  width:15px; height:15px; border-radius:50%; background:#fff; border:2px solid var(--accent2);
  box-shadow:0 0 12px rgba(200,45,45,.75); cursor:pointer;
}
input[type=range]::-moz-range-track{height:4px; border-radius:4px;
  background:linear-gradient(90deg, rgba(200,45,45,.5), rgba(164,18,18,.45))}
.readout{
  flex:0 0 62px; text-align:right; color:var(--accent2);
  font-family:var(--mono); font-size:12px; font-variant-numeric:tabular-nums;
  text-shadow:0 0 10px rgba(200,45,45,.5);
}
select, input[type=text]{
  background-color:rgba(255,255,255,.045); border:1px solid var(--border); color:var(--text);
  border-radius:9px; padding:8px 12px; font:inherit; outline:none; cursor:pointer;
  transition:border-color .12s, background-color .12s, box-shadow .12s;
}
select{
  -webkit-appearance:none; appearance:none;
  flex:1 1 auto; width:auto; max-width:340px; padding-right:34px;
  background-image:url("data:image/svg+xml;utf8,%3Csvg xmlns='http://www.w3.org/2000/svg' width='11' height='7' viewBox='0 0 11 7'%3E%3Cpath d='M1.5 1.5l4 4 4-4' fill='none' stroke='%23b81010' stroke-width='1.7' stroke-linecap='round' stroke-linejoin='round'/%3E%3C/svg%3E");
  background-repeat:no-repeat; background-position:right 12px center;
}
select:hover{
  background-image:url("data:image/svg+xml;utf8,%3Csvg xmlns='http://www.w3.org/2000/svg' width='11' height='7' viewBox='0 0 11 7'%3E%3Cpath d='M1.5 1.5l4 4 4-4' fill='none' stroke='%23d43a3a' stroke-width='1.7' stroke-linecap='round' stroke-linejoin='round'/%3E%3C/svg%3E");
}
select:hover, input[type=text]:hover{border-color:var(--border-hover)}
select:focus, input[type=text]:focus{border-color:var(--accent); background-color:rgba(255,255,255,.07); box-shadow:0 0 0 3px rgba(164,18,18,.15)}
select{font-size:13px}
option{background:#10121c; color:#f4f5fb}
input[type=text]{flex:1; cursor:text; font-size:13px}
input[type=text]::placeholder{color:#60657a}
.keybind{
  flex:0 0 auto; width:200px; text-align:center;
  background:rgba(255,255,255,.045); border:1px solid var(--border); color:var(--text);
  border-radius:9px; padding:7px 10px; font:600 11.5px var(--mono); letter-spacing:.08em;
  cursor:pointer; transition:border-color .12s, box-shadow .12s, color .12s;
}
.keybind:hover{border-color:var(--border-hover); box-shadow:0 0 12px rgba(164,18,18,.15)}
.keybind.listening{
  border-color:var(--accent2); color:var(--accent2);
  box-shadow:0 0 0 3px rgba(200,45,45,.2);
  animation:blink 1s infinite;
}
@keyframes blink{0%,100%{opacity:1}50%{opacity:.55}}
input[type=color]{
  width:52px; height:30px; border:1px solid var(--border); border-radius:9px;
  background:rgba(255,255,255,.045); padding:3px; cursor:pointer;
}
.colorGroup{flex:1; display:flex; justify-content:flex-end; gap:10px; align-items:center}
.colorCell{display:flex; align-items:center; gap:4px; font-family:var(--mono); font-size:10px; letter-spacing:.08em; color:var(--muted); text-transform:uppercase}
.colorCell input[type=color]{width:38px; height:24px}
button.danger{
  background:transparent; border:1px solid var(--border); color:var(--muted);
  border-radius:9px; padding:10px 18px; font:600 12.5px var(--body); letter-spacing:.08em;
  cursor:pointer; transition:border-color .12s, color .12s, background .12s;
}
button.danger:hover{border-color:rgba(255,77,77,.65); color:var(--danger); background:rgba(255,77,77,.06)}
button.go{
  position:relative; overflow:hidden;
  width:100%;
  background:linear-gradient(135deg, #d43a3a, #b81010 55%, #7a0c0c);
  border:none; color:#fff; font:700 12px var(--body);
  letter-spacing:.22em; text-indent:.22em;
  border-radius:10px; padding:11px 18px; cursor:pointer;
  box-shadow:0 4px 18px rgba(164,18,18,.32);
  transition:transform .08s, box-shadow .12s, filter .12s, background .12s;
}
button.go::after{content:""; position:absolute; top:0; left:-80%; width:50%; height:100%;
  background:linear-gradient(100deg, transparent, rgba(255,255,255,.3), transparent);
  transform:skewX(-20deg); transition:left .5s ease}
button.go:hover::after{left:130%}
button.go:hover{filter:brightness(1.12); box-shadow:0 8px 32px rgba(164,18,18,.55)}
button.go:active{transform:scale(.97)}
button.go:disabled{background:#171a27; color:#6c7186; box-shadow:none; cursor:default}
button.go.stop{border:1px solid rgba(255,77,77,.75); color:#e84242; background:rgba(255,77,77,.09); box-shadow:0 0 24px rgba(255,77,77,.16)}
button.go.stop:hover{background:rgba(255,77,77,.2); color:#fff; filter:none}
.info-v{color:var(--accent2); font-family:var(--mono); font-size:12.5px; font-variant-numeric:tabular-nums; flex:1; text-align:right}
.switch{display:inline-flex; align-items:center; gap:11px; cursor:pointer; user-select:none}
.switch input{display:none}
.switch .track{
  width:42px; height:23px; border-radius:12px; background:#171a27;
  position:relative; transition:background .15s, box-shadow .15s;
}
.switch .track .thumb{
  position:absolute; top:3px; left:3px; width:17px; height:17px; border-radius:50%;
  background:#7c8099; transition:left .15s, background .15s;
}
.switch input:checked + .track{background:linear-gradient(90deg,#d43a3a,#b81010); box-shadow:0 0 14px rgba(164,18,18,.5)}
.switch input:checked + .track .thumb{left:22px; background:#fff}
.switch .lbl{color:var(--text); font-size:13px}
@media (max-width:900px){
  #app{flex-direction:column}
  #nav{flex-direction:row; border-right:none; border-bottom:1px solid var(--border); overflow-x:auto; padding:10px}
  .nav, .nav:hover, .nav.open, .nav.active{width:46px}
  .nav-lb{display:none}
  .cards{grid-template-columns:1fr}
  .card, .card.span-2, .card.span-3{grid-column:span 1}
}

/* --- Login gate ---------------------------------------------------------- */
#login{display:none; position:fixed; inset:0; z-index:50; align-items:center; justify-content:center;
  background:rgba(3,4,9,.78)}
body.loginMode #login{display:flex}
body.loginMode #topbar, body.loginMode #app{display:none}
#closed{display:none; position:fixed; inset:0; z-index:60; align-items:center; justify-content:center;
  background:rgba(3,4,9,.78)}
body.closedMode #closed{display:flex}
body.closedMode #topbar, body.closedMode #app, body.closedMode #login{display:none}
.closed-title{font-size:16px; font-weight:800; letter-spacing:.16em; color:var(--text); text-align:center}
.closed-sub{font:600 11px var(--body); letter-spacing:.26em; color:var(--muted); text-align:center; margin-top:-6px}
.login-card{width:400px; max-width:92vw; position:relative; background:rgba(13,15,24,.94);
  border:1px solid rgba(255,255,255,.1);
  border-radius:20px; padding:40px 32px 30px; display:flex; flex-direction:column; gap:16px;
  box-shadow:0 30px 80px rgba(0,0,0,.65), 0 0 60px rgba(164,18,18,.13);
  animation:loginIn .35s ease both}
@keyframes loginIn{from{opacity:0; transform:scale(.95) translateY(10px)}to{opacity:1; transform:none}}
.login-card::before{content:""; position:absolute; inset:-1px; border-radius:20px; padding:1px; pointer-events:none;
  background:var(--grad); opacity:.55;
  -webkit-mask:linear-gradient(#000 0 0) content-box, linear-gradient(#000 0 0);
  -webkit-mask-composite:xor; mask-composite:exclude}
.login-sub{font:600 9.5px var(--body); letter-spacing:.42em; color:var(--muted); text-align:center; margin-top:-10px; text-transform:uppercase}
.login-brand{font-size:26px; font-weight:800; letter-spacing:.3em; text-indent:.3em; text-transform:uppercase;
  background:var(--grad); -webkit-background-clip:text; background-clip:text; color:transparent;
  text-align:center; filter:drop-shadow(0 0 18px rgba(164,18,18,.5))}
#loginKey{
  width:100%; background:rgba(255,255,255,.05); border:1px solid var(--border); border-radius:10px;
  color:var(--text); font:600 13px var(--mono); letter-spacing:.12em; padding:12px 14px;
  outline:none; transition:border-color .12s, box-shadow .12s;
}
#loginKey:focus{border-color:var(--accent); box-shadow:0 0 0 3px rgba(164,18,18,.18)}
#loginKey::placeholder{color:#555a70; letter-spacing:.08em}
#loginMsg{min-height:16px; color:#e84242; font:600 12px var(--body); text-align:center; letter-spacing:.04em}

@media (prefers-reduced-motion: reduce){ .card{animation:none} }
</style>
</head>)WEB"
R"WEB(
<body>
<div id="login">
  <div class="login-card">
    <div class="login-brand">Arceus AI</div>
    <input type="password" id="loginKey" placeholder="License key" autocomplete="off" spellcheck="false">
    <label class="switch"><input type="checkbox" id="loginRemember"><span class="track"><span class="thumb"></span></span><span class="lbl">Remember me</span></label>
    <button type="button" class="go" id="loginBtn">LOGIN</button>
    <div id="loginMsg"></div>
  </div>
</div>
<div id="closed">
  <div class="login-card">
    <div class="login-brand">Arceus AI</div>
    <div class="closed-title">Is closed</div>
    <div class="closed-sub">Reopen to start again</div>
  </div>
</div>
<header id="topbar">
  <div class="brand"><img id="brandLogo" alt="" class="brand-logo"><span class="logo">Arceus AI</span></div>
  <div class="sys-pill" id="sysPill"><span class="dot"></span><span id="sysText">IDLE</span></div>
  <button type="button" class="discord-pill" id="discordBtn" title="discord.gg/FntYdwbm8m">DISCORD</button>
  <span id="sysClock"></span>
</header>

<div id="app">
<nav id="nav"></nav>

<main id="content">
  <div id="pageHead">
    <h1 id="pageTitle">Setup</h1>
  </div>
  <div class="cards" id="page"></div>
</main>
</div>

<script>
"use strict";
var cfg = {};
var currentTab = 'setup';
var running = false;

// Dotted-path helpers for nested config keys, e.g. "HipFire.strength"
function getKey(obj, path){
  var parts = path.split(".");
  var v = obj;
  for(var i=0;i<parts.length;i++){
    if(v==null) return undefined;
    v = v[parts[i]];
  }
  return v;
}
function setKey(obj, path, val){
  var parts = path.split(".");
  var v = obj;
  for(var i=0;i<parts.length-1;i++){
    if(v[parts[i]]==null || typeof v[parts[i]]!=="object") v[parts[i]] = {};
    v = v[parts[i]];
  }
  v[parts[parts.length-1]] = val;
}
// Deep-merge src into dst (objects merged recursively, scalars overwritten).
// Used so a single control change never wipes sibling fields of its section.
function deepMerge(dst, src){
  for(var k in src){
    if(!src.hasOwnProperty(k)) continue;
    if(src[k] && typeof src[k]==="object" && !Array.isArray(src[k]) &&
       dst[k] && typeof dst[k]==="object"){
      deepMerge(dst[k], src[k]);
    } else {
      dst[k] = src[k];
    }
  }
  return dst;
}

var SCHEMA = {
  setup: {
    nav: "Setup", title: "Setup",
    cards: [
      { title: "Game", w:2, controls: [
        {t:"select", label:"Game", key:"Input.game", list:"games"},
        {t:"text",   label:"Window Name", key:"Input.customGameName", show:"__custom", ph:"MyGame-Win64-Shipping"}
      ]},
      { title: "AI Model", controls: [
        {t:"select", label:"Model", key:"Network.modelChoice", list:"models"}
      ]},
      { title: "Capture & Input", w:3, controls: [
        {t:"select", label:"Input Method", key:"Input.method", list:"inputMethods"},
        {t:"select", label:"Capture Method", key:"Input.capture", list:"captureMethods"},
        {t:"select", label:"Capture Device", key:"Input.captureCardDeviceIndex", list:"captureDevices", show:"__captureCard"}
      ]},
      { title: "Start", w:3, controls: [
        {t:"start", label:"START", action:"start"}
      ]}
    ]
  },
  aim: {
    nav: "Aim", title: "Aim",
    cards: [
      { title: "Normal", w:2, controls: [
        {t:"slider", label:"Strength", key:"HipFire.strength", min:1, max:100, step:1, dec:0, suffix:"%", scale:100},
        {t:"select", label:"Aim Height", key:"HipFire.aimHeightChoice", list:"aimHeights"},
        {t:"slider", label:"FOV", key:"HipFire.fov", min:1, max:640, step:1, dec:0, suffix:"px", maxFrom:"Network.size"},
        {t:"slider", label:"Deadzone", key:"HipFire.deadzone", min:0, max:10, step:1, dec:0, suffix:"px"}
      ]},
      { title: "ADS", controls: [
        {t:"slider", label:"Strength", key:"Ads.strength", min:1, max:100, step:1, dec:0, suffix:"%", scale:100},
        {t:"select", label:"Aim Height", key:"Ads.aimHeightChoice", list:"aimHeights"},
        {t:"slider", label:"FOV", key:"Ads.fov", min:1, max:640, step:1, dec:0, suffix:"px", maxFrom:"Network.size"},
        {t:"slider", label:"Deadzone", key:"Ads.deadzone", min:0, max:10, step:1, dec:0, suffix:"px"}
      ]},
      { title: "Triggerbot", w:3, controls: [
        {t:"toggle", label:"Enable Triggerbot", key:"Trigger.enabled"},
        {t:"slider", label:"Proximity", key:"Trigger.proximity", min:0, max:50, step:1, dec:0, suffix:"px"},
        {t:"keybind", label:"Trigger Key", key:"Trigger.bind"}
      ]}
    ]
  },
  ai: {
    nav: "AI", title: "AI",
    cards: [
      { title: "Detection & Recoil", controls: [
        {t:"slider", label:"Confidence", key:"Network.confidence", min:0.01, max:100, step:0.01, dec:2, suffix:"%", scale:100},
        {t:"toggle", label:"Anti Recoil", key:"Recoil.enabled"},
        {t:"slider", label:"Recoil Strength", key:"Recoil.strength", min:0.1, max:10, step:0.01, dec:2, suffix:"px"},
        {t:"keybind", label:"Recoil Key", key:"Recoil.bind"}
      ]},
      { title: "Overlay", controls: [
        {t:"select", label:"FOV Shape", key:"Network.fovShape", list:"fovShapes"},
        {t:"slider", label:"Third-Person Mult", key:"Network.thirdPersonMultiplier", min:1, max:50, step:0.1, dec:1, suffix:"x"},
      ]}
    ]
  },
  binds: {
    nav: "Hotkeys", title: "Hotkeys",
    cards: [
      { title: "Binds", controls: [
        {t:"keybind", label:"Hold Aim", key:"Binds.aim"},
        {t:"keybind", label:"Toggle Aim", key:"Binds.toggleAim"},
        {t:"keybind", label:"Hide Menu", key:"Binds.toggleMenu"},
        {t:"keybind", label:"ADS Bind", key:"Binds.ads"},
        {t:"keybind", label:"Trigger Key", key:"Trigger.bind"}
      ]},
      { title: "Sensitivity & Appearance", controls: [
        {t:"slider", label:"X & Y Sens", key:"HipFire.sens", min:1, max:100, step:0.1, dec:1, suffix:"%"},
        {t:"slider", label:"ADS Sens", key:"Ads.sens", min:1, max:100, step:0.1, dec:1, suffix:"%"},
        {t:"text", label:"Accent Hex", key:"Appearance.accentHex", ph:"FFFFFF"}
      ]}
    ]
  },
  info: {
    nav: "Status", title: "Status",
    cards: [
      { title: "Performance", w:2, controls: [
        {t:"info", label:"Screenshot", key:"Internal.screenshotMs", suffix:" ms"},
        {t:"info", label:"Inference", key:"Internal.inferenceMs", suffix:" ms"},
        {t:"info", label:"Processing", key:"Internal.processingMs", suffix:" ms"}
      ]},
      { title: "Actions", controls: [
        {t:"button", label:"Reset to defaults", action:"reset"},
        {t:"exit", label:"Shutdown Application"}
      ]},
      { title: "System", w:3, controls: [
        {t:"info", label:"Resolution", key:"__res"},
        {t:"info", label:"State", key:"__state"}
      ]}
    ]
  }
};

var VK = {
  1:"L BUTTON",2:"R BUTTON",4:"M BUTTON",5:"MOUSE 4",6:"MOUSE 5",
  8:"BACKSPACE",9:"TAB",13:"ENTER",16:"SHIFT",17:"CONTROL",18:"ALT",
  27:"ESC",32:"SPACE",33:"PAGE UP",34:"PAGE DOWN",35:"END",36:"HOME",
  37:"LEFT ARROW",38:"UP ARROW",39:"RIGHT ARROW",40:"DOWN ARROW",
  44:"PRINT SCREEN",45:"INSERT",46:"DELETE",
  112:"F1",113:"F2",114:"F3",115:"F4",116:"F5",117:"F6",
  118:"F7",119:"F8",120:"F9",121:"F10",122:"F11",123:"F12"
};
function vkName(vk){
  if(!vk || vk===0) return "UNBOUND";
  if(VK[vk]) return VK[vk];
  if(vk>=48 && vk<=57) return String.fromCharCode(vk);
  if(vk>=65 && vk<=90) return String.fromCharCode(vk);
  if(vk>=96 && vk<=105) return "NUM " + (vk-96);
  return "0x" + vk.toString(16).toUpperCase();
}

function listOf(name){
  return (cfg.Lists && cfg.Lists[name]) ? cfg.Lists[name] : [];
}

function rowHTML(ctl){
  var show = ctl.show ? ' data-show="'+ctl.show+'"' : '';
  switch(ctl.t){
    case "toggle":
      return '<div class="row"'+show+'><label class="switch"><input type="checkbox" data-key="'+ctl.key+'"><span class="track"><span class="thumb"></span></span><span class="lbl">'+ctl.label+'</span></label></div>';
    case "slider":
      return '<div class="row"'+show+'><span class="label">'+ctl.label+'</span><input type="range" data-key="'+ctl.key+'" min="'+ctl.min+'" max="'+ctl.max+'" step="'+ctl.step+'" data-scale="'+(ctl.scale||1)+'" data-dec="'+(ctl.dec!==undefined?ctl.dec:0)+'" data-suffix="'+(ctl.suffix||"")+'" data-maxfrom="'+(ctl.maxFrom||"")+'"><span class="readout" data-readout="'+ctl.key+'"></span></div>';
    case "select": {
      var opts = "";
      var items = ctl.list ? listOf(ctl.list) : (ctl.opts||[]);
      for(var i=0;i<items.length;i++) opts += '<option value="'+i+'">'+items[i]+'</option>';
      return '<div class="row"'+show+'><span class="label">'+ctl.label+'</span><select data-key="'+ctl.key+'" data-list="'+(ctl.list||"")+'">'+opts+'</select></div>';
    }
    case "keybind":
      return '<div class="row"'+show+'><span class="label">'+ctl.label+'</span><button type="button" class="keybind" data-key="'+ctl.key+'"></button></div>';
    case "color":
      return '<div class="row"'+show+'><span class="label">'+ctl.label+'</span><input type="color" data-key="'+ctl.key+'"></div>';
    case "colors": {
      var cells = "";
      for(var i=0;i<ctl.keys.length;i++){
        cells += '<label class="colorCell"><span>'+ctl.labels[i]+'</span><input type="color" data-key="'+ctl.keys[i]+'"></label>';
      }
      return '<div class="row"'+show+'><span class="label">'+ctl.label+'</span><div class="colorGroup">'+cells+'</div></div>';
    }
    case "text":
      return '<div class="row"'+show+'><span class="label">'+ctl.label+'</span><input type="text" data-key="'+ctl.key+'" placeholder="'+(ctl.ph||"")+'"></div>';
    case "info":
      return '<div class="row"'+show+'><span class="label">'+ctl.label+'</span><span class="info-v" data-info="'+ctl.key+'" data-suffix="'+(ctl.suffix||"")+'"></span></div>';
    case "start":
      return '<div class="row"'+show+'><button type="button" class="go" data-action="'+ctl.action+'">'+ctl.label+'</button></div>';
    case "button":
      return '<div class="row"'+show+'><button type="button" class="danger" data-action="'+ctl.action+'">'+ctl.label+'</button></div>';
    case "exit":
      return '<div class="row"'+show+'><button type="button" class="danger" data-action="exit">'+ctl.label+'</button></div>';
  }
  return "";
}

function cardHTML(card){
  var rows = "";
  for(var i=0;i<card.controls.length;i++) rows += rowHTML(card.controls[i]);
  return '<div class="card'+(card.w? " span-"+card.w : "")+'"><div class="card-head"><h3>'+card.title+'</h3></div>'+rows+'</div>';
}

function render(){
  var tab = SCHEMA[currentTab];
  document.getElementById("pageTitle").textContent = tab.title;
  var cards = "";
  for(var i=0;i<tab.cards.length;i++) cards += cardHTML(tab.cards[i]);
  document.getElementById("page").innerHTML = cards;
  document.getElementById("page").className = "cards" + ((currentTab==="binds" || currentTab==="ai") ? " half" : "");
  applyValues();
}

function updateReadout(key){
  var el = document.querySelector('[data-readout="'+key+'"]');
  var r  = document.querySelector('input[type=range][data-key="'+key+'"]');
  if(!el||!r) return;
  var scale = parseFloat(r.dataset.scale||1);
  var dec   = parseInt(r.dataset.dec||0,10);
  var suffix = r.dataset.suffix||"";
  el.textContent = (parseFloat(r.value)).toFixed(dec) + suffix;
}

function paintSlider(r){
  if(!r) return;
  var min = parseFloat(r.min)||0, max = parseFloat(r.max)||100;
  var v = parseFloat(r.value); if(isNaN(v)) v = min;
  var p = (max>min) ? Math.round(Math.min(1, Math.max(0, (v-min)/(max-min)))*100) : 0;
  r.style.background = "linear-gradient(90deg, rgba(200,45,45,.9) 0%, rgba(164,18,18,.9) "+p+"%, rgba(255,255,255,.05) "+p+"%)";
}

// Dirty tracking: after the user touches a control we don't let poll()
// overwrite it until the POST round-trip finished (~600ms grace).
var dirtyTimes = {};
function markDirty(key){ dirtyTimes[key] = Date.now(); }
function isDirty(key){ return (Date.now() - (dirtyTimes[key]||0)) < 700; }

function applyValues(){
  if(!cfg) return;
  var els = document.querySelectorAll('input[type=checkbox][data-key]');
  for(var i=0;i<els.length;i++){
    if(isDirty(els[i].dataset.key)) continue;
    els[i].checked = !!getKey(cfg, els[i].dataset.key);
  }
  var rs = document.querySelectorAll('input[type=range][data-key]');
  for(var i=0;i<rs.length;i++){
    if(document.activeElement===rs[i]) continue;
    if(isDirty(rs[i].dataset.key)) continue;
    var scale = parseFloat(rs[i].dataset.scale||1);
    var mf = rs[i].dataset.maxfrom;
    if(mf){
      // FOV tracks the model input size (320 for 320px models, 640 for 640px);
      // before the model loads (size=-1) fall back to 320.
      var m = parseInt(getKey(cfg, mf),10);
      rs[i].max = (m>1) ? m : 320;
      var v = parseFloat(getKey(cfg, rs[i].dataset.key)) * scale;
      rs[i].value = Math.min(v, parseFloat(rs[i].max));
    } else {
      rs[i].value = getKey(cfg, rs[i].dataset.key) * scale;
    }
    updateReadout(rs[i].dataset.key);
    paintSlider(rs[i]);
  }
  var ss = document.querySelectorAll('select[data-key]');
  for(var i=0;i<ss.length;i++){
    if(isDirty(ss[i].dataset.key)) continue;
      var titles = listOf("windows");
      var idx = titles.indexOf(wanted);
      ss[i].value = String(idx>=0 ? idx : 0);
      continue;
    }
    var val = getKey(cfg, ss[i].dataset.key);
    if(val===undefined || val===null) val = 0;
    ss[i].value = String(val);
  }
  var kb = document.querySelectorAll('button.keybind[data-key]');
  for(var i=0;i<kb.length;i++){
    if(isDirty(kb[i].dataset.key)) continue;
    kb[i].textContent = vkName(getKey(cfg, kb[i].dataset.key));
  }
  var cc = document.querySelectorAll('input[type=color][data-key]');
  for(var i=0;i<cc.length;i++){
    if(isDirty(cc[i].dataset.key)) continue;
    var c = getKey(cfg, cc[i].dataset.key);
    cc[i].value = "#" + ((c&0xFFFFFF)>>>0).toString(16).padStart(6,"0");
  }
  var tx = document.querySelectorAll('input[type=text][data-key]');
  for(var i=0;i<tx.length;i++){
    if(document.activeElement===tx[i]) continue;
    if(isDirty(tx[i].dataset.key)) continue;
    var v = getKey(cfg, tx[i].dataset.key);
    if(v!==undefined && v!==null) tx[i].value = v;
  }
  var inf = document.querySelectorAll('[data-info]');
  for(var i=0;i<inf.length;i++){
    var k = inf[i].dataset.info;
    var v = getKey(cfg, k);
    if(k==="__res") v = (getKey(cfg,"Internal.ResolutionW")||0)+" x "+(getKey(cfg,"Internal.ResolutionH")||0);
    if(k==="__state") v = running ? "RUNNING" : "IDLE";
    inf[i].textContent = (v===undefined||v===null?"-":v) + inf[i].dataset.suffix;
  }
  var rows = document.querySelectorAll('[data-show]');
  for(var i=0;i<rows.length;i++){
    var cond = rows[i].dataset.show;
    var showIt = false;
    if(cond==="__custom")   showIt = (getKey(cfg,"Input.game")==7);
    else if(cond==="__captureCard") showIt = (getKey(cfg,"Input.capture")==2);
    else showIt = !!getKey(cfg, cond);
    rows[i].style.display = showIt ? "" : "none";
  }
  var st = getKey(cfg,"Internal.appState");
  running = (st===3);
  var pill = document.getElementById("sysPill");
  if(pill){
    pill.classList.toggle("live", running);
    document.getElementById("sysText").textContent = running ? "ACTIVE" : "IDLE";
  }
  document.body.classList.toggle("loginMode", (st===0));
  var go = document.querySelector('button.go[data-action=start]');
  if(go){
    go.disabled = false;
    go.textContent = running ? "STOP" : "START";
    go.classList.toggle("stop", running);
  }
}

function post(extra){
  var body = {};
  for(var k in cfg){
    if(k==="Lists" || k.indexOf("__")===0) continue;
    body[k] = cfg[k];
  }
  for(var e in (extra||{})) body[e] = extra[e];
  fetch("/config", {method:"POST", headers:{"Content-Type":"application/json"}, body: JSON.stringify(body)})
    .then(function(r){ if(r.ok) return r.json(); throw 0; })
    .then(function(j){ cfg=j; restoreLists(); applyValues(); })
    .catch(function(){});
}
function postReset(){
  fetch("/config", {method:"POST", headers:{"Content-Type":"application/json"}, body:'{"reset":true}'})
    .then(function(r){ if(r.ok) return r.json(); throw 0; })
    .then(function(j){ cfg=j; restoreLists(); applyValues(); })
    .catch(function(){});
}
function postStart(){
  fetch("/action", {method:"POST", headers:{"Content-Type":"application/json"}, body:'{"start":true}'})
    .then(function(r){ if(r.ok) return r.json(); throw 0; })
    .then(function(){ setTimeout(poll, 600); })
    .catch(function(){});
}
function postExit(){
  document.body.classList.add("closedMode");
  if(pollTimer) clearInterval(pollTimer);
  fetch("/action", {method:"POST", headers:{"Content-Type":"application/json"}, body:'{"stop":true}'})
    .then(function(r){ if(r.ok) return r.json(); throw 0; })
    .catch(function(){});
}

// --- Login gate ------------------------------------------------------------
function submitLogin(){
  var key = document.getElementById("loginKey").value.trim();
  if(!key) return;
  var btn = document.getElementById("loginBtn");
  var msg = document.getElementById("loginMsg");
  btn.disabled = true;
  btn.textContent = "CHECKING...";
  msg.textContent = ""; msg.style.color = "#F87171";
  fetch("/action", {method:"POST", headers:{"Content-Type":"application/json"},
    body: JSON.stringify({license:key, remember: document.getElementById("loginRemember").checked})})
    .then(function(r){ if(r.ok) return r.json(); throw 0; })
    .then(function(j){
      if(j.ok){
        msg.style.color = "var(--accent2)";
        msg.textContent = "License valid";
        setTimeout(poll, 600);
      } else {
        msg.textContent = "Invalid license key";
        btn.disabled = false; btn.textContent = "LOGIN";
      }
    })
    .catch(function(){
      msg.textContent = "Connection error";
      btn.disabled = false; btn.textContent = "LOGIN";
    });
}
document.getElementById("loginBtn").addEventListener("click", submitLogin);
document.getElementById("loginKey").addEventListener("keydown", function(e){
  if(e.key === "Enter") submitLogin();
});

var capturing = null;
// Guards against the click/auxclick that the browser fires right after a
// mouse-button capture (e.g. binding X4 also produces an auxclick on the
// same button, which used to instantly re-open capture = "never works").
var lastFinish = 0;
// Active finish callback - lets the poll() apply a native capture result.
var captureFinish = null;

function nativeStart(){
  fetch("/action", {method:"POST", headers:{"Content-Type":"application/json"}, body:'{"capture":true}'}).catch(function(){});
}
function nativeStop(){
  fetch("/action", {method:"POST", headers:{"Content-Type":"application/json"}, body:'{"capture":false}'}).catch(function(){});
}
function nativeClear(){
  fetch("/action", {method:"POST", headers:{"Content-Type":"application/json"}, body:'{"captureClear":true}'}).catch(function(){});
}

function startCapture(btn){
  var key = btn.dataset.key;
  var finish = function(vk){
    if(vk===27){
      // ESC clears the bind
      var patch = {};
      setKey(patch, key, 0);
      deepMerge(cfg, patch);
      markDirty(key);
      post();
    } else if(vk!==null && vk!==undefined){
      var patch = {};
      setKey(patch, key, vk);
      deepMerge(cfg, patch);
      markDirty(key);
      post();
    }
    btn.classList.remove("listening");
    btn.textContent = vkName(getKey(cfg, key));
    window.removeEventListener("keydown", onKey, true);
    window.removeEventListener("mousedown", onMouse, true);
    window.removeEventListener("mouseup", onMouseUp, true);
    capturing = null;
    captureFinish = null;
    lastFinish = Date.now();
    nativeStop();
  };
  var onKey = function(ev){
    ev.preventDefault(); ev.stopPropagation();
    finish(ev.keyCode||null);
  };
  var onMouse = function(ev){
    // buttons 3/4 = X1/X2 (browser back/forward); never let them navigate
    if(ev.button>=3){ ev.preventDefault(); ev.stopImmediatePropagation(); }
    var m=[1,4,2,5,6][ev.button];
    finish(m!==undefined?m:null);
  };
  var onMouseUp = function(ev){ if(ev.button>=3) ev.preventDefault(); };
  if(capturing) finish(null);
  capturing = btn;
  captureFinish = finish;
  btn.classList.add("listening");
  btn.textContent = "LISTENING...";
  window.addEventListener("keydown", onKey, true);
  window.addEventListener("mousedown", onMouse, true);
  window.addEventListener("mouseup", onMouseUp, true);
  // native side-button listening: browsers hide X1/X2 from web pages
  nativeStart();
}

// Cached dynamic lists: the /config payload intentionally returns empty
// windows/captureDevices arrays (they are heavy to enumerate), so keep the
// last fetched copies here and restore them after every config update.
var cachedLists = { windows: [], captureDevices: [] };

function restoreLists(){
  if(!cfg.Lists) cfg.Lists = {};
  if((!cfg.Lists.windows || cfg.Lists.windows.length===0) && cachedLists.windows.length)
    cfg.Lists.windows = cachedLists.windows;
  if((!cfg.Lists.captureDevices || cfg.Lists.captureDevices.length===0) && cachedLists.captureDevices.length)
    cfg.Lists.captureDevices = cachedLists.captureDevices;
}

function fetchWindows(){
  fetch("/windows", {cache:"no-store"})
    .then(function(r){ if(r.ok) return r.json(); throw 0; })
    .then(function(t){ cachedLists.windows = t; if(cfg.Lists) cfg.Lists.windows = t; populateSelects(); })
    .catch(function(){});
}
function fetchDevices(){
  fetch("/capture-devices", {cache:"no-store"})
    .then(function(r){ if(r.ok) return r.json(); throw 0; })
    .then(function(t){ cachedLists.captureDevices = t; if(cfg.Lists) cfg.Lists.captureDevices = t; populateSelects(); })
    .catch(function(){});
}

function poll(){
  fetch("/config", {cache:"no-store"})
    .then(function(r){ if(r.ok) return r.json(); throw 0; })
    .then(function(j){
      cfg=j;
      restoreLists();
      applyValues();
      // Native capture result (side buttons can only be seen by the app)
      var cv = getKey(cfg, "Internal.captureVk");
      if(cv!==null && cv!==undefined && cv>=0){
        if(captureFinish) captureFinish(cv);
        nativeClear();
      }
      if(currentTab==="setup"){
        if(!cachedLists.windows.length) fetchWindows();
        if(!cachedLists.captureDevices.length) fetchDevices();
      }
    })
    .catch(function(){});
}

function populateSelects(){
  var ss = document.querySelectorAll('select[data-key]');
  for(var i=0;i<ss.length;i++){
    var ctl = findCtl(ss[i].dataset.key);
    if(!ctl || !ctl.list) continue;
    var items = listOf(ctl.list);
    var opts = "";
    for(var j=0;j<items.length;j++) opts += '<option value="'+j+'">'+items[j]+'</option>';
    ss[i].innerHTML = opts;
  }
  applyValues();
}

function findCtl(key){
  for(var t in SCHEMA){
    for(var i=0;i<SCHEMA[t].cards.length;i++){
      for(var j=0;j<SCHEMA[t].cards[i].controls.length;j++){
        var c = SCHEMA[t].cards[i].controls[j];
        if(c.key===key) return c;
      }
    }
  }
  return {};
}

// Lightweight patcher for live controls: sends only the changed keys and
// coalesces rapid changes (max one request in flight, latest state always
// wins) so slider drags stay smooth without flooding the server.
var patchPosting = false, patchQueued = null;
function postPatch(patch){
  patchQueued = patchQueued ? deepMerge(patchQueued, patch) : patch;
  if(patchPosting) return;
  patchPosting = true;
  var body = JSON.stringify(patchQueued);
  patchQueued = null;
  fetch("/config", {method:"POST", headers:{"Content-Type":"application/json"}, body: body})
    .then(function(r){ if(r.ok) return r.json(); throw 0; })
    .then(function(j){ cfg=j; })
    .catch(function(){})
    .finally(function(){
      patchPosting = false;
      if(patchQueued) postPatch(null);
    });
}
document.addEventListener("input", function(e){
  if(e.target.matches("input[type=range]")){
    var el = e.target;
    updateReadout(el.dataset.key);
    paintSlider(el);
    var scale = parseFloat(el.dataset.scale||1);
    var patch = {};
    setKey(patch, el.dataset.key, parseFloat(el.value)/scale);
    deepMerge(cfg, patch);
    markDirty(el.dataset.key);
    postPatch(patch);
  }
});
// Mouse wheel on a slider: scroll up/down to nudge the value by one step
// (Ctrl+wheel = 10x for coarse changes). Stops the page from scrolling.
document.addEventListener("wheel", function(e){
  var el = e.target.closest ? e.target.closest("input[type=range]") : null;
  if(!el) return;
  e.preventDefault();
  var step = parseFloat(el.step) || 1;
  var min = parseFloat(el.min) || 0;
  var max = parseFloat(el.max) || 100;
  var cur = parseFloat(el.value) || min;
  var nv = cur + step * (e.deltaY < 0 ? 1 : -1) * (e.ctrlKey ? 10 : 1);
  nv = Math.min(max, Math.max(min, nv));
  nv = min + Math.round((nv - min) / step) * step;
  nv = Math.min(max, Math.max(min, nv));
  if(Math.abs(nv - cur) < step * 0.0001) return;
  el.value = nv;
  el.dispatchEvent(new Event("input", {bubbles:true}));
}, {passive:false});
document.addEventListener("change", function(e){
  var el = e.target;
  if(el.matches("input[type=range]")){
    var scale = parseFloat(el.dataset.scale||1);
    var patch = {};
    setKey(patch, el.dataset.key, parseFloat(el.value)/scale);
    deepMerge(cfg, patch);
    markDirty(el.dataset.key);
    post();
    updateReadout(el.dataset.key);
  }
  else if(el.matches("input[type=checkbox]")){
    if(!el.dataset.key) return;
    var patch = {};
    setKey(patch, el.dataset.key, el.checked);
    deepMerge(cfg, patch);
    markDirty(el.dataset.key);
    post();
    applyValues();
  }
  else if(el.matches("select")){
    var patch = {};
      var titles = listOf("windows");
      var title = titles[parseInt(el.value,10)] || "";
      deepMerge(cfg, patch);
    } else {
      setKey(patch, el.dataset.key, parseInt(el.value,10));
      deepMerge(cfg, patch);
    }
    markDirty(el.dataset.key);
    post();
    applyValues();
  }
  else if(el.matches("input[type=color]")){
    var patch = {};
    setKey(patch, el.dataset.key, 0xFF000000 | parseInt(el.value.substring(1),16));
    deepMerge(cfg, patch);
    markDirty(el.dataset.key);
    post();
  }
  else if(el.matches("input[type=text]")){
    var patch = {};
    setKey(patch, el.dataset.key, el.value);
    deepMerge(cfg, patch);
    markDirty(el.dataset.key);
    post();
  }
});
document.addEventListener("click", function(e){
  var b = e.target.closest("button.keybind");
  if(b){ if(Date.now()-lastFinish>500) startCapture(b); return; }
  var s = e.target.closest("button.go[data-action=start]"); if(s){ if(running) postExit(); else postStart(); return; }
  var x = e.target.closest("button[data-action=exit]"); if(x){ postExit(); return; }
  var d = e.target.closest("button[data-action=reset]"); if(d){ postReset(); }
});
// ---------------------------------------------------------------------------
// Mouse side buttons (X1/X2, VK 5/6): browsers treat them as back/forward.
// preventDefault stops the navigation, but the FIRST press on a keybind
// button happens before any capture listener exists - so handle it right
// here at the mousedown itself: clicking a keybind button WITH a side
// button binds that side button instantly (one click = done).
// ---------------------------------------------------------------------------
document.addEventListener("mousedown", function(e){
  if(e.button===3 || e.button===4){
    e.preventDefault();
    e.stopImmediatePropagation();
    lastFinish = Date.now();
    var kb = e.target.closest("button.keybind");
    if(kb && kb.dataset.key){
      var patch = {};
      setKey(patch, kb.dataset.key, (e.button===3)?5:6);
      deepMerge(cfg, patch);
      markDirty(kb.dataset.key);
      post();
      kb.textContent = vkName(getKey(cfg, kb.dataset.key));
    }
  }
}, true);
// Last line of defense: cancel any browser back/forward navigation that a
// side button triggers before we could preventDefault it (popstate fires
// when the browser tries to leave the page; pushState cancels the leave).
history.pushState(null, "", location.href);
window.addEventListener("popstate", function(){ history.pushState(null, "", location.href); });
// Auxclick = non-primary buttons; only kill the context menu here, binding
// is handled in the mousedown handler above.
document.addEventListener("auxclick", function(e){
  if(e.button>=2) e.preventDefault();
});

var ICONS = {
  setup: '<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="3"/><path d="M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 0 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 0 1-2.83-2.83l.06-.06a1.65 1.65 0 0 0 .33-1.82 1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 0 1 2.83-2.83l.06.06a1.65 1.65 0 0 0 1.82.33H9a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 0 1 2.83 2.83l-.06.06a1.65 1.65 0 0 0-.33 1.82V9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1z"/></svg>',
  aim: '<svg viewBox="0 0 24 24"><circle cx="12" cy="12" r="7"/><path d="M12 2v4M12 18v4M2 12h4M18 12h4"/></svg>',
  ai: '<svg viewBox="0 0 24 24"><rect x="6" y="6" width="12" height="12" rx="2"/><rect x="10" y="10" width="4" height="4"/><path d="M9 2v2M15 2v2M9 20v2M15 20v2M2 9h2M2 15h2M20 9h2M20 15h2"/></svg>',
  binds: '<svg viewBox="0 0 24 24"><rect x="2" y="6" width="20" height="12" rx="2"/><path d="M6 10h.01M10 10h.01M14 10h.01M18 10h.01M6 14h.01M18 14h.01M10 14h4"/></svg>',
  info: '<svg viewBox="0 0 24 24"><path d="M22 12h-4l-3 9L9 3l-3 9H2"/></svg>'
};
var nav = document.getElementById("nav");
nav.addEventListener("mouseleave", function(){
  document.querySelectorAll(".nav.open").forEach(function(x){ x.classList.remove("open"); });
});
for(var t in SCHEMA){
  (function(key){
    var btn = document.createElement("button");
    btn.className = "nav" + (key===currentTab ? " active" : "");
    btn.dataset.tab = key;
    btn.innerHTML = '<span class="nav-ic">'+(ICONS[key]||"")+'</span><span class="nav-lb">'+(SCHEMA[key].nav||SCHEMA[key].title)+'</span>';
    btn.addEventListener("click", function(){
      if(key !== currentTab){
        currentTab = key;
        document.querySelectorAll(".nav").forEach(function(x){ x.classList.remove("active", "open"); });
        btn.classList.add("active", "open");
        render();
      } else {
        btn.classList.toggle("open");
      }
    });
    nav.appendChild(btn);
  })(t);
}

// Background particle network (molecules that connect near the cursor)
var pn = [];

render();
var pollTimer = setInterval(poll, 500);
poll();

function tickClock(){
  var c = document.getElementById("sysClock");
  if(!c) return;
  var d = new Date();
  c.textContent = [d.getHours(), d.getMinutes(), d.getSeconds()]
    .map(function(n){ return String(n).padStart(2,"0"); }).join(":");
}
tickClock();
setInterval(tickClock, 1000);

var LOGO_B64 = "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";
document.getElementById("brandLogo").src = "data:image/png;base64," + LOGO_B64;

document.getElementById("discordBtn").addEventListener("click", function(){
  fetch("/action", {method:"POST", headers:{"Content-Type":"application/json"}, body:'{"discord":true}'}).catch(function(){});
});
</script>
</body>
</html>
)WEB";
