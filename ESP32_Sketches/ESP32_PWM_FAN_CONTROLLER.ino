#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>
#include <time.h>

// --------------------
// WIFI

const char* ssid = "YOUR_SSID_HERE";
const char* password = "YOUR_WIFI_PASSWORD_HERE";

// --------------------
// FAN PINS

const int fanPins[6] = {32, 14, 25, 26, 27, 33};

// PWM channels
const int pwmChannels[6] = {0, 1, 2, 3, 4, 5};

// --------------------
// PWM SETTINGS

#define PWM_FREQ 25000
#define PWM_RESOLUTION 8

// --------------------
// DEFAULT fan speed

#define DEFAULT_FAN_PERCENT 50

int fanPWM[6] = {DEFAULT_FAN_PERCENT, DEFAULT_FAN_PERCENT, DEFAULT_FAN_PERCENT,
                  DEFAULT_FAN_PERCENT, DEFAULT_FAN_PERCENT, DEFAULT_FAN_PERCENT};

// --------------------
// CONTROL MODE - Boots into API mode by default

enum ControlMode { MODE_API = 0, MODE_MANUAL = 1 };
ControlMode currentMode = MODE_API;   // MODE_API / MODE_MANUAL


// --------------------
// API KEY

// Change this before deploying
const char* API_KEY = "CHANGE_ME_SECRET_KEY";

// --------------------
// LAST API CALL LOG (fans)

struct ApiCallLog {
    bool hasCall = false;
    time_t timestamp = 0;
    int fanId = -1;       // -1 = all fans
    int value = 0;
    bool applied = false; // false if it arrived while in manual mode (logged but ignored)
    String ip = "";
};
ApiCallLog lastApiCall;

// --------------------
// NTP / TIME

const char* ntpServer1 = "pool.ntp.org";
const char* ntpServer2 = "time.nist.gov";
// Ireland/UK local time incl. DST. Adjust if the ESP32 lives elsewhere.
const char* tzInfo = "GMT0BST,M3.5.0/1,M10.5.0";

WebServer server(80);

// --------------------
// ESP32 INTERNAL TEMPERATURE

#ifdef __cplusplus
extern "C" {
#endif
uint8_t temprature_sens_read();
#ifdef __cplusplus
}
#endif

float readChipTemp() {
    return (temprature_sens_read() - 32) / 1.8;
}

// --------------------
// TIME / MODE HELPERS

String formatTime(time_t t) {
    if (t == 0) return "N/A";

    struct tm timeinfo;
    localtime_r(&t, &timeinfo);

    char buf[24];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &timeinfo);

    return String(buf);
}

String modeToString(ControlMode m) {
    return m == MODE_MANUAL ? "manual" : "api";
}

// --------------------
// APPLY PWM

void setFan(int id, int percent) {

    percent = constrain(percent, 0, 100);

    fanPWM[id] = percent;

    int value = map(percent, 0, 100, 0, 255);

    ledcWrite(fanPins[id], value);
}

// --------------------
// SET ALL FANS

void setAllFans(int percent) {

    for (int i = 0; i < 6; i++) {
        setFan(i, percent);
    }
}

// --------------------
// APPLY AN INCOMING API CHANGE (logs regardless of mode)


bool applyApiChange(int fanId, int value, IPAddress ip) {

    value = constrain(value, 0, 100);

    lastApiCall.hasCall = true;
    lastApiCall.timestamp = time(nullptr);
    lastApiCall.fanId = fanId;
    lastApiCall.value = value;
    lastApiCall.ip = ip.toString();

    if (currentMode == MODE_API) {
        if (fanId == -1) {
            setAllFans(value);
        } else {
            setFan(fanId, value);
        }
        lastApiCall.applied = true;
        return true;
    }

    lastApiCall.applied = false;
    return false;
}

// --------------------
// WEB UI

String htmlPage() {

    return R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">

<title>ESP32 6-Fan Controller</title>

<style>
body {
    font-family: Arial, sans-serif;
    background: #f2f2f2;
    margin: 0;
    padding: 0;
}

.container {
    max-width: 780px;
    margin: 20px auto;
    background: white;
    border-radius: 14px;
    overflow: hidden;
    box-shadow: 0 2px 10px rgba(0,0,0,0.08);
}

.header {
    padding: 16px 22px;
    border-bottom: 1px solid #eee;
    display: flex;
    justify-content: space-between;
    align-items: center;
    flex-wrap: wrap;
    gap: 10px;
}

.header h2 {
    margin: 0;
}

.mode-box {
    display: flex;
    align-items: center;
    gap: 10px;
}

.mode-label {
    font-weight: bold;
    min-width: 130px;
    text-align: right;
}

.switch {
    position: relative;
    display: inline-block;
    width: 56px;
    height: 28px;
    flex-shrink: 0;
}

.switch input {
    opacity: 0;
    width: 0;
    height: 0;
}

.slider {
    position: absolute;
    cursor: pointer;
    inset: 0;
    background: #4caf50;
    transition: .3s;
    border-radius: 28px;
}

.slider:before {
    content: "";
    position: absolute;
    height: 20px;
    width: 20px;
    left: 4px;
    bottom: 4px;
    background: white;
    transition: .3s;
    border-radius: 50%;
}

input:checked + .slider {
    background: #ff9800;
}

input:checked + .slider:before {
    transform: translateX(28px);
}

.power-pill {
    padding: 4px 12px;
    border-radius: 20px;
    font-weight: bold;
    font-size: 13px;
}

.split {
    display: flex;
    flex-wrap: wrap;
}

.panel {
    flex: 1 1 320px;
    padding: 20px 22px;
}

.panel-left {
    border-right: 1px solid #eee;
}

@media (max-width: 680px) {
    .panel-left {
        border-right: none;
        border-bottom: 1px solid #eee;
    }
}

.panel h3 {
    margin-top: 0;
}

.panel.disabled {
    opacity: 0.45;
    pointer-events: none;
}

.fan-row {
    display: flex;
    align-items: center;
    gap: 8px;
    margin: 6px 0;
}

.fan-row label {
    width: 55px;
}

.current-val {
    width: 46px;
    color: #555;
    font-size: 13px;
}

input[type="number"] {
    width: 65px;
    padding: 4px;
}

button {
    padding: 7px 12px;
    border-radius: 6px;
    border: 1px solid #ccc;
    background: #f7f7f7;
    cursor: pointer;
}

button:hover {
    background: #eee;
}

hr {
    border: none;
    border-top: 1px solid #eee;
    margin: 16px 0;
}

.bubble-grid {
    display: grid;
    grid-template-columns: repeat(3, 1fr);
    gap: 14px;
    margin-bottom: 18px;
}

.bubble {
    width: 78px;
    height: 78px;
    border-radius: 50%;
    margin: 0 auto;
    display: flex;
    align-items: center;
    justify-content: center;
    background: conic-gradient(#43a047 0%, #e0e0e0 0%);
}

.bubble-inner {
    width: 62px;
    height: 62px;
    background: white;
    border-radius: 50%;
    display: flex;
    flex-direction: column;
    align-items: center;
    justify-content: center;
}

.bubble-value {
    font-weight: bold;
    font-size: 15px;
}

.bubble-label {
    font-size: 10px;
    color: #777;
}

.debug-box {
    background: #f7f7f7;
    border-radius: 8px;
    padding: 12px 14px;
    font-size: 13px;
    line-height: 1.6;
}

.debug-box .row {
    display: flex;
    justify-content: space-between;
}

.debug-box .row span:first-child {
    color: #777;
}

.temp-line {
    font-size: 13px;
    color: #555;
    margin-bottom: 14px;
}
</style>
</head>

<body>

<div class="container">

<div class="header">
    <h2>6-Fan Controller</h2>

    <div class="mode-box">
        <span class="mode-label" id="modeLabel">API Controlled</span>
        <label class="switch">
            <input type="checkbox" id="modeSwitch" onchange="setMode()">
            <span class="slider"></span>
        </label>
    </div>
</div>

<div class="split">

    <div class="panel panel-left" id="manualPanel">
        <h3>Manual Control</h3>

        <div id="fanRows"></div>

        <button onclick="setIndividual()">Apply Individual</button>

        <hr>

        <h3>Set All Fans</h3>
        <div class="fan-row">
            <input id="allFans" type="number" min="0" max="100" value="50">
            <button onclick="setAll()">Apply</button>
        </div>
    </div>

    <div class="panel panel-right">
        <h3>Automated (API) Mode</h3>
        <div class="temp-line">ESP32 temperature: <span id="temp">0</span> &deg;C</div>

        <div class="bubble-grid" id="bubbleGrid"></div>

        <h3>Last API Call</h3>
        <div class="debug-box" id="debugBox">No API calls received yet.</div>
    </div>

</div>

</div>

<script>

const FAN_COUNT = 6;
let applyingModeFromServer = false;
let previousMode = null;

// build fan rows / bubbles once
(function buildUI() {
    const rows = document.getElementById("fanRows");
    const bubbles = document.getElementById("bubbleGrid");

    for (let i = 0; i < FAN_COUNT; i++) {
        rows.innerHTML += `
            <div class="fan-row">
                <label>Fan ${i + 1}</label>
                <span class="current-val" id="fcur${i}">--%</span>
                <input id="f${i}" type="number" min="0" max="100" placeholder="new %">
            </div>`;

        bubbles.innerHTML += `
            <div class="bubble" id="bubble${i}">
                <div class="bubble-inner">
                    <div class="bubble-value" id="bubbleVal${i}">--%</div>
                    <div class="bubble-label">Fan ${i + 1}</div>
                </div>
            </div>`;
    }
})();

function setAll() {
    let v = document.getElementById("allFans").value;
    fetch("/all?value=" + v);
}

function setIndividual() {
    for (let i = 0; i < FAN_COUNT; i++) {
        let v = document.getElementById("f" + i).value;
        if (v === "") continue;
        fetch("/fan?id=" + i + "&value=" + v);
    }
}

function setMode() {
    if (applyingModeFromServer) return;
    const manual = document.getElementById("modeSwitch").checked;
    fetch("/setMode?mode=" + (manual ? "manual" : "api"));
}

function renderStatus(data) {

    // mode switch + label + panel enable/disable
    const manual = data.mode === "manual";
    applyingModeFromServer = true;
    document.getElementById("modeSwitch").checked = manual;
    applyingModeFromServer = false;

    document.getElementById("modeLabel").innerText = manual ? "Manual Control" : "API Controlled";
    document.getElementById("manualPanel").classList.toggle("disabled", !manual);

    // fans: "current" readout always updates live; the input box is only
    // touched (prefilled) the moment you switch INTO manual mode, so typing
    // in it is never interrupted by the 1s status poll.
    const enteringManual = manual && previousMode !== "manual";

    for (let i = 0; i < FAN_COUNT; i++) {
        const pct = data.fans[i];

        document.getElementById("fcur" + i).innerText = pct + "%";
        document.getElementById("bubbleVal" + i).innerText = pct + "%";
        document.getElementById("bubble" + i).style.background =
            `conic-gradient(#43a047 0% ${pct}%, #e0e0e0 ${pct}% 100%)`;

        if (enteringManual) {
            document.getElementById("f" + i).value = pct;
        }
    }

    previousMode = data.mode;

    // temperature
    document.getElementById("temp").innerText = parseFloat(data.temp).toFixed(1);

    // last API call debug box
    const box = document.getElementById("debugBox");
    const call = data.lastApiCall;

    if (!call.hasCall) {
        box.innerText = "No API calls received yet.";
    } else {
        const target = call.fanId === -1 ? "All fans" : ("Fan " + (call.fanId + 1));
        const status = call.applied ? "Applied" : "Ignored (device was in manual mode)";

        box.innerHTML = `
            <div class="row"><span>Time</span><span>${call.time}</span></div>
            <div class="row"><span>Target</span><span>${target}</span></div>
            <div class="row"><span>Value</span><span>${call.value}%</span></div>
            <div class="row"><span>From IP</span><span>${call.ip}</span></div>
            <div class="row"><span>Status</span><span>${status}</span></div>`;
    }
}

// LIVE SYNC FROM ESP32
setInterval(() => {
    fetch("/status")
        .then(res => res.json())
        .then(renderStatus);
}, 1000);

</script>

</body>
</html>
)rawliteral";
}

// --------------------
// ROUTES

void handleRoot() {
    server.send(200, "text/html", htmlPage());
}

// Manual control (from the web UI, only effective in manual mode)

void handleFan() {

    if (currentMode != MODE_MANUAL) {
        server.send(409, "text/plain", "Device is in API mode - switch to manual to use this control");
        return;
    }

    int id = server.arg("id").toInt();
    int value = server.arg("value").toInt();

    if (id >= 0 && id < 6) {
        setFan(id, value);
    }

    server.send(200, "text/plain", "OK");
}

void handleAll() {

    if (currentMode != MODE_MANUAL) {
        server.send(409, "text/plain", "Device is in API mode - switch to manual to use this control");
        return;
    }

    int value = server.arg("value").toInt();
    setAllFans(value);

    server.send(200, "text/plain", "OK");
}

// Mode switch

void handleSetMode() {

    String m = server.arg("mode");

    if (m == "manual") {
        currentMode = MODE_MANUAL;
    } else if (m == "api") {
        currentMode = MODE_API;
    } else {
        server.send(400, "text/plain", "Invalid mode");
        return;
    }

    server.send(200, "text/plain", "OK");
}

// External API (for a server script to call, e.g. based on CPU temperature)

void handleApiFan() {

    if (server.arg("key") != API_KEY) {
        server.send(401, "text/plain", "Unauthorized");
        return;
    }

    int id = server.arg("id").toInt();
    int value = server.arg("value").toInt();

    if (id < 0 || id >= 6) {
        server.send(400, "text/plain", "Invalid fan id");
        return;
    }

    bool applied = applyApiChange(id, value, server.client().remoteIP());

    server.send(200, "text/plain", applied ? "OK" : "IGNORED (device in manual mode)");
}

void handleApiAll() {

    if (server.arg("key") != API_KEY) {
        server.send(401, "text/plain", "Unauthorized");
        return;
    }

    int value = server.arg("value").toInt();

    bool applied = applyApiChange(-1, value, server.client().remoteIP());

    server.send(200, "text/plain", applied ? "OK" : "IGNORED (device in manual mode)");
}

// Status (fans + temperature + mode + last API call)

void handleStatus() {

    String json = "{";

    json += "\"mode\":\"" + modeToString(currentMode) + "\",";

    json += "\"fans\":[";
    for (int i = 0; i < 6; i++) {
        json += String(fanPWM[i]);
        if (i < 5) json += ",";
    }
    json += "],";

    json += "\"temp\":" + String(readChipTemp()) + ",";

    json += "\"lastApiCall\":{";
    json += "\"hasCall\":" + String(lastApiCall.hasCall ? "true" : "false") + ",";
    json += "\"time\":\"" + formatTime(lastApiCall.timestamp) + "\",";
    json += "\"fanId\":" + String(lastApiCall.fanId) + ",";
    json += "\"value\":" + String(lastApiCall.value) + ",";
    json += "\"applied\":" + String(lastApiCall.applied ? "true" : "false") + ",";
    json += "\"ip\":\"" + lastApiCall.ip + "\"";
    json += "}";

    server.send(200, "application/json", json);
}

// --------------------
// SETUP

void setup() {

    Serial.begin(115200);

    // PWM init
    for (int i = 0; i < 6; i++) {

        ledcAttach(fanPins[i], PWM_FREQ, PWM_RESOLUTION);

        setFan(i, DEFAULT_FAN_PERCENT); // startup default
    }

    // WiFi
    WiFi.begin(ssid, password);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }

    Serial.println("\nConnected");
    Serial.println(WiFi.localIP());

    // NTP time sync (needed for real timestamps on the last-API-call / power logs)
    configTzTime(tzInfo, ntpServer1, ntpServer2);

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo, 10000)) {
        Serial.println("NTP sync failed (no internet access?) - timestamps will be wrong until it syncs");
    } else {
        Serial.println("Time synced: " + formatTime(time(nullptr)));
    }

    // Routes
    server.on("/", handleRoot);
    server.on("/fan", handleFan);
    server.on("/all", handleAll);
    server.on("/status", handleStatus);
    server.on("/setMode", handleSetMode);
    server.on("/api/fan", handleApiFan);
    server.on("/api/all", handleApiAll);

    server.begin();
}

// --------------------
// LOOP

void loop() {
    server.handleClient();
}