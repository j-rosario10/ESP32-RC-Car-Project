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
      transition: 0.2s ease-out;
    }

    /* Drift visual tint */
    body.drift-on .video-bg {
      filter: brightness(0.7) hue-rotate(-20deg) saturate(1.2);
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

    .mode-toggle span.label {
      opacity: 0.9;
    }

    .mode-toggle span.state {
      font-weight: bold;
      margin-left: 2px;
      color: #f44;
    }

    .mode-toggle.active {
      border-color: #0ff;
      box-shadow: 0 0 16px rgba(0,255,255,0.6);
      background: radial-gradient(circle at top left, rgba(0,120,120,0.9), rgba(0,0,0,0.85));
    }

    .mode-toggle.active span.state {
      color: #0ff;
    }

    /* Center speed / gear cluster */
    .speed-wrap {
      pointer-events: auto;
      margin: 0 auto;
      padding: 10px 18px 12px 18px;
      background: radial-gradient(circle at top, rgba(0,255,128,0.12), rgba(0,0,0,0.9));
      border-radius: 18px;
      min-width: 260px;
      text-align: center;
      box-shadow: 0 0 18px rgba(0, 255, 128, 0.25);
      border: 1px solid rgba(0,255,128,0.6);
      backdrop-filter: blur(4px);
    }

    body.drift-on .speed-wrap {
      border-color: #0ff;
      box-shadow: 0 0 22px rgba(0,255,255,0.35);
    }

    .speed-display {
      font-size: 22px;
      letter-spacing: 3px;
      margin-bottom: 4px;
    }

    .gear-display {
      font-size: 14px;
      color: #0f0;
      margin-bottom: 6px;
    }

    .speed-slider input[type=range],
    #speedSlider {
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

    body.drift-on .rpm-fill {
      background: linear-gradient(90deg, #0ff, #ff0, #f0f);
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

    .status-line {
      margin: 2px 0;
      opacity: 0.9;
    }

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

    /* Steering buttons */
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

    .steer-btn:hover {
      filter: brightness(1.15);
    }

    .steer-btn.active {
      border-color: #0f0;
      color: #0f0;
      transform: translateY(-2px);
      box-shadow: 0 0 15px rgba(0,255,0,0.8);
    }

    /* Pedals */
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

    .pedal:hover {
      filter: brightness(1.15);
    }

    .pedal.active {
      transform: perspective(400px) rotateX(4deg) translateY(-2px);
      border-color: #0f0;
      color: #0f0;
      box-shadow: 0 10px 20px rgba(0,255,0,0.7);
    }

    .gas {
      background: linear-gradient(145deg, #666, #272727);
      border-color: #8f8;
    }

    .brake {
      background: linear-gradient(145deg, #703131, #2b0505);
      border-color: #f88;
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
    let currentSpeed = 2600;
    const STEER = 2000;
    const DRIFT_STEER = 3800;
    const MAX = 4095;

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

    speedSlider.oninput=e=>{
      currentSpeed=parseInt(e.target.value);
      speedDisplay.textContent="SPEED: "+currentSpeed;
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
