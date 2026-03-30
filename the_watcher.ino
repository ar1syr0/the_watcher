/*
  The Watcher — Self-contained Network Watchdog & Incident Logger by ar1syr0 x claude

  What it does:
    - Connects to your Wi-Fi
    - Hosts a beautiful dashboard directly from the board
    - Monitors router, public internet, and an optional endpoint
    - Classifies incidents as local / internet / service failures
    - Drives the 12x8 LED matrix as a live ambient indicator
*/

#include <Arduino_LED_Matrix.h>
#include <RTC.h>
#include <WiFiS3.h>

// ─────────────────────────────────────────────────────────────
//  USER CONFIGURATION — edit these before uploading
// ─────────────────────────────────────────────────────────────
const char *WIFI_SSID = "YOUR_SSID";
const char *WIFI_PASS = "YOUR_PASSWORD";
const char *ENDPOINT_HOST = "YOUR_CHOICE_OF_ENDPOINT";
const int ENDPOINT_PORT = 80;
const char *ENDPOINT_PATH = "/generate_204";

// Check every 15 seconds. Reduce to 10 for more responsive detection.
const unsigned long CHECK_INTERVAL_MS = 15000;
// Timeout for each individual check
const unsigned long HTTP_TIMEOUT_MS = 3000;
// Maximum incidents stored in memory (circular buffer)
const int MAX_LOGS = 50;
// ─────────────────────────────────────────────────────────────

ArduinoLEDMatrix matrix;
WiFiServer server(80);

// ── LED matrix icon frames (8×12 pixel arrays) ───────────────
// Checkmark — healthy
uint8_t ICON_OK[8][12] = {
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1}, {1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0},
    {1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0}, {0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0},
    {0, 0, 1, 1, 0, 0, 1, 1, 0, 0, 0, 0}, {0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0, 0}};
// Triangle/warning — degraded
uint8_t ICON_WARN[8][12] = {
    {0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 1, 1, 1, 0, 0, 0, 0, 0},
    {0, 0, 0, 1, 1, 1, 1, 1, 0, 0, 0, 0}, {0, 0, 1, 1, 1, 0, 1, 1, 1, 0, 0, 0},
    {0, 1, 1, 0, 0, 0, 0, 0, 1, 1, 0, 0}, {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0},
    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}, {0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0}};
// X — offline
uint8_t ICON_BAD[8][12] = {
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1}, {0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0},
    {0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0}, {0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0},
    {0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0}, {0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0},
    {0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0}, {0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0}};

// ── Mini 3×5 pixel font for digits 0-9 ───────────────────────
const uint8_t DIGITS[10][5][3] = {
    {{1, 1, 1}, {1, 0, 1}, {1, 0, 1}, {1, 0, 1}, {1, 1, 1}}, // 0
    {{0, 1, 0}, {1, 1, 0}, {0, 1, 0}, {0, 1, 0}, {1, 1, 1}}, // 1
    {{1, 1, 1}, {0, 0, 1}, {1, 1, 1}, {1, 0, 0}, {1, 1, 1}}, // 2
    {{1, 1, 1}, {0, 0, 1}, {1, 1, 1}, {0, 0, 1}, {1, 1, 1}}, // 3
    {{1, 0, 1}, {1, 0, 1}, {1, 1, 1}, {0, 0, 1}, {0, 0, 1}}, // 4
    {{1, 1, 1}, {1, 0, 0}, {1, 1, 1}, {0, 0, 1}, {1, 1, 1}}, // 5
    {{1, 1, 1}, {1, 0, 0}, {1, 1, 1}, {1, 0, 1}, {1, 1, 1}}, // 6
    {{1, 1, 1}, {0, 0, 1}, {0, 0, 1}, {0, 1, 0}, {0, 1, 0}}, // 7
    {{1, 1, 1}, {1, 0, 1}, {1, 1, 1}, {1, 0, 1}, {1, 1, 1}}, // 8
    {{1, 1, 1}, {1, 0, 1}, {1, 1, 1}, {0, 0, 1}, {1, 1, 1}}  // 9
};

// ── Dynamic frame buffer & page cycling ──────────────────────
uint8_t dynamicFrame[8][12];

enum MatrixPage {
  PAGE_STATUS,
  PAGE_PATHS,
  PAGE_LATENCY,
  PAGE_SIGNAL,
  PAGE_COUNT
};
MatrixPage matrixPage = PAGE_STATUS;
unsigned long lastPageSwitch = 0;
const unsigned long PAGE_MS = 2500; // time per page

// ── Health state ──────────────────────────────────────────────
enum HealthState { HEALTH_OK, HEALTH_DEGRADED, HEALTH_OFFLINE };

// ── Incident record ───────────────────────────────────────────
struct Incident {
  unsigned long startMs;
  unsigned long endMs;
  unsigned long durationMs;
  char category[20];
  int worstLatency;
};

Incident incidentLog[MAX_LOGS];
int logHead = 0;
int logCount = 0;

// ── Runtime state ─────────────────────────────────────────────
HealthState currentState = HEALTH_OFFLINE;
bool routerOk = false;
bool internetOk = false;
bool endpointOk = false;

unsigned long lastCheckMs = 0;
unsigned long bootMs = 0;
unsigned long totalChecks = 0;
unsigned long okChecks = 0;

int latRouter = -1;
int latInternet = -1;
int latEndpoint = -1;
int avgInternet = -1;
int worstInternet = -1;

bool incidentOpen = false;
char activeCategory[20] = "";
unsigned long activeStart = 0;
int activeWorst = -1;

String localIp = "";
String gatewayIp = "";
IPAddress gatewayAddr;

// ─────────────────────────────────────────────────────────────
//  LED Matrix helpers — multi-page cycling display
// ─────────────────────────────────────────────────────────────
void matrixOk() { matrix.renderBitmap(ICON_OK, 8, 12); }
void matrixWarn() { matrix.renderBitmap(ICON_WARN, 8, 12); }
void matrixBad() { matrix.renderBitmap(ICON_BAD, 8, 12); }

void clearFrame() { memset(dynamicFrame, 0, sizeof(dynamicFrame)); }

void drawDigit(int d, int x, int y) {
  if (d < 0 || d > 9)
    return;
  for (int r = 0; r < 5; r++)
    for (int c = 0; c < 3; c++) {
      int px = x + c, py = y + r;
      if (px >= 0 && px < 12 && py >= 0 && py < 8)
        dynamicFrame[py][px] = DIGITS[d][r][c];
    }
}

void drawNumber(int val, int y) {
  // Draw up to 4 digits, centered horizontally
  if (val < 0)
    val = 0;
  if (val > 9999)
    val = 9999;
  int digits[4];
  int n = 0;
  int tmp = val;
  if (tmp == 0) {
    digits[0] = 0;
    n = 1;
  } else {
    while (tmp > 0 && n < 4) {
      digits[n++] = tmp % 10;
      tmp /= 10;
    }
  }
  // Reverse order
  for (int i = 0; i < n / 2; i++) {
    int t = digits[i];
    digits[i] = digits[n - 1 - i];
    digits[n - 1 - i] = t;
  }
  int totalW = n * 3 + (n - 1); // 3 per digit + 1 gap
  int x = (12 - totalW) / 2;
  for (int i = 0; i < n; i++) {
    drawDigit(digits[i], x, y);
    x += 4;
  }
}

// Page 1: Path status — three vertical bars (R / I / E)
void buildPathPage() {
  clearFrame();
  bool paths[3] = {routerOk, internetOk, endpointOk};
  int cols[3] = {1, 5, 9}; // center of each bar pair
  for (int b = 0; b < 3; b++) {
    // Dot label at top
    dynamicFrame[0][cols[b]] = 1;
    dynamicFrame[0][cols[b] + 1] = 1;
    // Vertical bar — full height if OK, single bottom pixel if down
    int h = paths[b] ? 6 : 1;
    for (int r = 0; r < h; r++) {
      dynamicFrame[7 - r][cols[b]] = 1;
      dynamicFrame[7 - r][cols[b] + 1] = 1;
    }
  }
  matrix.renderBitmap(dynamicFrame, 8, 12);
}

// Page 2: Latency — show internet latency as digits
void buildLatencyPage() {
  clearFrame();
  int lat = latInternet;
  if (lat < 0) {
    // Timeout indicator — two horizontal dashes
    for (int c = 3; c < 9; c++) {
      dynamicFrame[3][c] = 1;
      dynamicFrame[5][c] = 1;
    }
  } else {
    // Top bar proportional to latency (0-200ms range)
    int barLen = lat * 12 / 200;
    if (barLen < 1)
      barLen = 1;
    if (barLen > 12)
      barLen = 12;
    for (int c = 0; c < barLen; c++)
      dynamicFrame[0][c] = 1;
    // Digits below
    drawNumber(lat, 2);
  }
  // Bottom-right corner dot = "ms" marker
  dynamicFrame[7][11] = 1;
  matrix.renderBitmap(dynamicFrame, 8, 12);
}

// Page 3: WiFi signal strength bars
void buildSignalPage() {
  clearFrame();
  int rssi = WiFi.RSSI();
  int bars = 0;
  if (rssi > -50)
    bars = 5;
  else if (rssi > -60)
    bars = 4;
  else if (rssi > -70)
    bars = 3;
  else if (rssi > -80)
    bars = 2;
  else if (rssi > -90)
    bars = 1;

  // 5 bars, each 2 columns wide, 1 col gap
  // Heights: 2, 3, 4, 5, 6  |  Positions: cols 1-2, 4-5, 7-8, 10-11 ... need 5
  // bars Use 1-col wide bars at cols 1, 3, 5, 7, 9 to fit 5 in 12 cols
  for (int b = 0; b < 5; b++) {
    int height = 2 + b;  // 2,3,4,5,6
    int col = 1 + b * 2; // 1,3,5,7,9
    if (b < bars) {
      for (int r = 0; r < height; r++)
        dynamicFrame[7 - r][col] = 1;
    } else {
      // Dim: just bottom pixel as placeholder
      dynamicFrame[7][col] = 1;
    }
  }
  matrix.renderBitmap(dynamicFrame, 8, 12);
}

// Show the current page
void renderMatrix() {
  switch (matrixPage) {
  case PAGE_STATUS:
    if (currentState == HEALTH_OK)
      matrixOk();
    else if (currentState == HEALTH_DEGRADED)
      matrixWarn();
    else
      matrixBad();
    break;
  case PAGE_PATHS:
    buildPathPage();
    break;
  case PAGE_LATENCY:
    buildLatencyPage();
    break;
  case PAGE_SIGNAL:
    buildSignalPage();
    break;
  default:
    break;
  }
}

// Call from loop() to auto-cycle pages
void updateMatrixPage() {
  if (millis() - lastPageSwitch >= PAGE_MS) {
    lastPageSwitch = millis();
    matrixPage = (MatrixPage)(((int)matrixPage + 1) % PAGE_COUNT);
    renderMatrix();
  }
}

// ─────────────────────────────────────────────────────────────
//  Incident management
// ─────────────────────────────────────────────────────────────
void openIncident(const char *cat, int latency) {
  if (incidentOpen)
    return;
  incidentOpen = true;
  strncpy(activeCategory, cat, sizeof(activeCategory) - 1);
  activeStart = millis();
  activeWorst = latency;
}

void updateWorst(int latency) {
  if (incidentOpen && latency > activeWorst)
    activeWorst = latency;
}

void closeIncident() {
  if (!incidentOpen)
    return;
  Incident &slot = incidentLog[logHead];
  slot.startMs = activeStart;
  slot.endMs = millis();
  slot.durationMs = slot.endMs - slot.startMs;
  strncpy(slot.category, activeCategory, sizeof(slot.category) - 1);
  slot.worstLatency = activeWorst;
  logHead = (logHead + 1) % MAX_LOGS;
  if (logCount < MAX_LOGS)
    logCount++;
  incidentOpen = false;
  activeCategory[0] = '\0';
  activeStart = 0;
  activeWorst = -1;
}

// ─────────────────────────────────────────────────────────────
//  Network probes
// ─────────────────────────────────────────────────────────────
int tcpProbe(IPAddress ip, uint16_t port) {
  WiFiClient c;
  c.setTimeout(HTTP_TIMEOUT_MS / 1000);
  unsigned long t = millis();
  if (!c.connect(ip, port)) {
    c.stop();
    return -1;
  }
  int ms = (int)(millis() - t);
  c.stop();
  return ms;
}

int httpProbe(const char *host, int port, const char *path) {
  WiFiClient c;
  c.setTimeout(HTTP_TIMEOUT_MS / 1000);
  unsigned long t = millis();
  if (!c.connect(host, port)) {
    c.stop();
    return -1;
  }
  c.print(String("GET ") + path + " HTTP/1.1\r\nHost: " + host +
          "\r\nConnection: close\r\n\r\n");
  unsigned long deadline = millis() + HTTP_TIMEOUT_MS;
  while (c.connected() && !c.available() && millis() < deadline) {
  }
  int ms = (int)(millis() - t);
  c.stop();
  return (millis() >= deadline) ? -1 : ms;
}

// ─────────────────────────────────────────────────────────────
//  Health check cycle
// ─────────────────────────────────────────────────────────────
void performChecks() {
  totalChecks++;

  if (WiFi.status() != WL_CONNECTED) {
    routerOk = internetOk = endpointOk = false;
    currentState = HEALTH_OFFLINE;
    openIncident("wifi", -1);
    renderMatrix();
    return;
  }

  // WiFi is connected → router is reachable by definition
  // Ping is just for latency measurement; if router blocks ICMP, use 0
  latRouter = WiFi.ping(gatewayAddr);
  if (latRouter < 0)
    latRouter = 0;
  routerOk = true;

  latInternet = tcpProbe(IPAddress(1, 1, 1, 1), 80);
  latEndpoint = httpProbe(ENDPOINT_HOST, ENDPOINT_PORT, ENDPOINT_PATH);

  internetOk = latInternet >= 0;
  endpointOk = latEndpoint >= 0;

  if (routerOk && internetOk && endpointOk)
    currentState = HEALTH_OK;
  else if (routerOk || internetOk)
    currentState = HEALTH_DEGRADED;
  else
    currentState = HEALTH_OFFLINE;

  renderMatrix();

  if (latInternet >= 0) {
    avgInternet =
        avgInternet < 0 ? latInternet : (avgInternet * 4 + latInternet) / 5;
    if (latInternet > worstInternet)
      worstInternet = latInternet;
  }

  if (currentState == HEALTH_OK) {
    okChecks++;
    closeIncident();
  } else {
    int worst = max(latRouter, max(latInternet, latEndpoint));
    if (!routerOk)
      openIncident("router", worst);
    else if (!internetOk)
      openIncident("internet", worst);
    else
      openIncident("endpoint", worst);
    updateWorst(worst);
  }
}

// ─────────────────────────────────────────────────────────────
//  Helpers for JSON/HTML generation
// ─────────────────────────────────────────────────────────────
String uptimeStr() {
  unsigned long s = (millis() - bootMs) / 1000;
  char buf[40];
  snprintf(buf, sizeof(buf), "%lud %02luh %02lum %02lus", s / 86400,
           (s % 86400) / 3600, (s % 3600) / 60, s % 60);
  return String(buf);
}

String logsJson() {
  String out = "[";
  for (int i = 0; i < logCount; i++) {
    int idx = ((logHead - 1 - i) + MAX_LOGS) % MAX_LOGS;
    if (i > 0)
      out += ",";
    out += "{\"cat\":\"" + String(incidentLog[idx].category) + "\"," +
           "\"dur\":" + String(incidentLog[idx].durationMs) + "," +
           "\"worst\":" + String(incidentLog[idx].worstLatency) + "}";
  }
  return out + "]";
}

String statusJson() {
  float pct = totalChecks ? (100.0f * okChecks / totalChecks) : 0.0f;
  String j = "{";
  j += "\"health\":\"" +
       String(currentState == HEALTH_OK         ? "healthy"
              : currentState == HEALTH_DEGRADED ? "degraded"
                                                : "offline") +
       "\",";
  j += "\"router\":" + String(routerOk ? "true" : "false") + ",";
  j += "\"internet\":" + String(internetOk ? "true" : "false") + ",";
  j += "\"endpoint\":" + String(endpointOk ? "true" : "false") + ",";
  j += "\"latRouter\":" + String(latRouter) + ",";
  j += "\"latInternet\":" + String(latInternet) + ",";
  j += "\"latEndpoint\":" + String(latEndpoint) + ",";
  j += "\"avgInternet\":" + String(avgInternet) + ",";
  j += "\"worstInternet\":" + String(worstInternet) + ",";
  j += "\"uptimePct\":" + String(pct, 2) + ",";
  j += "\"uptimeHuman\":\"" + uptimeStr() + "\",";
  j += "\"ssid\":\"" + String(WiFi.SSID()) + "\",";
  j += "\"ip\":\"" + localIp + "\",";
  j += "\"gateway\":\"" + gatewayIp + "\",";
  j += "\"rssi\":" + String(WiFi.RSSI()) + ",";
  j += "\"checks\":" + String(totalChecks) + ",";
  j += "\"okChecks\":" + String(okChecks) + ",";
  j += "\"incidentOpen\":" + String(incidentOpen ? "true" : "false") + ",";
  j += "\"activeCat\":\"" + String(activeCategory) + "\",";
  j += "\"logs\":" + logsJson();
  j += "}";
  return j;
}

// ─────────────────────────────────────────────────────────────
//  Embedded dashboard HTML
//  Served directly from the board — no CDN, no cloud
// ─────────────────────────────────────────────────────────────
String htmlPage() {
  return R"rawliteral(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>The Watcher</title>
<style>
:root{
  --bg:#07100d;--bg2:#0b1710;--panel:#0e1d15;--panel2:#112018;
  --text:#d2f5e3;--muted:#7aaa93;--line:rgba(100,170,130,.15);
  --green:#55f0a0;--amber:#ffbf69;--red:#ff6b6b;
  --radius:16px;--font:Inter,ui-sans-serif,system-ui,sans-serif;
}
*{box-sizing:border-box;margin:0;padding:0}
html{color-scheme:dark}
body{
  min-height:100dvh;font-family:var(--font);font-size:16px;
  color:var(--text);line-height:1.5;
  background:
    radial-gradient(ellipse 80% 40% at 50% -10%, rgba(85,240,160,.07), transparent),
    linear-gradient(180deg,var(--bg),var(--bg2));
}
.wrap{max-width:1060px;margin:0 auto;padding:clamp(16px,4vw,32px)}
header{
  display:flex;justify-content:space-between;align-items:flex-end;
  flex-wrap:wrap;gap:12px;padding-block-end:20px;
  border-bottom:1px solid var(--line);margin-block-end:20px
}
h1{font-size:clamp(22px,4vw,34px);letter-spacing:-.03em;font-weight:700}
h1 span{color:var(--green)}
.sub{color:var(--muted);font-size:14px}
.badge{
  padding:8px 14px;border-radius:999px;border:1px solid var(--line);
  font-size:13px;font-family:ui-monospace,monospace;
  background:rgba(255,255,255,.02);transition:color .3s
}
.grid{display:grid;grid-template-columns:repeat(12,1fr);gap:14px}
.card{
  grid-column:span 12;border-radius:var(--radius);
  background:linear-gradient(180deg,rgba(255,255,255,.025),rgba(255,255,255,.01));
  border:1px solid var(--line);padding:18px;
  box-shadow:0 8px 24px rgba(0,0,0,.25)
}
@media(min-width:620px){.s6{grid-column:span 6}}
@media(min-width:900px){.s4{grid-column:span 4}.s8{grid-column:span 8}}
.kicker{font-size:11px;letter-spacing:.12em;text-transform:uppercase;color:var(--muted);margin-block-end:10px}
.big{font-size:clamp(26px,5vw,42px);font-weight:700;letter-spacing:-.04em;font-variant-numeric:tabular-nums}
.row{display:flex;justify-content:space-between;align-items:center;gap:12px}
.path-item{
  display:flex;justify-content:space-between;align-items:center;
  padding:11px 14px;border-radius:12px;
  background:var(--panel2);border:1px solid var(--line);margin-block-end:8px
}
.path-item:last-child{margin-block-end:0}
.dot{width:9px;height:9px;border-radius:50%;flex-shrink:0}
.ok{background:var(--green)}
.warn{background:var(--amber)}
.bad{background:var(--red)}
.mono{font-family:ui-monospace,SFMono-Regular,monospace;font-size:13px}
.pill{display:inline-flex;align-items:center;gap:6px;padding:4px 10px;border-radius:999px;font-size:12px;background:rgba(255,255,255,.05)}
.bar-wrap{height:8px;border-radius:999px;background:rgba(255,255,255,.06);overflow:hidden;margin-block:10px 6px}
.bar-fill{height:100%;border-radius:999px;background:linear-gradient(90deg,var(--green),#9affc8);transition:width .6s ease}
.log-list{display:grid;gap:8px;max-height:400px;overflow-y:auto;scrollbar-width:thin}
.log{padding:12px 14px;border-radius:12px;background:var(--panel2);border:1px solid var(--line)}
.log-title{font-weight:600;margin-block-end:4px}
footer{margin-block-start:20px;color:var(--muted);font-size:12px;text-align:center}
</style>
</head>
<body>
<div class="wrap">
  <header>
    <div>
      <h1>The <span>Watcher</span></h1>
      <p class="sub">Self-contained network watchdog &amp; incident logger · by ar1syr0 | using: UNO R4 WiFi</p>
    </div>
    <div class="badge" id="topBadge">Connecting…</div>
  </header>

  <div class="grid">
    <section class="card s4">
      <div class="kicker">Health</div>
      <div class="big" id="health">—</div>
      <div class="mono" style="color:var(--muted);margin-top:6px" id="ssid">—</div>
    </section>

    <section class="card s4">
      <div class="kicker">Uptime score</div>
      <div class="big" id="uptimePct">—</div>
      <div class="bar-wrap"><div class="bar-fill" id="uptimeBar" style="width:0%"></div></div>
      <div class="mono" style="color:var(--muted)" id="uptimeHuman">—</div>
    </section>

    <section class="card s4">
      <div class="kicker">Signal strength</div>
      <div class="big" id="rssi">—</div>
      <div class="mono" style="color:var(--muted)">dBm</div>
    </section>

    <section class="card s6">
      <div class="kicker">Path health</div>
      <div id="pathHealth"></div>
    </section>

    <section class="card s6">
      <div class="kicker">Latency</div>
      <div id="latency"></div>
    </section>

    <section class="card">
      <div class="kicker">Incident log</div>
      <div class="log-list" id="logs">
        <div class="log"><div style="color:var(--muted)">No incidents recorded yet.</div></div>
      </div>
    </section>
  </div>

  <footer class="mono">Served from the board ~ ar1syr0 ~</footer>
</div>

<script>
const fmt = v => v < 0 ? 'timeout' : `${v} ms`;
const dur = ms => {
  const s=Math.floor(ms/1000),h=Math.floor(s/3600),m=Math.floor((s%3600)/60),ss=s%60;
  return h?`${h}h ${m}m ${ss}s`:m?`${m}m ${ss}s`:`${ss}s`;
};
const dot = ok => `<span class="dot ${ok===true?'ok':ok===false?'bad':'warn'}"></span>`;

function pathRow(label, ok, val) {
  return `<div class="path-item">${dot(ok)} <span>${label}</span><span class="mono">${val}</span></div>`;
}

async function refresh() {
  try {
    const d = await fetch('/status').then(r => r.json());
    const healthy = d.health === 'healthy';
    const degraded = d.health === 'degraded';
    const color = healthy ? 'var(--green)' : degraded ? 'var(--amber)' : 'var(--red)';

    document.getElementById('topBadge').textContent = `${d.health.toUpperCase()} · ${d.ip}`;
    document.getElementById('topBadge').style.color = color;
    document.getElementById('health').textContent = d.health;
    document.getElementById('health').style.color = color;
    document.getElementById('ssid').textContent = `${d.ssid} · ${d.ip}`;
    document.getElementById('uptimePct').textContent = `${d.uptimePct.toFixed(1)}%`;
    document.getElementById('uptimeBar').style.width = `${Math.min(100, d.uptimePct)}%`;
    document.getElementById('uptimeHuman').textContent = d.uptimeHuman;
    document.getElementById('rssi').textContent = d.rssi;
    document.getElementById('ipFooter').textContent = d.ip;

    document.getElementById('pathHealth').innerHTML =
      pathRow('Router',   d.router,   d.router   ? 'reachable' : 'down') +
      pathRow('Internet', d.internet, d.internet ? 'reachable' : 'down') +
      pathRow('Endpoint', d.endpoint, d.endpoint ? 'reachable' : 'down');

    document.getElementById('latency').innerHTML =
      pathRow('Router',           true, fmt(d.latRouter))   +
      pathRow('Internet',         true, fmt(d.latInternet)) +
      pathRow('Endpoint',         true, fmt(d.latEndpoint)) +
      pathRow('Avg internet',     true, fmt(d.avgInternet)) +
      pathRow('Worst internet',   true, fmt(d.worstInternet));

    const logs = d.logs || [];
    document.getElementById('logs').innerHTML = logs.length
      ? logs.map(l => `
          <article class="log">
            <div class="row">
              <div class="log-title">${l.cat}</div>
              <span class="pill mono">${dur(l.dur)}</span>
            </div>
            <div class="mono" style="color:var(--muted)">Worst latency: ${l.worst} ms</div>
          </article>`).join('')
      : `<div class="log"><div style="color:var(--muted)">No incidents recorded yet.</div></div>`;
  } catch(e) {
    document.getElementById('topBadge').textContent = 'Reconnecting…';
  }
}
refresh();
setInterval(refresh, 5000);
</script>
</body>
</html>)rawliteral";
}

// ─────────────────────────────────────────────────────────────
//  HTTP server
// ─────────────────────────────────────────────────────────────
void handleClient(WiFiClient client) {
  String req = "";
  unsigned long t = millis();
  while (client.connected() && millis() - t < 1000) {
    while (client.available()) {
      req += (char)client.read();
      if (req.endsWith("\r\n\r\n"))
        goto done;
    }
  }
done:
  bool isStatus = req.indexOf("GET /status") >= 0;
  String body = isStatus ? statusJson() : htmlPage();
  String ct = isStatus ? "application/json" : "text/html; charset=utf-8";
  client.print("HTTP/1.1 200 OK\r\nContent-Type: " + ct +
               "\r\nConnection: close\r\nContent-Length: " + body.length() +
               "\r\n\r\n");
  client.print(body);
  delay(1);
  client.stop();
}

// ─────────────────────────────────────────────────────────────
//  Setup & loop
// ─────────────────────────────────────────────────────────────
void connectWiFi() {
  matrixWarn();
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long t = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t < 20000)
    delay(300);
  if (WiFi.status() == WL_CONNECTED) {
    localIp = WiFi.localIP().toString();
    gatewayAddr = WiFi.gatewayIP();
    gatewayIp = gatewayAddr.toString();
    Serial.print("\n[The Watcher] Connected. Open http://");
    Serial.println(localIp);
  } else {
    matrixBad();
    Serial.println("[The Watcher] Wi-Fi connection failed.");
  }
}

void setup() {
  bootMs = millis();
  Serial.begin(115200);
  delay(1000);
  matrix.begin();
  RTC.begin();
  connectWiFi();
  server.begin();
  Serial.println("[The Watcher] Web server running.");
}

void loop() {
  if (WiFi.status() != WL_CONNECTED)
    connectWiFi();

  if (millis() - lastCheckMs >= CHECK_INTERVAL_MS) {
    lastCheckMs = millis();
    performChecks();
  }

  // Cycle the LED matrix through info pages
  updateMatrixPage();

  WiFiClient client = server.available();
  if (client)
    handleClient(client);
}