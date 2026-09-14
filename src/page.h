#pragma once
#include <Arduino.h>

// This page doesn't drive the car. It only reports which keys you're holding
// and displays what the car sends back. All the driving decisions happen on
// the ESP32, because Wi-Fi delay is unpredictable and anything automatic
// needs steady timing.

static const char PAGE_HTML[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html>
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>M2 RC</title>
<style>
  body { margin:0; background:#0a0a0c; color:#eee;
         font-family:system-ui,sans-serif; overflow:hidden;
         -webkit-user-select:none; user-select:none; }
  .wrap { padding:16px; max-width:640px; margin:0 auto; }
  h1 { font-size:14px; letter-spacing:2px; color:#888; font-weight:600; }

  .bar { height:14px; background:#1a1a1e; border-radius:7px;
         overflow:hidden; margin:4px 0 12px; }
  .fill { height:100%; width:0%; background:#e33; transition:width 60ms linear; }
  .fill.mid { background:#3af; }

  .row { display:flex; gap:10px; margin-top:14px; }
  .pedal { flex:1; padding:22px 0; text-align:center; border-radius:10px;
           background:#17171b; border:1px solid #333; font-weight:700;
           letter-spacing:1px; touch-action:none; }
  .pedal.active { background:#3a1010; border-color:#e55; }

  table { width:100%; font-size:12px; margin-top:16px;
          border-collapse:collapse; font-variant-numeric:tabular-nums; }
  td { padding:3px 0; color:#999; }
  td.v { text-align:right; color:#eee; }
  .warn { color:#e55; font-weight:700; }
  .label { font-size:11px; color:#777; letter-spacing:1px; }
</style>
</head>
<body>
<div class="wrap">
  <h1>BMW M2 RC</h1>

  <div class="label">THROTTLE</div>
  <div class="bar"><div class="fill" id="thr"></div></div>

  <div class="label">BRAKE</div>
  <div class="bar"><div class="fill" id="brk"></div></div>

  <div class="label">STEERING</div>
  <div class="bar"><div class="fill mid" id="str"></div></div>

  <div class="row">
    <div class="pedal" id="p-a">LEFT</div>
    <div class="pedal" id="p-w">GO</div>
    <div class="pedal" id="p-s">BRAKE</div>
    <div class="pedal" id="p-d">RIGHT</div>
  </div>

  <div class="row">
    <div class="pedal" id="p-drift">DRIFT</div>
  </div>

  <table>
    <tr><td>yaw target</td><td class="v"><span id="yt">0</span> deg/s</td></tr>
    <tr><td>yaw actual</td><td class="v"><span id="ya">0</span> deg/s</td></tr>
    <tr><td>yaw error</td><td class="v"><span id="ye">0</span> deg/s</td></tr>
    <tr><td>pack</td><td class="v"><span id="v">0</span> V</td></tr>
    <tr><td>current</td><td class="v"><span id="i">0</span> A</td></tr>
    <tr><td>power</td><td class="v"><span id="p">0</span> W</td></tr>
    <tr><td>energy out</td><td class="v"><span id="jo">0</span> J</td></tr>
    <tr><td>energy back</td><td class="v"><span id="ji">0</span> J</td></tr>
    <tr><td>recovered</td><td class="v"><span id="pct">0</span> %</td></tr>
    <tr><td>loop overruns</td><td class="v"><span id="ov">0</span></td></tr>
    <tr><td>status</td><td class="v"><span id="st">-</span></td></tr>
  </table>
</div>

<script>
// The only thing this page keeps track of. Everything else comes from the car.
let keys = { w:false, a:false, s:false, d:false };
let drift = false;
let dirty = true;

function setKey(k, down) {
  if (keys[k] === down) return;
  keys[k] = down;
  dirty = true;              // only send when something actually changes
}

document.addEventListener('keydown', e => {
  const k = e.key.toLowerCase();
  if (k in keys) { setKey(k, true); e.preventDefault(); }
  if (k === 'shift') { drift = !drift; dirty = true; }
});

document.addEventListener('keyup', e => {
  const k = e.key.toLowerCase();
  if (k in keys) { setKey(k, false); e.preventDefault(); }
});

// Pointer events work for both touch and mouse, so this is one bit of code
// instead of two.
function bindPedal(id, key) {
  const el = document.getElementById(id);
  const down = e => { e.preventDefault(); setKey(key, true);  el.classList.add('active'); };
  const up   = e => { e.preventDefault(); setKey(key, false); el.classList.remove('active'); };
  el.addEventListener('pointerdown', down);
  el.addEventListener('pointerup', up);
  el.addEventListener('pointercancel', up);
  el.addEventListener('pointerleave', up);
}
bindPedal('p-w','w'); bindPedal('p-a','a');
bindPedal('p-s','s'); bindPedal('p-d','d');

document.getElementById('p-drift').addEventListener('pointerdown', e => {
  e.preventDefault();
  drift = !drift;
  dirty = true;
  document.getElementById('p-drift').classList.toggle('active', drift);
});

// If you switch tabs or click away, let go of everything. Alt-tabbing while
// holding the throttle is a very easy way to drive the car into a wall.
function releaseAll() {
  for (const k in keys) keys[k] = false;
  dirty = true;
  document.querySelectorAll('.pedal').forEach(p => p.classList.remove('active'));
}
window.addEventListener('blur', releaseAll);
document.addEventListener('visibilitychange', () => { if (document.hidden) releaseAll(); });

// Send when something changes, plus a message every 150ms even if nothing
// did. The car stops itself if it hears nothing for 300ms, so these regular
// messages are what tell it we're still here.
let lastSend = 0;
setInterval(() => {
  const now = Date.now();
  if (!dirty && now - lastSend < 150) return;
  dirty = false;
  lastSend = now;

  const q = `/input?w=${+keys.w}&a=${+keys.a}&s=${+keys.s}&d=${+keys.d}&drift=${+drift}`;
  fetch(q).catch(() => {});
}, 40);

// Ask the car for its numbers 10 times a second. Faster than that is just
// wasted - you couldn't read it anyway.
setInterval(async () => {
  try {
    const r = await fetch('/telemetry');
    const t = await r.json();

    document.getElementById('thr').style.width = Math.abs(t.throttle*100) + '%';
    document.getElementById('brk').style.width = (t.brake*100) + '%';
    document.getElementById('str').style.width = Math.abs(t.steer*100) + '%';

    document.getElementById('yt').textContent = t.yawTarget.toFixed(0);
    document.getElementById('ya').textContent = t.yawActual.toFixed(0);
    document.getElementById('ye').textContent = t.yawError.toFixed(0);
    document.getElementById('v').textContent  = t.volts.toFixed(2);
    document.getElementById('i').textContent  = t.amps.toFixed(2);
    document.getElementById('p').textContent  = t.watts.toFixed(1);
    document.getElementById('jo').textContent = t.joulesOut.toFixed(1);
    document.getElementById('ji').textContent = t.joulesIn.toFixed(1);

    const pct = t.joulesOut > 0 ? (t.joulesIn / t.joulesOut * 100) : 0;
    document.getElementById('pct').textContent = pct.toFixed(1);

    document.getElementById('ov').textContent = t.overruns;

    const st = document.getElementById('st');
    st.textContent = t.failsafe ? 'FAILSAFE' : 'ok';
    st.className = t.failsafe ? 'warn' : '';
  } catch(e) {}
}, 100);
</script>
</body>
</html>
)HTML";
