#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>  // built into ESP32 Arduino core

#include "car.h"

// =================== HTML PAGE (WASD CONTROL) ===================
const char MAIN_PAGE[] = R"EOF(
<!DOCTYPE html>
<html>
<head>
  <meta charset="utf-8" />
  <title>ESP32 RC Car - WASD Control</title>
  <style>
    body {
      font-family: Arial, sans-serif;
      background: #111;
      color: #eee;
      text-align: center;
      margin-top: 50px;
    }
    .key {
      display: inline-block;
      border: 2px solid #555;
      border-radius: 8px;
      padding: 20px 30px;
      margin: 10px;
      font-size: 24px;
      min-width: 60px;
      background: #222;
    }
    .key.active {
      border-color: #0f0;
      box-shadow: 0 0 10px #0f0;
      background: #333;
    }
    .row {
      margin: 10px 0;
    }
    .status {
      margin-top: 20px;
      font-size: 16px;
      color: #0f0;
    }
  </style>
</head>
<body>
  <h1>ESP32 RC Car - WASD Control</h1>
  <p>Click on the page, then use W / A / S / D keys to drive.</p>
  <div class="row">
    <div id="key-w" class="key">W</div>
  </div>
  <div class="row">
    <div id="key-a" class="key">A</div>
    <div id="key-s" class="key">S</div>
    <div id="key-d" class="key">D</div>
  </div>
  <div class="status" id="status">Idle</div>

  <script>
    let activeKey = null;

    function setStatus(text) {
      document.getElementById('status').textContent = text;
    }

    function setKeyActive(key, isActive) {
      const el = document.getElementById('key-' + key);
      if (!el) return;
      if (isActive) el.classList.add('active');
      else el.classList.remove('active');
    }

    function sendCommand(path, label) {
      fetch(path).catch(err => console.log(err));
      setStatus(label);
    }

    document.addEventListener('keydown', function(e) {
      const k = e.key.toLowerCase();
      if (!['w','a','s','d'].includes(k)) return;
      e.preventDefault(); // avoid scrolling / default behavior

      if (activeKey === k) return; // already active, ignore repeat
      activeKey = k;

      if (k === 'w') {
        setKeyActive('w', true);
        sendCommand('/forward', 'Forward');
      } else if (k === 's') {
        setKeyActive('s', true);
        sendCommand('/backward', 'Backward');
      } else if (k === 'a') {
        setKeyActive('a', true);
        sendCommand('/left', 'Left');
      } else if (k === 'd') {
        setKeyActive('d', true);
        sendCommand('/right', 'Right');
      }
    });

    document.addEventListener('keyup', function(e) {
      const k = e.key.toLowerCase();
      if (!['w','a','s','d'].includes(k)) return;
      e.preventDefault();

      // Clear visual
      setKeyActive('w', false);
      setKeyActive('a', false);
      setKeyActive('s', false);
      setKeyActive('d', false);

      // Only stop if we are releasing the active key
      if (activeKey === k) {
        activeKey = null;
        sendCommand('/stop', 'Stopped');
      }
    });

    // If window loses focus (alt-tab, etc), stop the car
    window.addEventListener('blur', function() {
      activeKey = null;
      setKeyActive('w', false);
      setKeyActive('a', false);
      setKeyActive('s', false);
      setKeyActive('d', false);
      sendCommand('/stop', 'Stopped (blur)');
    });
  </script>
</body>
</html>
)EOF";

// ----------------- PINS & WIFI CONFIG -----------------
const int BUZZER_PIN = 2;

// WiFi AP SETTINGS
const char* AP_SSID = "ESP32-RC-Car";     // network name you will see on laptop
const char* AP_PASS = "drivecar123";      // password (can change)

// HTTP server on port 80
WebServer server(80);

// ----------------- HTTP HANDLERS -----------------
void handleRoot() {
    server.send(200, "text/html", MAIN_PAGE);
}

int getSpeedFromRequest(int defaultSpeed = 2000) {
    if (server.hasArg("speed")) {
        int s = server.arg("speed").toInt();
        if (s < 0) s = 0;
        if (s > 4095) s = 4095;
        return s;
    }
    return defaultSpeed;
}

void handleForward() {
    int speed = getSpeedFromRequest();
    Car::forward(speed);
    server.send(200, "text/plain", "Moving forward at speed " + String(speed));
}

void handleBackward() {
    int speed = getSpeedFromRequest();
    Car::backward(speed);
    server.send(200, "text/plain", "Moving backward at speed " + String(speed));
}

void handleLeft() {
    int speed = getSpeedFromRequest();
    Car::turnLeft(speed);
    server.send(200, "text/plain", "Turning left at speed " + String(speed));
}

void handleRight() {
    int speed = getSpeedFromRequest();
    Car::turnRight(speed);
    server.send(200, "text/plain", "Turning right at speed " + String(speed));
}

void handleStop() {
    Car::stop();
    server.send(200, "text/plain", "Stopped");
}

void handleNotFound() {
    server.send(404, "text/plain", "Not found. Use /forward, /backward, /left, /right, /stop");
}

// ----------------- SETUP & LOOP -----------------
void setup() {
    Serial.begin(115200);
    delay(500);

    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);  // keep buzzer off

    // ---- Initialize motors ----
    Serial.println("Initializing car...");
    Car::begin();
    Car::stop();
    Serial.println("Car motor driver ready.");

    // ---- Start WiFi Access Point ----
    Serial.println("Starting WiFi AP...");
    WiFi.mode(WIFI_AP);
    bool apResult = WiFi.softAP(AP_SSID, AP_PASS);

    if (!apResult) {
        Serial.println("Failed to start AP!");
    } else {
        Serial.print("AP started. SSID: ");
        Serial.println(AP_SSID);
        Serial.print("IP address: ");
        Serial.println(WiFi.softAPIP());  // usually 192.168.4.1
    }

    // ---- Configure HTTP routes ----
    server.on("/", handleRoot);
    server.on("/forward", handleForward);
    server.on("/backward", handleBackward);
    server.on("/left", handleLeft);
    server.on("/right", handleRight);
    server.on("/stop", handleStop);
    server.onNotFound(handleNotFound);

    server.begin();
    Serial.println("HTTP server started.");
}

void loop() {
    server.handleClient();
    // no automatic motion here – car only moves when you hit endpoints
}
