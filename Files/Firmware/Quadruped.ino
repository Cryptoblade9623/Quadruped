#include <WiFi.h>
#include <WebSocketsServer.h>
#include <Wire.h>
#include <Adafruit_PWMServoDriver.h>

// ═══════════════════════════════════════════════
//  WIFI ACCESS POINT SETTINGS
// ═══════════════════════════════════════════════
const char* AP_SSID     = "Quadruped";
const char* AP_PASSWORD = "12345678";

// ═══════════════════════════════════════════════
//  SERVO CALIBRATION
//  Adjust these once you know your neutral angles
//  Order: CH0=FL_Hip, CH1=FL_Knee,
//         CH2=FR_Hip, CH3=FR_Knee,
//         CH4=RL_Hip, CH5=RL_Knee,
//         CH6=RR_Hip, CH7=RR_Knee
// ═══════════════════════════════════════════════
int neutralPos[8] = {
  90,  // CH0  Front Left  Hip
  90,  // CH1  Front Left  Knee
  90,  // CH2  Front Right Hip
  90,  // CH3  Front Right Knee
  90,  // CH4  Rear  Left  Hip
  90,  // CH5  Rear  Left  Knee
  90,  // CH6  Rear  Right Hip
  90   // CH7  Rear  Right Knee
};

// ═══════════════════════════════════════════════
//  GAIT PARAMETERS  (degrees, tweak freely)
// ═══════════════════════════════════════════════
int hipSwing   = 25;   // how far hips swing forward/back
int kneeSwing  = 30;   // how far knees lift
int stepDelay  = 120;  // ms between gait steps
int turnSwing  = 20;   // hip offset for turning

// ═══════════════════════════════════════════════
//  MG90S PULSE WIDTHS  (microseconds)
// ═══════════════════════════════════════════════
#define SERVO_MIN_US  500
#define SERVO_MAX_US  2500
#define SERVO_FREQ    50

// ═══════════════════════════════════════════════
//  CHANNEL MAP
// ═══════════════════════════════════════════════
#define FL_HIP   0
#define FL_KNEE  1
#define FR_HIP   2
#define FR_KNEE  3
#define RL_HIP   4
#define RL_KNEE  5
#define RR_HIP   6
#define RR_KNEE  7

// ═══════════════════════════════════════════════
//  OBJECTS
// ═══════════════════════════════════════════════
Adafruit_PWMServoDriver pwm = Adafruit_PWMServoDriver(0x40);
WebSocketsServer webSocket = WebSocketsServer(81);
WiFiServer server(80);

// ═══════════════════════════════════════════════
//  STATE
// ═══════════════════════════════════════════════
String currentAction = "standby";
bool moving = false;
unsigned long lastStepTime = 0;
int gaitPhase = 0;

// ═══════════════════════════════════════════════
//  WEBPAGE  (served from ESP32)
// ═══════════════════════════════════════════════
const char webpage[] PROGMEM = R"rawhtml(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
<title>Quadruped Control</title>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; touch-action: manipulation; }
  body {
    background: #0d0d0d;
    color: #e0e0e0;
    font-family: 'Segoe UI', sans-serif;
    height: 100vh;
    display: flex;
    flex-direction: column;
    overflow: hidden;
  }
  header {
    background: #1a1a2e;
    padding: 12px 20px;
    display: flex;
    align-items: center;
    justify-content: space-between;
    border-bottom: 2px solid #00d4ff;
  }
  header h1 { font-size: 18px; color: #00d4ff; letter-spacing: 2px; }
  #status {
    display: flex; align-items: center; gap: 8px; font-size: 13px;
  }
  #dot {
    width: 10px; height: 10px; border-radius: 50%;
    background: #ff4444; transition: background 0.3s;
  }
  #dot.connected { background: #00ff88; }
  .main {
    display: flex;
    flex: 1;
    overflow: hidden;
  }
  /* ── D-PAD ── */
  .dpad-area {
    flex: 1;
    display: flex;
    align-items: center;
    justify-content: center;
    padding: 20px;
  }
  .dpad {
    display: grid;
    grid-template-columns: repeat(3, 70px);
    grid-template-rows: repeat(3, 70px);
    gap: 8px;
  }
  .dpad-btn {
    background: #1e1e3a;
    border: 2px solid #00d4ff44;
    border-radius: 12px;
    color: #00d4ff;
    font-size: 28px;
    cursor: pointer;
    display: flex;
    align-items: center;
    justify-content: center;
    transition: all 0.1s;
    user-select: none;
    -webkit-user-select: none;
  }
  .dpad-btn:active, .dpad-btn.pressed {
    background: #00d4ff;
    color: #0d0d0d;
    transform: scale(0.93);
  }
  .dpad-btn.stop {
    background: #2a1a1a;
    border-color: #ff444444;
    color: #ff4444;
    font-size: 20px;
  }
  .dpad-btn.stop:active {
    background: #ff4444;
    color: #fff;
  }
  .dpad-empty { visibility: hidden; }
  /* ── SIDEBAR ── */
  .sidebar {
    width: 160px;
    background: #111122;
    border-left: 2px solid #00d4ff22;
    display: flex;
    flex-direction: column;
    padding: 16px 10px;
    gap: 10px;
  }
  .sidebar h2 {
    font-size: 11px;
    letter-spacing: 2px;
    color: #00d4ff88;
    text-align: center;
    margin-bottom: 4px;
  }
  .action-btn {
    background: #1a1a2e;
    border: 2px solid #ffffff11;
    border-radius: 10px;
    color: #ccc;
    font-size: 14px;
    padding: 12px 8px;
    cursor: pointer;
    text-align: center;
    transition: all 0.15s;
    user-select: none;
  }
  .action-btn:active, .action-btn.active {
    background: #00d4ff22;
    border-color: #00d4ff;
    color: #00d4ff;
  }
  /* ── FOOTER ── */
  footer {
    background: #111122;
    padding: 8px 20px;
    font-size: 12px;
    color: #555;
    border-top: 1px solid #ffffff11;
    display: flex;
    justify-content: space-between;
  }
  #currentAction { color: #00d4ff88; }
</style>
</head>
<body>

<header>
  <h1>🤖 QUADRUPED</h1>
  <div id="status">
    <div id="dot"></div>
    <span id="connText">Connecting...</span>
  </div>
</header>

<div class="main">
  <!-- D-PAD -->
  <div class="dpad-area">
    <div class="dpad">
      <div class="dpad-empty"></div>
      <div class="dpad-btn" id="btn-forward"   ontouchstart="send('forward')"  ontouchend="send('stop')" onmousedown="send('forward')" onmouseup="send('stop')">▲</div>
      <div class="dpad-empty"></div>

      <div class="dpad-btn" id="btn-left"      ontouchstart="send('left')"     ontouchend="send('stop')" onmousedown="send('left')"    onmouseup="send('stop')">◀</div>
      <div class="dpad-btn stop" id="btn-stop" ontouchstart="send('stop')"     ontouchend="send('stop')" onmousedown="send('stop')"    onmouseup="send('stop')">■</div>
      <div class="dpad-btn" id="btn-right"     ontouchstart="send('right')"    ontouchend="send('stop')" onmousedown="send('right')"   onmouseup="send('stop')">▶</div>

      <div class="dpad-empty"></div>
      <div class="dpad-btn" id="btn-backward"  ontouchstart="send('backward')" ontouchend="send('stop')" onmousedown="send('backward')" onmouseup="send('stop')">▼</div>
      <div class="dpad-empty"></div>
    </div>
  </div>

  <!-- SIDEBAR -->
  <div class="sidebar">
    <h2>ACTIONS</h2>
    <div class="action-btn" onclick="send('standby')">🔋 Standby</div>
    <div class="action-btn" onclick="send('sleep')">😴 Sleep</div>
    <div class="action-btn" onclick="send('wave')">👋 Wave</div>
    <div class="action-btn" onclick="send('rotate')">🔄 Rotate</div>
  </div>
</div>

<footer>
  <span>192.168.4.1</span>
  <span id="currentAction">standby</span>
</footer>

<script>
  var ws;
  var reconnectTimer;

  function connect() {
    ws = new WebSocket('ws://' + location.hostname + ':81/');
    ws.onopen = function() {
      document.getElementById('dot').classList.add('connected');
      document.getElementById('connText').textContent = 'Connected';
      clearTimeout(reconnectTimer);
    };
    ws.onclose = function() {
      document.getElementById('dot').classList.remove('connected');
      document.getElementById('connText').textContent = 'Reconnecting...';
      reconnectTimer = setTimeout(connect, 2000);
    };
    ws.onerror = function() { ws.close(); };
  }

  function send(cmd) {
    document.getElementById('currentAction').textContent = cmd;
    if (ws && ws.readyState === 1) ws.send(cmd);
  }

  connect();
</script>
</body>
</html>
)rawhtml";

// ═══════════════════════════════════════════════
//  SERVO HELPERS
// ═══════════════════════════════════════════════
void setServo(uint8_t ch, int degrees) {
  degrees = constrain(degrees, 0, 180);
  int us = map(degrees, 0, 180, SERVO_MIN_US, SERVO_MAX_US);
  int ticks = (int)((float)us / 1000000.0f * SERVO_FREQ * 4096.0f);
  pwm.setPWM(ch, 0, ticks);
}

// Mirror: left side servos are physically flipped vs right side
// Pass raw angle; function applies mirror for right-side channels
void setHip(uint8_t ch, int angle) {
  // Right side channels (FR=2, RR=6) are mirrored
  if (ch == FR_HIP || ch == RR_HIP) {
    angle = 180 - angle;
  }
  setServo(ch, angle);
}

void setKnee(uint8_t ch, int angle) {
  // Right side knees (FR=3, RR=7) are mirrored
  if (ch == FR_KNEE || ch == RR_KNEE) {
    angle = 180 - angle;
  }
  setServo(ch, angle);
}

void allNeutral() {
  for (int i = 0; i < 8; i++) setServo(i, neutralPos[i]);
}

// ═══════════════════════════════════════════════
//  POSES
// ═══════════════════════════════════════════════
void poseStandby() {
  allNeutral();
}

void poseSleep() {
  // Crouch down — knees bent more
  setHip(FL_HIP,  neutralPos[FL_HIP]);
  setHip(FR_HIP,  neutralPos[FR_HIP]);
  setHip(RL_HIP,  neutralPos[RL_HIP]);
  setHip(RR_HIP,  neutralPos[RR_HIP]);
  setKnee(FL_KNEE, neutralPos[FL_KNEE] + 40);
  setKnee(FR_KNEE, neutralPos[FR_KNEE] + 40);
  setKnee(RL_KNEE, neutralPos[RL_KNEE] + 40);
  setKnee(RR_KNEE, neutralPos[RR_KNEE] + 40);
}

// ═══════════════════════════════════════════════
//  GAIT STEP  (non-blocking, called from loop)
// ═══════════════════════════════════════════════
void walkStep(int dir) {
  // Diagonal pairs: (FL+RR) and (FR+RL)
  // dir: +1 = forward, -1 = backward
  switch (gaitPhase) {
    case 0: // Lift FL + RR, swing forward
      setKnee(FL_KNEE, neutralPos[FL_KNEE] - kneeSwing);
      setKnee(RR_KNEE, neutralPos[RR_KNEE] - kneeSwing);
      break;
    case 1:
      setHip(FL_HIP, neutralPos[FL_HIP] + hipSwing * dir);
      setHip(RR_HIP, neutralPos[RR_HIP] + hipSwing * dir);
      break;
    case 2: // Plant FL + RR
      setKnee(FL_KNEE, neutralPos[FL_KNEE]);
      setKnee(RR_KNEE, neutralPos[RR_KNEE]);
      break;
    case 3: // Push back with FL + RR, lift FR + RL
      setHip(FL_HIP, neutralPos[FL_HIP] - hipSwing * dir);
      setHip(RR_HIP, neutralPos[RR_HIP] - hipSwing * dir);
      setKnee(FR_KNEE, neutralPos[FR_KNEE] - kneeSwing);
      setKnee(RL_KNEE, neutralPos[RL_KNEE] - kneeSwing);
      break;
    case 4:
      setHip(FR_HIP, neutralPos[FR_HIP] + hipSwing * dir);
      setHip(RL_HIP, neutralPos[RL_HIP] + hipSwing * dir);
      break;
    case 5: // Plant FR + RL
      setKnee(FR_KNEE, neutralPos[FR_KNEE]);
      setKnee(RL_KNEE, neutralPos[RL_KNEE]);
      break;
    case 6: // Push back
      setHip(FR_HIP, neutralPos[FR_HIP] - hipSwing * dir);
      setHip(RL_HIP, neutralPos[RL_HIP] - hipSwing * dir);
      break;
  }
  gaitPhase = (gaitPhase + 1) % 7;
}

void turnStep(int dir) {
  // dir: +1 = right, -1 = left
  switch (gaitPhase) {
    case 0:
      setKnee(FL_KNEE, neutralPos[FL_KNEE] - kneeSwing);
      setKnee(RR_KNEE, neutralPos[RR_KNEE] - kneeSwing);
      break;
    case 1:
      setHip(FL_HIP, neutralPos[FL_HIP] + turnSwing * dir);
      setHip(RR_HIP, neutralPos[RR_HIP] - turnSwing * dir);
      break;
    case 2:
      setKnee(FL_KNEE, neutralPos[FL_KNEE]);
      setKnee(RR_KNEE, neutralPos[RR_KNEE]);
      break;
    case 3:
      setKnee(FR_KNEE, neutralPos[FR_KNEE] - kneeSwing);
      setKnee(RL_KNEE, neutralPos[RL_KNEE] - kneeSwing);
      break;
    case 4:
      setHip(FR_HIP, neutralPos[FR_HIP] - turnSwing * dir);
      setHip(RL_HIP, neutralPos[RL_HIP] + turnSwing * dir);
      break;
    case 5:
      setKnee(FR_KNEE, neutralPos[FR_KNEE]);
      setKnee(RL_KNEE, neutralPos[RL_KNEE]);
      break;
  }
  gaitPhase = (gaitPhase + 1) % 6;
}

// ═══════════════════════════════════════════════
//  WAVE  (blocking animation, runs once)
// ═══════════════════════════════════════════════
void doWave() {
  allNeutral();
  delay(200);
  for (int i = 0; i < 3; i++) {
    setKnee(FR_KNEE, neutralPos[FR_KNEE] - 50);
    setHip(FR_HIP,  neutralPos[FR_HIP]  - 30);
    delay(300);
    setHip(FR_HIP,  neutralPos[FR_HIP]  + 30);
    delay(300);
  }
  allNeutral();
  currentAction = "standby";
}

// ═══════════════════════════════════════════════
//  ROTATE  (non-blocking, uses turnStep right)
// ═══════════════════════════════════════════════

// ═══════════════════════════════════════════════
//  WEBSOCKET EVENT
// ═══════════════════════════════════════════════
void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  if (type == WStype_TEXT) {
    String cmd = String((char*)payload);
    cmd.trim();
    Serial.println("CMD: " + cmd);

    if (cmd == "stop" || cmd == "standby") {
      currentAction = "standby";
      moving = false;
      gaitPhase = 0;
      poseStandby();
    } else if (cmd == "sleep") {
      currentAction = "sleep";
      moving = false;
      poseSleep();
    } else if (cmd == "wave") {
      currentAction = "wave";
      moving = false;
      doWave();
    } else if (cmd == "forward") {
      currentAction = "forward";
      moving = true;
      gaitPhase = 0;
    } else if (cmd == "backward") {
      currentAction = "backward";
      moving = true;
      gaitPhase = 0;
    } else if (cmd == "left") {
      currentAction = "left";
      moving = true;
      gaitPhase = 0;
    } else if (cmd == "right") {
      currentAction = "right";
      moving = true;
      gaitPhase = 0;
    } else if (cmd == "rotate") {
      currentAction = "rotate";
      moving = true;
      gaitPhase = 0;
    }
  }
}

// ═══════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  Serial.println("Quadruped booting...");

  // PCA9685
  Wire.begin(6, 7); // SDA=GPIO6, SCL=GPIO7 for ESP32-C3 Super Mini
  pwm.begin();
  pwm.setOscillatorFrequency(27000000);
  pwm.setPWMFreq(SERVO_FREQ);
  delay(100);
  allNeutral();
  Serial.println("Servos ready");

  // WiFi AP
  WiFi.softAP(AP_SSID, AP_PASSWORD);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  // HTTP server
  server.begin();

  // WebSocket
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  Serial.println("Ready! Connect to WiFi: Quadruped / 12345678");
  Serial.println("Then open browser: http://192.168.4.1");
}

// ═══════════════════════════════════════════════
//  LOOP
// ═══════════════════════════════════════════════
void loop() {
  webSocket.loop();

  // Serve HTTP page
  WiFiClient client = server.available();
  if (client) {
    String req = "";
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        req += c;
        if (req.endsWith("\r\n\r\n")) break;
      }
    }
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/html");
    client.println("Connection: close");
    client.println();
    client.print(webpage);
    client.stop();
  }

  // Non-blocking gait
  if (moving && (millis() - lastStepTime > stepDelay)) {
    lastStepTime = millis();
    if (currentAction == "forward")  walkStep(1);
    if (currentAction == "backward") walkStep(-1);
    if (currentAction == "left")     turnStep(-1);
    if (currentAction == "right")    turnStep(1);
    if (currentAction == "rotate")   turnStep(1);
  }
}
