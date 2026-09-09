#include <WiFi.h>
#include <WebServer.h>

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
}

.header h2 {
    margin: 0;
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
</div>

<div class="split">

    <div class="panel panel-left">
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
        <h3>Fan Overview</h3>
        <div class="temp-line">ESP32 temperature: <span id="temp">0</span> &deg;C</div>

        <div class="bubble-grid" id="bubbleGrid"></div>
    </div>

</div>

</div>

<script>

const FAN_COUNT = 6;
let inputsInitialized = false;

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

function renderStatus(data) {

    // "current" readout always updates live; the input box is only
    // prefilled once, on the very first status response, so typing in
    // it is never interrupted by the 1s status poll.
    for (let i = 0; i < FAN_COUNT; i++) {
        const pct = data.fans[i];

        document.getElementById("fcur" + i).innerText = pct + "%";
        document.getElementById("bubbleVal" + i).innerText = pct + "%";
        document.getElementById("bubble" + i).style.background =
            `conic-gradient(#43a047 0% ${pct}%, #e0e0e0 ${pct}% 100%)`;

        if (!inputsInitialized) {
            document.getElementById("f" + i).value = pct;
        }
    }

    inputsInitialized = true;

    // temperature
    document.getElementById("temp").innerText = parseFloat(data.temp).toFixed(1);
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

void handleFan() {

    int id = server.arg("id").toInt();
    int value = server.arg("value").toInt();

    if (id >= 0 && id < 6) {
        setFan(id, value);
    }

    server.send(200, "text/plain", "OK");
}

void handleAll() {

    int value = server.arg("value").toInt();
    setAllFans(value);

    server.send(200, "text/plain", "OK");
}

// Status (fans + temperature)

void handleStatus() {

    String json = "{";

    json += "\"fans\":[";
    for (int i = 0; i < 6; i++) {
        json += String(fanPWM[i]);
        if (i < 5) json += ",";
    }
    json += "],";

    json += "\"temp\":" + String(readChipTemp());

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

    // Routes
    server.on("/", handleRoot);
    server.on("/fan", handleFan);
    server.on("/all", handleAll);
    server.on("/status", handleStatus);

    server.begin();
}

// --------------------
// LOOP

void loop() {
    server.handleClient();
}
