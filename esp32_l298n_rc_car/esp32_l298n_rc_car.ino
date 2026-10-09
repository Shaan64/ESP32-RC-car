/*
 * ESP32 + L298N 4WD WiFi RC Car
 * ---------------------------------------------------------------
 * ARCHITECTURE
 *   - ESP32 runs in SoftAP mode (no router required). Phone connects
 *     directly to the ESP32's WiFi and loads a control page at
 *     http://192.168.4.1/
 *   - 4 motors, wired in pairs: LEFT side motors in parallel on one
 *     L298N channel (IN1/IN2/ENA), RIGHT side motors in parallel on
 *     the other channel (IN3/IN4/ENB). This is the standard skid-steer
 *     configuration for a single L298N driving 4 DC motors.
 *   - Steering is skid-steer (pivot turn): LEFT = left side reverse +
 *     right side forward, RIGHT = the opposite. If you want arc turns
 *     instead of pivot turns, change applyDirection().
 *   - The web UI sends /move?dir=X repeatedly while a direction button
 *     is held (every 200ms) and /move?dir=S on release. This is a
 *     deadman-switch pattern: the firmware ALSO has an independent
 *     800ms failsafe timeout that stops the motors if no command
 *     arrives (WiFi drop, browser closed, phone locked, etc).
 *
 * REQUIREMENTS
 *   - Arduino ESP32 board package version >= 3.0.0
 *     (this sketch uses the current ledcAttach(pin,freq,res) /
 *     ledcWrite(pin,duty) API. On core < 3.0 this will NOT compile;
 *     you'd need the old ledcSetup/ledcAttachPin/channel-based API.)
 *
 * WIRING (change pins below if your layout differs)
 *   L298N            ESP32 GPIO
 *   ---------------------------
 *   IN1 (left dir A)  -> 27
 *   IN2 (left dir B)  -> 26
 *   ENA (left PWM)    -> 14
 *   IN3 (right dir A) -> 33
 *   IN4 (right dir B) -> 32
 *   ENB (right PWM)   -> 25
 *   GND               -> ESP32 GND (common ground, mandatory)
 *
 *   All 6 GPIOs above are safe general-purpose pins on a standard
 *   ESP32 devkit (no flash pins, no strapping pins).
 *
 * POWER - READ THIS, IT IS THE #1 CAUSE OF "IT DOESN'T WORK"
 *   - Motors must be powered from the L298N's own supply input
 *     (6-12V battery pack for the motors), NOT from the ESP32's 5V/3V3
 *     pins. The ESP32 5V regulator cannot source motor current.
 *   - ESP32 GND, L298N GND, and motor battery GND must all be tied
 *     together (common ground). Skipping this causes erratic/no motor
 *     response even if the ESP32 code is correct.
 *   - Remove the L298N's ENA/ENB jumper caps if present - those tie
 *     EN permanently to 5V and disable PWM speed control from code.
 *   - L298N logic inputs are rated for 5V TTL levels but the ~2.3V
 *     minimum HIGH threshold at 5V supply is met by ESP32's 3.3V
 *     output on essentially every real L298N breakout board in
 *     practice. If motors are unresponsive to direction while PWM
 *     clearly works, a logic-level shifter on IN1-IN4 is the fix.
 *
 * USAGE
 *   1. Flash this sketch.
 *   2. On your phone, connect to WiFi SSID "ESP32_RC_Car"
 *      (password below - CHANGE IT before real use).
 *   3. Open a browser to http://192.168.4.1/
 *   4. Hold direction buttons to drive, use the slider for speed.
 * ---------------------------------------------------------------
 */

#include <WiFi.h>
#include <WebServer.h>

// ---------------- WiFi AP credentials ----------------
const char* AP_SSID     = "ESP32_RC_Car";
const char* AP_PASSWORD = "12345678";   // WPA2 requires >= 8 chars. CHANGE THIS.

// ---------------- Motor driver pins (L298N) ----------------
const int IN1_PIN = 27;   // Left  motor direction A
const int IN2_PIN = 26;   // Left  motor direction B
const int ENA_PIN = 14;   // Left  motor PWM (speed)

const int IN3_PIN = 33;   // Right motor direction A
const int IN4_PIN = 32;   // Right motor direction B
const int ENB_PIN = 25;   // Right motor PWM (speed)

// ---------------- PWM configuration ----------------
const int PWM_FREQ       = 5000;  // Hz, standard for DC motor drivers
const int PWM_RESOLUTION = 8;     // bits -> duty range 0-255

// Below this duty the motors don't have enough torque to overcome static
// friction/back-EMF and just buzz without turning. Measured on THIS chassis
// and THIS motor/wheel combo - re-measure if you change motors, wheels, or
// the mechanical load, since the real floor is empirical, not a constant.
const uint8_t PWM_FLOOR = 100;
const uint8_t PWM_MAX   = 255;

// ---------------- Runtime state ----------------
// The UI exposes 0-100% to the user, not raw PWM - "50% speed" means
// something to a person, "duty 178" doesn't. speedPercentToPWM() is the
// ONLY place that maps one to the other, so the firmware and the web page
// never have two copies of that formula to keep in sync.
uint8_t currentSpeedPercent = 65;        // 0-100, set by the web UI slider
uint8_t currentSpeed        = 0;         // 0-255 PWM, derived from the line above
char currentDir = 'S';                   // 'F','B','L','R','S'
unsigned long lastCommandMillis = 0;
const unsigned long COMMAND_TIMEOUT_MS = 800;  // failsafe: stop if no command in time

WebServer server(80);

// percent 0-100  ->  PWM PWM_FLOOR-PWM_MAX. 0% is NOT "motor off" - it's the
// slowest speed that still turns the wheels. Stopping is a separate action
// (STOP / failsafe), not something this slider does.
uint8_t speedPercentToPWM(int percent) {
  percent = constrain(percent, 0, 100);
  return (uint8_t) map(percent, 0, 100, PWM_FLOOR, PWM_MAX);
}

// ---------------- Motor primitives ----------------
void setLeft(bool forward, uint8_t spd) {
  digitalWrite(IN1_PIN, forward ? HIGH : LOW);
  digitalWrite(IN2_PIN, forward ? LOW  : HIGH);
  ledcWrite(ENA_PIN, spd);
}

void setRight(bool forward, uint8_t spd) {
  digitalWrite(IN3_PIN, forward ? HIGH : LOW);
  digitalWrite(IN4_PIN, forward ? LOW  : HIGH);
  ledcWrite(ENB_PIN, spd);
}

void stopMotors() {
  ledcWrite(ENA_PIN, 0);
  ledcWrite(ENB_PIN, 0);
  digitalWrite(IN1_PIN, LOW);
  digitalWrite(IN2_PIN, LOW);
  digitalWrite(IN3_PIN, LOW);
  digitalWrite(IN4_PIN, LOW);
}

void applyDirection(char dir, uint8_t spd) {
  switch (dir) {
    case 'F': setLeft(true,  spd); setRight(true,  spd); break;
    case 'B': setLeft(false, spd); setRight(false, spd); break;
    case 'L': setLeft(false, spd); setRight(true,  spd); break; // pivot left
    case 'R': setLeft(true,  spd); setRight(false, spd); break; // pivot right
    case 'S':
    default:  stopMotors(); break;
  }
}

// ---------------- Web UI (served from flash, not RAM) ----------------
const char INDEX_HTML[] PROGMEM = R"HTMLPAGE(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1, maximum-scale=1, user-scalable=no">
<title>ESP32 RC Car</title>
<style>
  :root{
    --btn-size: 92px;
    --accent: #00c853;
    --bg: #111417;
    --panel: #1c2128;
  }
  *{ box-sizing:border-box; -webkit-tap-highlight-color:transparent; }
  html,body{
    margin:0; padding:0; height:100%;
    background:var(--bg); color:#eee;
    font-family:-apple-system,Segoe UI,Roboto,sans-serif;
    overscroll-behavior:none; user-select:none; touch-action:none;
  }
  .wrap{
    display:flex; flex-direction:column; align-items:center;
    justify-content:space-between; min-height:100vh;
    padding:20px 16px; gap:20px;
  }
  h1{ font-size:16px; font-weight:600; margin:4px 0; letter-spacing:1px; color:#9aa0a6; }
  .dpad{
    display:grid;
    grid-template-columns: var(--btn-size) var(--btn-size) var(--btn-size);
    grid-template-rows: var(--btn-size) var(--btn-size) var(--btn-size);
    gap:12px; justify-content:center;
  }
  .btn{
    display:flex; align-items:center; justify-content:center;
    background:var(--panel); border:1px solid #2a2f37;
    border-radius:18px; font-size:34px; color:#fff;
    user-select:none; touch-action:none;
    transition:background .1s, transform .1s;
  }
  .btn.active{ background:var(--accent); transform:scale(.94); }
  #btnF{ grid-column:2; grid-row:1; }
  #btnL{ grid-column:1; grid-row:2; }
  #btnStop{ grid-column:2; grid-row:2; font-size:13px; font-weight:700;
            background:#3a1f1f; border-color:#5a2a2a; }
  #btnStop.active{ background:#c62828; }
  #btnR{ grid-column:3; grid-row:2; }
  #btnB{ grid-column:2; grid-row:3; }
  .speed-panel{
    width:100%; max-width:420px; background:var(--panel);
    border-radius:16px; padding:16px 20px; border:1px solid #2a2f37;
  }
  .speed-label{ display:flex; justify-content:space-between; font-size:13px;
                color:#9aa0a6; margin-bottom:8px; }
  #speedVal{ color:var(--accent); font-weight:700; }
  input[type=range]{ width:100%; height:36px; -webkit-appearance:none; background:transparent; }
  input[type=range]::-webkit-slider-runnable-track{ height:8px; border-radius:4px; background:#333; }
  input[type=range]::-webkit-slider-thumb{
    -webkit-appearance:none; width:28px; height:28px; border-radius:50%;
    background:var(--accent); margin-top:-10px; border:3px solid #0d1114;
  }
  input[type=range]::-moz-range-track{ height:8px; border-radius:4px; background:#333; }
  input[type=range]::-moz-range-thumb{
    width:28px; height:28px; border-radius:50%; background:var(--accent); border:3px solid #0d1114;
  }
  .status{ font-size:12px; color:#5f6368; min-height:14px; }
  @media (min-width:480px){ :root{ --btn-size:104px; } }
</style>
</head>
<body>
<div class="wrap">
  <h1>ESP32 RC CAR</h1>

  <div class="dpad">
    <div class="btn" id="btnF" data-dir="F">&#9650;</div>
    <div class="btn" id="btnL" data-dir="L">&#9664;</div>
    <div class="btn" id="btnStop" data-dir="S">STOP</div>
    <div class="btn" id="btnR" data-dir="R">&#9654;</div>
    <div class="btn" id="btnB" data-dir="B">&#9660;</div>
  </div>

  <div class="speed-panel">
    <div class="speed-label"><span>SPEED</span><span id="speedVal">65%</span></div>
    <input type="range" id="speedSlider" min="0" max="100" value="65">
  </div>

  <div class="status" id="status">Ready</div>
</div>

<script>
(function(){
  var HOLD_INTERVAL_MS = 200;   // must be well under the firmware's 800ms failsafe
  var activeDir = null;
  var holdTimer = null;
  var statusEl = document.getElementById('status');

  function sendMove(dir){
    fetch('/move?dir=' + dir)
      .then(function(r){ statusEl.textContent = r.ok ? ('OK: ' + dir) : 'Error'; })
      .catch(function(){ statusEl.textContent = 'Connection lost'; });
  }

  function startHold(btn, dir){
    if (activeDir === dir) return;
    activeDir = dir;
    btn.classList.add('active');
    sendMove(dir);
    clearInterval(holdTimer);
    holdTimer = setInterval(function(){ sendMove(dir); }, HOLD_INTERVAL_MS);
  }

  function endHold(btn){
    clearInterval(holdTimer);
    holdTimer = null;
    if (btn) btn.classList.remove('active');
    if (activeDir !== null && activeDir !== 'S'){ sendMove('S'); }
    activeDir = null;
  }

  document.querySelectorAll('.dpad .btn').forEach(function(btn){
    var dir = btn.getAttribute('data-dir');
    if (dir === 'S'){
      btn.addEventListener('pointerdown', function(e){
        e.preventDefault();
        sendMove('S');
        btn.classList.add('active');
        setTimeout(function(){ btn.classList.remove('active'); }, 150);
      });
      return;
    }
    btn.addEventListener('pointerdown',  function(e){ e.preventDefault(); startHold(btn, dir); });
    btn.addEventListener('pointerup',    function(e){ e.preventDefault(); endHold(btn); });
    btn.addEventListener('pointerleave', function(){ endHold(btn); });
    btn.addEventListener('pointercancel',function(){ endHold(btn); });
  });

  var slider = document.getElementById('speedSlider');
  var speedVal = document.getElementById('speedVal');
  var lastSpeedSend = 0;
  var SPEED_THROTTLE_MS = 120; // WebServer is blocking/single-threaded, avoid flooding it

  slider.addEventListener('input', function(){
    speedVal.textContent = slider.value + '%';
    var now = Date.now();
    if (now - lastSpeedSend > SPEED_THROTTLE_MS){
      lastSpeedSend = now;
      fetch('/speed?value=' + slider.value).catch(function(){});
    }
  });
  slider.addEventListener('change', function(){
    fetch('/speed?value=' + slider.value).catch(function(){});
  });

  document.addEventListener('contextmenu', function(e){ e.preventDefault(); });
})();
</script>
</body>
</html>
)HTMLPAGE";

// ---------------- HTTP handlers ----------------
void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleMove() {
  if (!server.hasArg("dir")) { server.send(400, "text/plain", "missing dir"); return; }
  String d = server.arg("dir");
  char dir = d.length() ? d[0] : 'S';
  if (dir=='F' || dir=='B' || dir=='L' || dir=='R' || dir=='S') {
    currentDir = dir;
    lastCommandMillis = millis();
    applyDirection(currentDir, currentSpeed);   // currentSpeed is already a PWM value
    server.send(200, "text/plain", "OK");
  } else {
    server.send(400, "text/plain", "bad dir");
  }
}

void handleSpeed() {
  // Wire format is percent (0-100), matching what the slider shows the user.
  if (!server.hasArg("value")) { server.send(400, "text/plain", "missing value"); return; }
  int percent = server.arg("value").toInt();
  currentSpeedPercent = (uint8_t) constrain(percent, 0, 100);
  currentSpeed = speedPercentToPWM(currentSpeedPercent);
  if (currentDir != 'S') {           // re-apply instantly if already moving
    lastCommandMillis = millis();
    applyDirection(currentDir, currentSpeed);
  }
  server.send(200, "text/plain", "OK");
}

void handleNotFound() {
  server.send(404, "text/plain", "Not found");
}

// ---------------- Setup / loop ----------------
void setup() {
  Serial.begin(115200);

  pinMode(IN1_PIN, OUTPUT);
  pinMode(IN2_PIN, OUTPUT);
  pinMode(IN3_PIN, OUTPUT);
  pinMode(IN4_PIN, OUTPUT);

  // ESP32 core >= 3.0 pin-based ledc API
  if (!ledcAttach(ENA_PIN, PWM_FREQ, PWM_RESOLUTION)) {
    Serial.println("ERROR: ledcAttach failed on ENA_PIN");
  }
  if (!ledcAttach(ENB_PIN, PWM_FREQ, PWM_RESOLUTION)) {
    Serial.println("ERROR: ledcAttach failed on ENB_PIN");
  }

  stopMotors();
  currentSpeed = speedPercentToPWM(currentSpeedPercent);  // sync PWM to the UI's default 65%

  WiFi.mode(WIFI_AP);
  bool apOk = WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.println(apOk ? "SoftAP started" : "ERROR: SoftAP failed to start");
  Serial.print("Connect phone to WiFi \"");
  Serial.print(AP_SSID);
  Serial.println("\" then browse to:");
  Serial.print("http://");
  Serial.println(WiFi.softAPIP());   // default 192.168.4.1

  server.on("/", handleRoot);
  server.on("/move", handleMove);
  server.on("/speed", handleSpeed);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();

  // Failsafe: if the browser stops sending keep-alive commands (WiFi drop,
  // phone locked, app closed), cut motors instead of driving off unattended.
  if (currentDir != 'S' && (millis() - lastCommandMillis > COMMAND_TIMEOUT_MS)) {
    currentDir = 'S';
    stopMotors();
  }
}
