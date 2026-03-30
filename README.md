# The Watcher

> A self-contained network watchdog and incident logger for the **Arduino UNO R4 WiFi**.  
> Power it up and open the IP.

---

## What it does

- Connects to your home Wi-Fi
- Continuously monitors three network layers: router, public internet, and a configurable endpoint
- Classifies every failure by root cause (Wi-Fi loss, router down, ISP down, service down)
- Logs all incidents in onboard RAM with duration and latency data
- Hosts a dark-mode dashboard **served directly from the board**
- Drives the **onboard 12×8 LED matrix** as a multi-page cycling display — health icon, path status bars, latency digits, and signal strength

---

## Hardware

| Item | Notes |
|---|---|
| Arduino UNO R4 WiFi | Only board required |
| USB-C cable | Power and programming |
| Wi-Fi network | 2.4 GHz |

No additional components needed.

---

## Repository structure

```text
the-watcher/
├── TheWatcher.ino   ← complete sketch
└── README.md        ← this file
```

---

## Quick start

### 1. Install board support

Open Arduino IDE → Boards Manager → search **UNO R4** → install the official package.

### 2. Configure credentials

Open `TheWatcher.ino` and edit the configuration block near the top:

```cpp
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASS     = "YOUR_WIFI_PASSWORD";
const char* ENDPOINT_HOST = "connectivitycheck.gstatic.com";
const int   ENDPOINT_PORT = 80;
const char* ENDPOINT_PATH = "/generate_204";
```

### 3. Upload

Select **Arduino UNO R4 WiFi** as your board, select the correct port, and upload.

### 4. Open Serial Monitor

Set baud rate to `115200`. The board will print its assigned IP address once connected:

### 5. Open dashboard

Visit that IP in any browser on the same network. The dashboard refreshes automatically every 5 seconds.

---

## Dashboard

The web UI is served entirely from the board on port 80.

| URL | Returns |
|---|---|
| `http://<board-ip>/` | Live dashboard |
| `http://<board-ip>/status` | Raw JSON data |

### Dashboard panels

- **Health** — healthy/degraded/offline with colour coding
- **Uptime score** — percentage of successful checks since boot
- **Signal strength** — live RSSI in dBm
- **Path health** — router, internet, endpoint status individually
- **Latency** — current, average, and worst internet latency
- **Incident log** — every recorded failure with category and duration

---

## LED matrix

The 12×8 LED matrix cycles through **four pages** every 2.5 seconds, turning the board into an at-a-glance ambient monitor:

| Page | Display | Description |
|---|---|---|
| **Status** | ✓ / △ / ✕ | Health icon — checkmark, warning triangle, or cross |
| **Paths** | Three vertical bars | Router / Internet / Endpoint — full bar = up, stub = down |
| **Latency** | Numeric digits | Internet latency in ms, with a proportional top bar |
| **Signal** | Five ascending bars | WiFi RSSI strength (phone-style indicator) |

Latency digits are rendered using a built-in **3×5 pixel font** (digits 0–9) that can display values up to 9999 on the matrix. If the internet probe times out, two horizontal dashes are shown instead.

---

## Health model

The Watcher monitors three distinct layers to diagnose failures precisely:

| Layer | Check method |
|---|---|
| Router | WiFi connection status + ICMP ping for latency |
| Internet | TCP connect to `1.1.1.1:80` |
| Endpoint | HTTP GET to `connectivitycheck.gstatic.com/generate_204` |

**Health classification:**

| Status | Condition |
|---|---|
| `healthy` | All three layers reachable |
| `degraded` | Router or internet reachable, but not all |
| `offline` | No reachable path |

---

## Incident model

When a failure begins, The Watcher opens an incident in memory.  
When all checks pass again, the incident is closed and stored with:

- **category** — `wifi`, `router`, `internet`, or `endpoint`
- **duration** — total time the failure lasted
- **worst latency** — highest latency observed during the incident
- **timestamps** — milliseconds since boot at start and end

Incidents are stored in a circular buffer of 50 slots. Oldest entries are overwritten when full. All data is cleared on reboot.

---

## Configuration reference

| Constant | Default | Purpose |
|---|---|---|
| `WIFI_SSID` | — | Your Wi-Fi network name |
| `WIFI_PASS` | — | Your Wi-Fi password |
| `ENDPOINT_HOST` | `connectivitycheck.gstatic.com` | Hostname to monitor |
| `ENDPOINT_PORT` | `80` | Port for HTTP probe |
| `ENDPOINT_PATH` | `/generate_204` | Path for HTTP GET |
| `CHECK_INTERVAL_MS` | `15000` | Milliseconds between checks |
| `HTTP_TIMEOUT_MS` | `3000` | Per-check timeout |
| `MAX_LOGS` | `50` | Incident buffer size |

---

## JSON status API

`GET /status` returns a full JSON object. Example:

```json
{
  "health": "healthy",
  "router": true,
  "internet": true,
  "endpoint": true,
  "latRouter": 18,
  "latInternet": 31,
  "latEndpoint": 68,
  "avgInternet": 28,
  "worstInternet": 94,
  "uptimePct": 99.51,
  "uptimeHuman": "0d 03h 22m 44s",
  "ssid": "MyWiFi",
  "ip": "192.168.1.55",
  "gateway": "192.168.1.1",
  "rssi": -47,
  "checks": 824,
  "okChecks": 820,
  "incidentOpen": false,
  "activeCat": "",
  "logs": [
    { "cat": "internet", "dur": 142000, "worst": 3001 }
  ]
}
```

---

## Notes

- **Timestamps** are uptime-based (milliseconds since boot).
- **Router detection** uses WiFi connection status (if associated with the AP, the router is up) combined with ICMP ping (`WiFi.ping()`) for latency measurement. This avoids false negatives from routers that block TCP and ICMP.
- **Internet and endpoint** probes use TCP and HTTP respectively, as these are the most reliable methods for plain connectivity checks without TLS.
- The board has limited RAM. Keep `MAX_LOGS` at 50 or below.
- The sketch is a complete single-file project by design — no headers, no libraries beyond the Arduino UNO R4 core.

---

## Roadmap (Maybe)

- [ ] NTP time sync for real-world timestamps
- [ ] Configuration page in browser
- [ ] Wi-Fi setup via captive portal
- [ ] Latency sparkline history
- [ ] Daily reset counters
- [ ] Adaptive check interval under stress

---
