#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

#include "car.h"

// =================== GAME-STYLE DASHBOARD HTML ===================
const char MAIN_PAGE[] = R"EOF(
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

    /* Fullscreen "video" background (placeholder) */
    .video-bg {
      position: fixed;
      inset: 0;
      background: radial-gradient(circle at center, #555 0, #222 40%, #000 100%);
      background-size: cover;
      filter: brightness(0.6);
      z-index: 0;
    }

    /* HUD overlay */
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

    /* Drift toggle button (top-left) */
    .mode-toggle {
      pointer-events: auto;
      padding: 8px 14px;
      border-radius: 999px;
      background: rgba(0, 0, 0, 0.6);
      border: 2px solid #888;
      font-size: 12px;
      letter-spacing: 1px;
      text-transform: uppercase;
      cursor: pointer;
      box-shadow: 0 0 0 rgba(0,0,0,0);
      transition: 0.12s ease-out;
    }

    .mode-toggle span.label {
      opacity: 0.9;
    }

    .mode-toggle span.state {
      font-weight: bold;
      margin-left: 4px;
      color: #f44;
    }

    .mode-toggle.active {
      border-color: #0ff;
      box-shadow: 0 0 16px rgba(0,255,255,0.8);
      background: radial-gradient(circle at top left, #066, #000);
    }

    .mode-toggle.active span.state {
      color: #0ff;
    }

    .speed-wrap {
      pointer-events: auto;
      margin: 0 auto;
      padding: 8px 16px 10px 16px;
      background: rgba(0, 0, 0, 0.6);
      border: 2px solid #0f0;
      border-radius: 12px;
      min-width: 220px;
      text-align: center;
    }

    .speed-display {
      font-size: 20px;
      letter-spacing: 2px;
      margin-bottom: 6px;
    }

    .gear-display {
      font-size: 14px;
      color: #0f0;
      margin-bottom: 6px;
    }

    .speed-slider input[type=range] {
      width: 100%;
    }

    .rpm-bar {
      margin-top: 6px;
      height: 6px;
      background: rgba(255, 255, 255, 0.1);
      border-radius: 4px;
      overflow: hidden;
    }

    .rpm-fill {
      height: 100%;
      width: 0%;
      background: linear-gradient(90deg, #0f0, #ff0, #f00);
      transition: width 0.08s linear;
    }

    .status-panel {
      pointer-events: auto;
      padding: 10px 16px;
      background: rgba(0, 0, 0, 0.7);
      border-radius: 10px;
      min-width: 220px;
      max-width: 280px;
      font-size: 12px;
      text-align: left;
    }

    .status-title {
      color: #0f0;
      font-weight: bold;
      margin-bottom: 4px;
      text-transform: uppercase;
      letter-spacing: 1px;
      font-size: 11px;
    }

    .status-line {
      margin: 2px 0;
    }

    .hud-bottom {
      display: flex;
      justify-content: space-between;
      padding: 18px 24px;
      align-items: flex-end;
    }

    .steering, .pedals {
      pointer-events: auto;
      display: flex;
      gap: 16px;
      align-items: center;
    }

    .steer-btn {
      width: 64px;
      height: 64px;
      border-radius: 50%;
      border: 2px solid rgba(255,255,255,0.4);
      background: rgba(0,0,0,0.7);
      display: flex;
      align-items: center;
      justify-content: center;
      font-size: 28px;
      color: #eee;
      cursor: pointer;
      transition: 0.1s;
    }

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
    }

    .pedal::before {
      content: "";
      position: absolute;
      inset: 10px 16px;
      background: repeating-linear-gradient(
        to bottom,
        rgba(0,0,0,0.8) 0,
        rgba(0,0,0,0.8) 4px,
        rgba(80,80,80,0.8) 4px,
        rgba(80,80,80,0.8) 8px
      );
      border-radius: 8px;
      opacity: 0.6;
    }

    .pedal.active {
      transform: translateY(-4px);
      border-color: #0f0;
      color: #0f0;
      box-shadow: 0 10px 20px rgba(0,255,0,0.7);
    }

    .gas { background: linear-gradient(145deg, #666, #272727); border-color: #8f8; }
    .brake { background: linear-gradient(145deg, #703131, #2b0505); border-color: #f88; }

    .control-label {
      font-size: 11px;
      margin-top: 4px;
      color: #aaa;
      text-align: center;
    }
  </style>
</head>
<body>
  <div class="video-bg"></div>

  <div class="hud">
    <div class="hud-top">
      <!-- Drift toggle on the left -->
      <div id="driftToggle" class="mode-toggle">
        <span class="label">DRIFT</span>
        <span class="state">OFF</span>
      </div>

      <!-- Speed + gear + slider + rpm in the center -->
      <div class="speed-wrap">
        <div id="speedDisplay" class="speed-display">SPEED: 2600</div>
        <div id="gearDisplay" class="gear-display">GEAR: N</div>
        <div class="speed-slider">
          <input id="speedSlider" type="range" min="800" max="4095" value="2600" />
        </div>
        <div class="rpm-bar">
          <div id="rpmFill" class="rpm-fill"></div>
        </div>
      </div>

      <!-- Status panel on the right -->
      <div class="status-panel">
        <div class="status-title">STATUS</div>
        <div id="statusLine" class="status-line">Idle</div>
        <div id="lastCmdLine" class="status-line">Last cmd: None</div>
        <div class="status-line">IP: 192.168.4.1</div>
        <div class="status-line">Control: WASD or on-screen controls</div>
      </div>
    </div>

    <div class="hud-bottom">
      <!-- Steering bottom-left -->
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

      <!-- Pedals bottom-right -->
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
    // Base magnitudes
    let currentSpeed = 2600;
    const STEER = 2000;        // normal steering
    const DRIFT_STEER = 3800;  // stronger steering in drift mode
    const MAX = 4095;

    // State flags
    let driftEnabled = false;

    let keyW=false,keyA=false,keyS=false,keyD=false;
    let btnLeft=false,btnRight=false,btnAccel=false,btnBrake=false;

    const speedDisplay=document.getElementById("speedDisplay");
    const gearDisplay=document.getElementById("gearDisplay");
    const statusLine=document.getElementById("statusLine");
    const lastCmdLine=document.getElementById("lastCmdLine");
    const rpmFill=document.getElementById("rpmFill");
    const speedSlider=document.getElementById("speedSlider");
    const driftToggle=document.getElementById("driftToggle");
    const driftStateSpan = driftToggle.querySelector(".state");

    function setStatus(s){statusLine.textContent=s;}
    function setLast(s){lastCmdLine.textContent="Last cmd: "+s;}

    // Speed slider changes throttle magnitude
    speedSlider.oninput=e=>{
      currentSpeed=parseInt(e.target.value);
      speedDisplay.textContent="SPEED: "+currentSpeed;
      updateMotion();
    };

    // Toggle drift ON/OFF
    driftToggle.onclick = ()=>{
      driftEnabled = !driftEnabled;
      driftToggle.classList.toggle("active", driftEnabled);
      driftStateSpan.textContent = driftEnabled ? "ON" : "OFF";
      setStatus(driftEnabled ? "Drift mode enabled" : "Drift mode disabled");
      setLast("Drift "+(driftEnabled?"ON":"OFF"));
      // Recompute motion with new steering strength
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
      updateRpm(throttle);

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
      gearDisplay.textContent = t>0 ? "GEAR: D" : t<0 ? "GEAR: R" : "GEAR: N";
    }

    function updateRpm(t){
      let pct=Math.min(100,Math.abs(t)/MAX*100);
      rpmFill.style.width=pct+"%";
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

    document.onkeydown=e=>{
      let k=e.key.toLowerCase();
      if("wasd".includes(k)){e.preventDefault();}
      if(k==="w")keyW=true;
      if(k==="a")keyA=true;
      if(k==="s")keyS=true;
      if(k==="d")keyD=true;
      updateMotion();
    };

    document.onkeyup=e=>{
      let k=e.key.toLowerCase();
      if("wasd".includes(k)){e.preventDefault();}
      if(k==="w")keyW=false;
      if(k==="a")keyA=false;
      if(k==="s")keyS=false;
      if(k==="d")keyD=false;
      updateMotion();
    };
  </script>
</body>
</html>
)EOF";

// ----------------- ESP32 SERVER + CAR CONTROL -----------------
WebServer server(80);
const int BUZZER_PIN = 2;

int clampPwm(int v){
  if(v<-4095)return-4095;
  if(v>4095)return 4095;
  return v;
}

void handleRoot(){
  server.send(200,"text/html",MAIN_PAGE);
}

// /drive?left=..&right=..
void handleDrive(){
  int left=0,right=0;
  if(server.hasArg("left"))  left  = server.arg("left").toInt();
  if(server.hasArg("right")) right = server.arg("right").toInt();

  left  = clampPwm(left);
  right = clampPwm(right);

  // YOUR BUILD HAS SIDES REVERSED — SWAP THEM
  Car::move(right, right, left, left);

  server.send(200,"text/plain","OK");
}

void handleStop(){
  Car::stop();
  server.send(200,"text/plain","Stopped");
}

void handleNotFound(){
  server.send(404,"text/plain","Invalid endpoint");
}

void setup(){
  Serial.begin(115200);

  pinMode(BUZZER_PIN,OUTPUT);
  digitalWrite(BUZZER_PIN,LOW);

  Car::begin();
  Car::stop();

  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP32-RC-Car","drivecar123");

  server.on("/",handleRoot);
  server.on("/drive",handleDrive);
  server.on("/stop",handleStop);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial.println("AP: ESP32-RC-Car  |  Pass: drivecar123");
  Serial.println("Open http://192.168.4.1/ in your browser.");
}

void loop(){
  server.handleClient();
}
