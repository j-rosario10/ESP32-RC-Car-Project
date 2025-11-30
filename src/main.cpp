#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

#include "car.h"

// =================== GAME-STYLE DASHBOARD HTML ===================
const char MAIN_PAGE[] PROGMEM = R"EOF(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8" />
  <title>ESP32 RC Car - Game HUD</title>
  <style>
    html, body {
      margin: 0;
      padding: 0;
      height: 100%;
      overflow: hidden;
      font-family: Arial, sans-serif;
      background: #000;
      color: #eee;
    }

    .video-bg {
      position: fixed;
      inset: 0;
      background: radial-gradient(circle at center, #555 0, #222 40%, #000 100%);
      background-size: cover;
      filter: brightness(0.6);
      z-index: 0;
      transition: 0.2s ease-out;
    }

    body.drift-on .video-bg {
      filter: brightness(0.7) hue-rotate(-20deg) saturate(1.2);
    }

    .hud {
      position: fixed;
      inset: 0;
      z-index: 1;
      display: flex;
      flex-direction: column;
      justify-content: space-between;
      pointer-events: none;
    }

    .hud-top {
      display: flex;
      justify-content: space-between;
      padding: 12px 20px;
      align-items: flex-start;
    }

    .mode-toggle {
      pointer-events: auto;
      padding: 8px 14px;
      border-radius: 999px;
      background: rgba(0, 0, 0, 0.6);
      border: 1px solid rgba(150, 150, 150, 0.7);
      font-size: 12px;
      letter-spacing: 1px;
      text-transform: uppercase;
      cursor: pointer;
      box-shadow: 0 0 0 rgba(0,0,0,0);
      transition: 0.12s ease-out;
      display: inline-flex;
      align-items: center;
      gap: 4px;
    }

    .mode-toggle span.label { opacity: 0.9; }
    .mode-toggle span.state { font-weight: bold; color: #f44; }

    .mode-toggle.active {
      border-color: #0ff;
      box-shadow: 0 0 16px rgba(0,255,255,0.6);
      background: radial-gradient(circle at top left, rgba(0,120,120,0.9), rgba(0,0,0,0.85));
    }

    .mode-toggle.active span.state { color: #0ff; }

    .speed-wrap {
      pointer-events: auto;
      margin: 0 auto;
      text-align: center;
    }

    /* Gauge container */
    .gauge {
      position: relative;
      width: 260px;
      height: 180px; /* more vertical space so 0/10 don't clip */
      margin: 0 auto 4px auto;
    }

    .gauge-svg {
      width: 100%;
      height: 100%;
    }

    .gauge-arc-bg {
      fill: none;
      stroke: #ffffff;      /* white base arc */
      stroke-width: 6;
      stroke-linecap: round;
    }

    .gauge-arc-fill {
      fill: none;
      stroke: url(#gaugeGradient);  /* colored overlay arc */
      stroke-width: 6;
      stroke-linecap: round;
      stroke-dasharray: 1;     /* set in JS */
      stroke-dashoffset: 1;
      transition: stroke-dashoffset 0.08s linear;
    }

    .gauge-needle {
      stroke: #ff4444;   /* red needle */
      stroke-width: 3;
      stroke-linecap: round;
      transform-origin: 130px 130px;
      transform: rotate(-110deg);
      transition: transform 0.08s linear;
    }

    .gauge-center {
      position: absolute;
      left: 50%;
      top: 62%;
      transform: translate(-50%, -50%);
      display: flex;
      align-items: center;
      gap: 8px;
    }

    .gauge-gear {
      min-width: 26px;
      padding: 3px 6px;
      border-radius: 4px;
      background: #ffeb3b;
      color: #111;
      font-weight: bold;
      font-size: 14px;
    }

    .gauge-speed-num {
      font-size: 36px;
      font-weight: 700;
      letter-spacing: 2px;
    }

    .gauge-unit {
      position: absolute;
      left: 50%;
      top: 84%;
      transform: translateX(-50%);
      font-size: 10px;
      letter-spacing: 2px;
      opacity: 0.75;
    }

    .speed-slider {
      margin-top: 24px;
      display: inline-block;
      padding: 6px 14px 10px;
      border-radius: 999px;
      border: 1px solid rgba(255,255,255,0.35);
      background: rgba(0,0,0,0.6);
    }

    .speed-slider input[type=range],
    #speedSlider {
      width: 180px;
    }

    .status-panel {
      pointer-events: auto;
      padding: 10px 16px;
      background: radial-gradient(circle at top right, rgba(0,255,255,0.12), rgba(0,0,0,0.9));
      border-radius: 14px;
      min-width: 220px;
      max-width: 280px;
      font-size: 12px;
      text-align: left;
      border: 1px solid rgba(0,255,255,0.5);
      box-shadow: 0 0 14px rgba(0,255,255,0.18);
    }

    .status-title {
      color: #0ff;
      font-weight: 600;
      margin-bottom: 6px;
      text-transform: uppercase;
      letter-spacing: 1.5px;
      font-size: 11px;
    }

    .status-line { margin: 2px 0; opacity: 0.9; }

    .hud-bottom {
      display: flex;
      justify-content: space-between;
      padding: 14px 24px 18px 24px;
      align-items: flex-end;
    }

    .steering, .pedals {
      pointer-events: auto;
      display: flex;
      gap: 16px;
      align-items: center;
    }

    .control-label {
      font-size: 11px;
      margin-top: 4px;
      color: #aaa;
      text-align: center;
    }

    .steer-btn {
      width: 64px;
      height: 64px;
      border-radius: 50%;
      border: 2px solid rgba(255,255,255,0.35);
      background: rgba(0,0,0,0.7);
      display: flex;
      align-items: center;
      justify-content: center;
      font-size: 28px;
      color: #eee;
      cursor: pointer;
      transition: 0.1s;
      box-shadow: 0 0 0 rgba(0,0,0,0);
    }

    .steer-btn:hover { filter: brightness(1.15); }

    .steer-btn.active {
      border-color: #0f0;
      color: #0f0;
      transform: translateY(-2px);
      box-shadow: 0 0 15px rgba(0,255,0,0.8);
    }

    .pedal {
      width: 90px;
      height: 140px;
      border-radius: 12px;
      background: linear-gradient(145deg, #555, #222);
      border: 2px solid #777;
      display: flex;
      justify-content: center;
      align-items: center;
      font-size: 18px;
      color: #eee;
      font-weight: bold;
      cursor: pointer;
      transition: 0.1s;
      position: relative;
      text-shadow: 0 0 4px rgba(0,0,0,0.8);
      box-shadow: 0 6px 12px rgba(0,0,0,0.6);
      transform: perspective(400px) rotateX(12deg);
    }

    .pedal::before {
      content: "";
      position: absolute;
      inset: 10px 16px;
      background: repeating-linear-gradient(
        to bottom,
        rgba(0,0,0,0.85) 0,
        rgba(0,0,0,0.85) 4px,
        rgba(80,80,80,0.85) 4px,
        rgba(80,80,80,0.85) 8px
      );
      border-radius: 8px;
      opacity: 0.6;
    }

    .pedal:hover { filter: brightness(1.15); }

    .pedal.active {
      transform: perspective(400px) rotateX(4deg) translateY(-2px);
      border-color: #0f0;
      color: #0f0;
      box-shadow: 0 10px 20px rgba(0,255,0,0.7);
    }

    .gas { background: linear-gradient(145deg, #666, #272727); border-color: #8f8; }
    .brake { background: linear-gradient(145deg, #703131, #2b0505); border-color: #f88; }
  </style>
</head>
<body>
  <div class="video-bg"></div>

  <div class="hud">
    <div class="hud-top">
      <!-- Drift toggle -->
      <div id="driftToggle" class="mode-toggle">
        <span class="label">DRIFT</span>
        <span class="state">OFF</span>
      </div>

      <!-- Gauge + slider -->
      <div class="speed-wrap">
        <div class="gauge">
          <svg viewBox="0 0 260 180" class="gauge-svg">
            <defs>
              <linearGradient id="gaugeGradient" x1="0%" y1="0%" x2="100%" y2="0%">
                <stop offset="0%" stop-color="#0f0" />
                <stop offset="60%" stop-color="#ff0" />
                <stop offset="100%" stop-color="#f00" />
              </linearGradient>
            </defs>

            <!-- ticks + labels 0–10 (RPM-style) -->
            <g id="gaugeTicks"></g>

            <!-- arc paths set in JS so they match ticks exactly -->
            <path id="gaugeArcBg"  class="gauge-arc-bg" />
            <path id="gaugeFill"   class="gauge-arc-fill" />

            <!-- needle -->
            <line id="gaugeNeedle"
                  x1="130" y1="130"
                  x2="130" y2="50"
                  class="gauge-needle" />
          </svg>

          <div class="gauge-center">
            <div id="gearDisplay" class="gauge-gear">N</div>
            <div id="speedDisplay" class="gauge-speed-num">0</div>
          </div>
          <div class="gauge-unit">SPEED</div>
        </div>

        <div class="speed-slider">
          <input id="speedSlider" type="range" min="800" max="4095" value="2600" />
        </div>
      </div>

      <!-- Status -->
      <div class="status-panel">
        <div class="status-title">STATUS</div>
        <div id="statusLine" class="status-line">Idle</div>
        <div id="lastCmdLine" class="status-line">Last cmd: None</div>
        <div class="status-line">IP: 192.168.4.1</div>
      </div>
    </div>

    <div class="hud-bottom">
      <!-- Steering -->
      <div class="steering">
        <div class="control-group">
          <div id="btn-left" class="steer-btn">&#9664;</div>
          <div class="control-label">LEFT (A)</div>
        </div>
        <div class="control-group">
          <div id="btn-right" class="steer-btn">&#9654;</div>
          <div class="control-label">RIGHT (D)</div>
        </div>
      </div>

      <!-- Pedals -->
      <div class="pedals">
        <div class="control-group">
          <div id="btn-accel" class="pedal gas">GAS</div>
          <div class="control-label">ACCEL (W)</div>
        </div>
        <div class="control-group">
          <div id="btn-brake" class="pedal brake">BRAKE</div>
          <div class="control-label">REVERSE (S)</div>
        </div>
      </div>
    </div>
  </div>

  <script>
    // Shared angle range for ticks, needle, arc
    const START_DEG = -110;
    const END_DEG   =  110;

    let currentSpeed = 2600;
    const STEER = 2000;
    const DRIFT_STEER = 3800;
    const MAX = 4095;
    const MAX_DISPLAY_SPEED = 150; // "game" speed

    let driftEnabled = false;

    let keyW=false,keyA=false,keyS=false,keyD=false;
    let btnLeft=false,btnRight=false,btnAccel=false,btnBrake=false;

    const speedDisplay=document.getElementById("speedDisplay");
    const gearDisplay=document.getElementById("gearDisplay");
    const statusLine=document.getElementById("statusLine");
    const lastCmdLine=document.getElementById("lastCmdLine");
    const speedSlider=document.getElementById("speedSlider");
    const driftToggle=document.getElementById("driftToggle");
    const driftStateSpan = driftToggle.querySelector(".state");
    const gaugeNeedle = document.getElementById("gaugeNeedle");
    const gaugeFill = document.getElementById("gaugeFill");
    const gaugeArcBg = document.getElementById("gaugeArcBg");
    const ticksGroup = document.getElementById("gaugeTicks");

    const cx = 130, cy = 130, rArc = 80;
    let arcLength = 1;

    function setStatus(s){statusLine.textContent=s;}
    function setLast(s){lastCmdLine.textContent="Last cmd: "+s;}

    function degToRad(d){ return d * Math.PI / 180; }
    function polarToX(angleDeg, radius){ return cx + radius * Math.sin(degToRad(angleDeg)); }
    function polarToY(angleDeg, radius){ return cy - radius * Math.cos(degToRad(angleDeg)); }

    function describeArc(startDeg,endDeg,radius){
      const startX = polarToX(startDeg, radius);
      const startY = polarToY(startDeg, radius);
      const endX   = polarToX(endDeg,   radius);
      const endY   = polarToY(endDeg,   radius);
      const largeArc = (endDeg - startDeg) <= 180 ? 0 : 1;
      return `M ${startX} ${startY} A ${radius} ${radius} 0 ${largeArc} 1 ${endX} ${endY}`;
    }

    // Build ticks + labels + arc paths (0–10 rpm scale)
    (function buildGauge(){
      const SVG_NS = "http://www.w3.org/2000/svg";
      const spanDeg = END_DEG - START_DEG;

      // Single geometry for both arcs
      const d = describeArc(START_DEG, END_DEG, rArc);
      gaugeArcBg.setAttribute("d", d);
      gaugeFill.setAttribute("d", d);

      // arc length for dasharray — colored fill uses this as progress bar
      arcLength = 2 * Math.PI * rArc * (spanDeg / 360);
      gaugeFill.style.strokeDasharray = arcLength;
      gaugeFill.style.strokeDashoffset = arcLength; // idle = white only

      for(let i=0;i<=10;i++){
        const ratio = i/10;
        const angleDeg = START_DEG + spanDeg*ratio;

        const rOuter = rArc;
        const rInner = (i % 2 === 0) ? rArc - 14 : rArc - 8;

        const xOuter = polarToX(angleDeg, rOuter);
        const yOuter = polarToY(angleDeg, rOuter);
        const xInner = polarToX(angleDeg, rInner);
        const yInner = polarToY(angleDeg, rInner);

        const line = document.createElementNS(SVG_NS, "line");
        line.setAttribute("x1", xInner);
        line.setAttribute("y1", yInner);
        line.setAttribute("x2", xOuter);
        line.setAttribute("y2", yOuter);
        line.setAttribute("stroke", "rgba(255,255,255," + (i%2===0?1:0.6) + ")");
        line.setAttribute("stroke-width", (i%2===0?2:1));
        ticksGroup.appendChild(line);

        if(i % 2 === 0){
          // labels OUTSIDE the arc again, but SVG is taller now so no clipping
          const rText = rArc + 10;
          const xt = polarToX(angleDeg, rText);
          const yt = polarToY(angleDeg, rText);
          const text = document.createElementNS(SVG_NS, "text");
          text.setAttribute("x", xt);
          text.setAttribute("y", yt);
          text.setAttribute("fill", "#ffffff");
          text.setAttribute("font-size", "10");
          text.setAttribute("text-anchor", "middle");
          text.setAttribute("alignment-baseline", "middle");
          text.textContent = i.toString();
          ticksGroup.appendChild(text);
        }
      }
    })();

    function stopAllInputs(){
      keyW=keyA=keyS=keyD=false;
      btnLeft=btnRight=btnAccel=btnBrake=false;
    }

    speedSlider.oninput=e=>{
      currentSpeed=parseInt(e.target.value);
      updateMotion();
    };

    driftToggle.onclick = ()=>{
      driftEnabled = !driftEnabled;
      driftToggle.classList.toggle("active", driftEnabled);
      document.body.classList.toggle("drift-on", driftEnabled);
      driftStateSpan.textContent = driftEnabled ? "ON" : "OFF";
      setStatus(driftEnabled ? "Drift mode enabled" : "Drift mode disabled");
      setLast("Drift "+(driftEnabled?"ON":"OFF"));
      updateMotion();
    };

    function updateMotion(){
      let throttle=0,steering=0;

      if(keyW||btnAccel) throttle+=currentSpeed;
      if(keyS||btnBrake) throttle-=currentSpeed;

      const steerMag = driftEnabled ? DRIFT_STEER : STEER;

      if(keyA||btnLeft) steering-=steerMag;
      if(keyD||btnRight) steering+=steerMag;

      updateGear(throttle);
      updateGauge(throttle);
      highlight();

      if(throttle===0 && steering===0){
        fetch("/stop").catch(()=>{});
        setStatus("Stopped");
        setLast("Stop");
        return;
      }

      let left = throttle - steering;
      let right = throttle + steering;

      left=Math.max(-MAX,Math.min(MAX,left));
      right=Math.max(-MAX,Math.min(MAX,right));

      fetch(`/drive?left=${left}&right=${right}`).catch(()=>{});
      const msg=`L:${left} R:${right}` + (driftEnabled ? " [DRIFT]" : "");
      setStatus(msg);
      setLast(msg);
    }

    function highlight(){
      document.getElementById("btn-left").classList.toggle("active",keyA||btnLeft);
      document.getElementById("btn-right").classList.toggle("active",keyD||btnRight);
      document.getElementById("btn-accel").classList.toggle("active",keyW||btnAccel);
      document.getElementById("btn-brake").classList.toggle("active",keyS||btnBrake);
    }

    function updateGear(t){
      gearDisplay.textContent = t>0 ? "D" : t<0 ? "R" : "N";
    }

    // Circular gauge update:
    // - arcFill = colored "rpm" bar
    // - needle angle matches same ratio
    // - center number = SPEED
    function updateGauge(throttle){
      const t = Math.abs(throttle);
      const ratio = Math.min(1, t / MAX);

      // 0–150 game speed
      const fakeSpeed = Math.round(ratio * MAX_DISPLAY_SPEED);
      speedDisplay.textContent = fakeSpeed.toString();

      // needle angle
      const angle = START_DEG + (END_DEG - START_DEG) * ratio;
      gaugeNeedle.style.transform = `rotate(${angle}deg)`;

      // arc fill: idle = white only, accelerate = color grows along same path
      const offset = arcLength * (1 - ratio);
      gaugeFill.style.strokeDashoffset = offset;
    }

    function hold(id,ref){
      let el=document.getElementById(id);
      if(!el)return;
      el.onmousedown=e=>{e.preventDefault();window[ref]=true;updateMotion();}
      el.ontouchstart=e=>{e.preventDefault();window[ref]=true;updateMotion();}
      const stop=e=>{window[ref]=false;updateMotion();}
      window.addEventListener("mouseup",stop);
      window.addEventListener("touchend",stop);
      window.addEventListener("touchcancel",stop);
      el.onmouseleave=stop;
    }

    hold("btn-left","btnLeft");
    hold("btn-right","btnRight");
    hold("btn-accel","btnAccel");
    hold("btn-brake","btnBrake");

    document.addEventListener("keydown",e=>{
      const k=e.key.toLowerCase();
      if("wasd".includes(k)){e.preventDefault();}
      if(k==="w")keyW=true;
      if(k==="a")keyA=true;
      if(k==="s")keyS=true;
      if(k==="d")keyD=true;
      updateMotion();
    });

    document.addEventListener("keyup",e=>{
      const k=e.key.toLowerCase();
      if("wasd".includes(k)){e.preventDefault();}
      if(k==="w")keyW=false;
      if(k==="a")keyA=false;
      if(k==="s")keyS=false;
      if(k==="d")keyD=false;
      updateMotion();
    });

    // Safety: stop car if window loses focus
    window.addEventListener("blur", ()=>{
      stopAllInputs();
      updateMotion(); // sends /stop
    });

    // initial gauge render
    updateGauge(0);
  </script>
</body>
</html>
)EOF";

// ----------------- ESP32 SERVER + CAR CONTROL -----------------
WebServer server(80);
const int BUZZER_PIN = 2;

// clamp PWM to ±4095
int clampPwm(int v){
  if(v < -4095) return -4095;
  if(v >  4095) return  4095;
  return v;
}

void handleRoot(){
  server.send(200, "text/html", MAIN_PAGE);
}

// /drive?left=..&right=..
void handleDrive(){
  int left  = 0;
  int right = 0;

  if (server.hasArg("left"))  left  = server.arg("left").toInt();
  if (server.hasArg("right")) right = server.arg("right").toInt();

  left  = clampPwm(left);
  right = clampPwm(right);

  // hardware sides reversed in your wiring
  Car::move(right, right, left, left);

  server.send(200, "text/plain", "OK");
}

void handleStop(){
  Car::stop();
  server.send(200, "text/plain", "Stopped");
}

void handleNotFound(){
  server.send(404, "text/plain", "Invalid endpoint");
}

void setup(){
  Serial.begin(115200);

  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);  // keep buzzer off

  Car::begin();
  Car::stop();

  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP32-RC-Car", "drivecar123");

  server.on("/", handleRoot);
  server.on("/drive", handleDrive);
  server.on("/stop", handleStop);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.println("AP: ESP32-RC-Car  |  Pass: drivecar123");
  Serial.println("Open http://192.168.4.1/ in your browser.");
}

void loop(){
  server.handleClient();
}
