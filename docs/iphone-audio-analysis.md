# iPhone Audio Integration — Problem Analysis & Implementation Plan

**Platform:** M5Stack CoreS3 (ESP32-S3) · PlatformIO · Arduino framework  
**Date:** 2026-04-16

---

## Problem Analysis

### Why Classic Bluetooth Audio Doesn't Work

The M5Stack CoreS3 uses the **ESP32-S3** chip, which ships with **BLE (Bluetooth Low Energy) only** — Classic Bluetooth is absent from the silicon. This rules out:

| Profile | Purpose | Status |
|---|---|---|
| A2DP | Stereo audio streaming | **Not possible** — requires Classic BT |
| HFP | Hands-free / microphone | **Not possible** — requires Classic BT |
| BLE Audio (LC3) | Next-gen BLE audio | Partial — ESP-IDF 5.x has early support, but iPhone BLE Audio support is also limited and non-trivial to implement |

> The original ESP32 (not S3) does support Classic Bluetooth and A2DP. If audio streaming is the primary goal, swapping to an M5Stack Basic/Gray/Fire (ESP32) is an alternative.

### What the CoreS3 Does Have

- WiFi 802.11 b/g/n (2.4 GHz) via ESP32-S3
- Built-in speaker (driven via I2S, max ~1W, mono)
- I2S peripheral for external DAC/amplifier
- SD card slot (SDMMC, 4-bit mode)
- PSRAM (8 MB) — critical for audio buffering
- M5Unified library with `Speaker` abstraction

---

## Options Summary

| Option | iPhone Side | CoreS3 Side | Difficulty | Quality |
|---|---|---|---|---|
| SD card + browser control | Safari / any browser | HTTP server + I2S playback | Low | Good |
| WiFi HTTP audio stream | VLC or custom app | HTTP chunked receiver | Medium | Medium |
| AirPlay receiver | Native iOS | Reverse-engineered protocol | Very High | High |
| BLE Audio | iOS 16+ (limited) | ESP-IDF BLE Audio stack | High | Unknown stability |

---

## Recommended Option: SD Card + WiFi Web Control

Store audio files on the CoreS3's SD card. iPhone controls playback through a browser-based web interface served by the CoreS3 over WiFi. No app install required on iPhone.

### Architecture

```
iPhone (Safari)
     |
     | HTTP (WiFi LAN or CoreS3 AP mode)
     v
CoreS3 HTTP Server (AsyncWebServer)
     |
     +-- File list API  --> reads SD card index
     +-- Playback API   --> sends commands to audio task
     |
     v
FreeRTOS Audio Task
     |
     v
AudioFileSourceSD --> AudioGeneratorMP3/WAV --> AudioOutputI2S
     |
     v
Built-in Speaker (I2S) or external DAC
```

---

## Detailed Implementation Plan

### Step 1 — Dependencies (`platformio.ini`)

Add to `lib_deps`:

```ini
lib_deps =
    M5Unified=https://github.com/m5stack/M5Unified
    madhephaestus/ESP32Servo
    https://github.com/m5stack/M5CoreS3
    m5stack/M5GFX
    esphome/ESPAsyncWebServer-esphome   ; async HTTP server
    bblanchon/ArduinoJson               ; JSON API responses
    earlephilhower/ESP8266Audio         ; MP3/WAV decoder (works on ESP32-S3)
```

> **Quirk:** The standard `me-no-dev/ESPAsyncWebServer` has known crashes on ESP32-S3 under load. Use `esphome/ESPAsyncWebServer-esphome` — it contains S3-specific fixes.

---

### Step 2 — SD Card Init (CoreS3-Specific)

The CoreS3 uses SDMMC in **4-bit mode** on specific pins. Do **not** use the generic `SD.begin()` — it defaults to SPI mode and will fail or perform poorly.

```cpp
#include <SD.h>
#include <M5CoreS3.h>

// CoreS3 SDMMC pins (from M5CoreS3 schematic)
#define SD_CLK  36
#define SD_CMD  35
#define SD_D0   14
#define SD_D1   17  // 4-bit mode
#define SD_D2   21  // 4-bit mode
#define SD_D3   47  // 4-bit mode (also CS in SPI mode)

bool initSD() {
    // M5Unified initialises the SD card internally if you call
    // M5.begin() with the correct config — prefer this path:
    auto cfg = M5.config();
    cfg.external_spk = false;      // use built-in speaker
    M5.begin(cfg);

    if (!SD.begin(SD_D3, SPI, 25000000)) {
        Serial.println("SD init failed");
        return false;
    }
    return true;
}
```

> **Quirk:** The CoreS3 shares SPI bus lines with the display. Always call `M5.begin()` before touching SD, and initialise SD after the display is up to avoid bus contention.

---

### Step 3 — Audio Playback Task

ESP8266Audio runs its decode loop synchronously. Wrap it in a **dedicated FreeRTOS task pinned to Core 0** (leave Core 1 for WiFi/HTTP). PSRAM must be used for the MP3 decoder buffer or you will get heap allocation failures.

```cpp
#include <AudioFileSourceSD.h>
#include <AudioGeneratorMP3.h>
#include <AudioOutputI2S.h>

// CoreS3 I2S speaker pins
#define I2S_BCLK  34
#define I2S_LRCLK 33
#define I2S_DOUT  13
#define I2S_AMP_EN 12   // speaker amplifier enable GPIO

static AudioFileSourceSD*  audioSrc  = nullptr;
static AudioGeneratorMP3*  mp3       = nullptr;
static AudioOutputI2S*     audioOut  = nullptr;
static volatile bool       playRequested = false;
static String              currentFile   = "";
static SemaphoreHandle_t   audioMutex;

void audioTask(void* param) {
    audioMutex = xSemaphoreCreateMutex();

    audioOut = new AudioOutputI2S();
    audioOut->SetPinout(I2S_BCLK, I2S_LRCLK, I2S_DOUT);
    audioOut->SetGain(0.5f);   // 0.0–1.0; start at 50% to avoid distortion

    // Enable amplifier
    pinMode(I2S_AMP_EN, OUTPUT);
    digitalWrite(I2S_AMP_EN, HIGH);

    while (true) {
        if (playRequested && currentFile.length() > 0) {
            xSemaphoreTake(audioMutex, portMAX_DELAY);
            playRequested = false;
            String file = currentFile;
            xSemaphoreGive(audioMutex);

            if (mp3 && mp3->isRunning()) {
                mp3->stop();
            }
            delete mp3;
            delete audioSrc;

            // Allocate decoder buffer in PSRAM
            audioSrc = new AudioFileSourceSD(file.c_str());
            mp3 = new AudioGeneratorMP3();
            mp3->begin(audioSrc, audioOut);
        }

        if (mp3 && mp3->isRunning()) {
            if (!mp3->loop()) {
                mp3->stop();
            }
        }

        vTaskDelay(1);  // yield, but keep loop tight for smooth audio
    }
}

void startAudioTask() {
    // Pin to Core 0; stack in PSRAM via xTaskCreatePinnedToCore
    xTaskCreatePinnedToCore(audioTask, "audio", 8192, nullptr, 2, nullptr, 0);
}
```

> **Quirk:** The built-in speaker amplifier on CoreS3 has an **enable pin (GPIO 12)**. If you forget `digitalWrite(I2S_AMP_EN, HIGH)` you will hear nothing. M5Unified handles this automatically via `M5.Speaker` — but if you bypass M5Unified for I2S directly, you must toggle it yourself.

> **Quirk:** `vTaskDelay(1)` is critical. Without it the audio task starves the WiFi stack on ESP32-S3 causing disconnects mid-playback.

---

### Step 4 — HTTP Server & API

```cpp
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

AsyncWebServer server(80);

// Build JSON file list from SD
String buildFileList() {
    JsonDocument doc;
    JsonArray arr = doc["files"].to<JsonArray>();
    File root = SD.open("/music");
    if (root && root.isDirectory()) {
        File f = root.openNextFile();
        while (f) {
            if (!f.isDirectory()) {
                String name = f.name();
                if (name.endsWith(".mp3") || name.endsWith(".wav")) {
                    arr.add(name);
                }
            }
            f = root.openNextFile();
        }
    }
    String out;
    serializeJson(doc, out);
    return out;
}

void setupServer() {
    // Serve a minimal web UI from PROGMEM (see Step 5)
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "text/html", PLAYER_HTML);
    });

    // GET /files  → JSON list of tracks
    server.on("/files", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "application/json", buildFileList());
    });

    // POST /play?file=/music/track.mp3
    server.on("/play", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (req->hasParam("file", true)) {
            xSemaphoreTake(audioMutex, portMAX_DELAY);
            currentFile   = req->getParam("file", true)->value();
            playRequested = true;
            xSemaphoreGive(audioMutex);
            req->send(200, "text/plain", "OK");
        } else {
            req->send(400, "text/plain", "Missing 'file' param");
        }
    });

    // POST /stop
    server.on("/stop", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (mp3 && mp3->isRunning()) mp3->stop();
        req->send(200, "text/plain", "OK");
    });

    // POST /volume?v=0.7
    server.on("/volume", HTTP_POST, [](AsyncWebServerRequest* req) {
        if (req->hasParam("v", true)) {
            float vol = req->getParam("v", true)->value().toFloat();
            vol = constrain(vol, 0.0f, 1.0f);
            if (audioOut) audioOut->SetGain(vol);
            req->send(200, "text/plain", "OK");
        }
    });

    server.begin();
}
```

---

### Step 5 — Minimal iPhone-Friendly Web UI

Embed a small HTML page in flash (PROGMEM). It uses fetch() so it works in Safari without any app:

```cpp
const char PLAYER_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>CoreS3 Player</title>
  <style>
    body { font-family: -apple-system, sans-serif; max-width: 420px;
           margin: 0 auto; padding: 16px; background: #111; color: #eee; }
    button { width: 100%; padding: 12px; margin: 6px 0; font-size: 16px;
             border: none; border-radius: 8px; background: #0a84ff; color: white; }
    input[type=range] { width: 100%; }
    #list div { padding: 10px; border-bottom: 1px solid #333; cursor: pointer; }
    #list div:active { background: #222; }
  </style>
</head>
<body>
  <h2>CoreS3 Player</h2>
  <input type="range" id="vol" min="0" max="1" step="0.05" value="0.5"
         oninput="setVol(this.value)">
  <button onclick="stop()">Stop</button>
  <div id="list">Loading...</div>
  <script>
    async function load() {
      const r = await fetch('/files');
      const d = await r.json();
      const el = document.getElementById('list');
      el.innerHTML = '';
      (d.files || []).forEach(f => {
        const div = document.createElement('div');
        div.textContent = f;
        div.onclick = () => play(f);
        el.appendChild(div);
      });
    }
    async function play(f) {
      await fetch('/play', { method:'POST',
        headers:{'Content-Type':'application/x-www-form-urlencoded'},
        body:'file=/music/' + encodeURIComponent(f) });
    }
    async function stop() {
      await fetch('/stop', { method:'POST' });
    }
    function setVol(v) {
      fetch('/volume', { method:'POST',
        headers:{'Content-Type':'application/x-www-form-urlencoded'},
        body:'v=' + v });
    }
    load();
  </script>
</body>
</html>
)rawliteral";
```

---

### Step 6 — WiFi Setup (AP mode recommended for simplicity)

Running the CoreS3 as a **WiFi Access Point** avoids needing a router. iPhone connects directly:

```cpp
#include <WiFi.h>

void setupWiFi() {
    // Option A: Access Point (no router needed)
    WiFi.softAP("CoreS3-Player", "12345678");
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());  // typically 192.168.4.1

    // Option B: Join existing network (uncomment to use)
    // WiFi.begin("YourSSID", "YourPassword");
    // while (WiFi.status() != WL_CONNECTED) delay(500);
    // Serial.println(WiFi.localIP());
}
```

---

### Step 7 — `setup()` and `loop()`

```cpp
void setup() {
    Serial.begin(115200);
    initSD();
    setupWiFi();
    setupServer();
    startAudioTask();

    // Display IP on CoreS3 screen
    M5.Display.setTextSize(2);
    M5.Display.println("CoreS3 Player");
    M5.Display.println(WiFi.softAPIP().toString());
    M5.Display.println("Connect: CoreS3-Player");
}

void loop() {
    M5.update();  // keep M5Unified happy (touch, buttons, power)
    delay(10);
}
```

---

## Known CoreS3 Quirks Checklist

| # | Quirk | Mitigation |
|---|---|---|
| 1 | ESP32-S3 has no Classic Bluetooth | Use WiFi for audio control (this plan) |
| 2 | Speaker amp requires GPIO 12 HIGH | Set manually if bypassing M5Unified Speaker |
| 3 | SD + display share SPI bus | Always call `M5.begin()` before SD init |
| 4 | Heap too small for MP3 decode buffer | Allocate audio objects from PSRAM |
| 5 | Audio task starves WiFi at 100% CPU | Add `vTaskDelay(1)` in audio loop |
| 6 | `ESPAsyncWebServer` (me-no-dev) crashes on S3 | Use `ESPAsyncWebServer-esphome` fork |
| 7 | I2S output volume distorts at gain > 0.8 | Cap `SetGain()` at 0.7–0.75 |
| 8 | PSRAM cache issue on ESP32-S3 | `-mfix-esp32-psram-cache-issue` already in `platformio.ini` ✓ |

---

## File Structure on SD Card

```
/music/
  track01.mp3
  track02.mp3
  ambient.wav
```

Keep filenames ASCII-only (no spaces, no Chinese characters) — the ESP32 FAT driver has known issues with non-ASCII filenames on some SD card brands.

---

## Future Extensions

- **mDNS**: Add `MDNS.begin("cores3")` so iPhone can reach `http://cores3.local` instead of a raw IP
- **Playlist / shuffle**: Track index in NVS (non-volatile storage) so it survives reboots
- **OTA updates**: `ArduinoOTA` can be layered on the same WiFi stack
- **Volume knob**: Map CoreS3 touch slider or physical button to `SetGain()`
