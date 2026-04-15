# M5 CoreS3 无线音频音箱实现方案

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 M5 CoreS3 实现为无线音频播放设备，提供完整的硬件分析、限制说明、替代方案及代码实现。

**Architecture:** 由于 ESP32-S3 不支持经典蓝牙（BR/EDR），无法直接使用 A2DP 协议。本方案采用 Wi-Fi 音频流（HTTP/WebSocket）作为主要实现路径，同时提供外部蓝牙模块扩展和 USB 音频两种备选方案。音频输出通过板载 I2S 功放 AW88298 驱动内置扬声器。

**Tech Stack:** ESP32-S3 / Arduino / M5Unified / I2S (AW88298) / Wi-Fi HTTP Audio Streaming / ESP32-A2DP (仅限外部模块方案)

---

## 第一部分：硬件技术分析

### 1.1 M5 CoreS3 音频硬件配置

| 组件 | 型号 | 功能 | 规格 |
|------|------|------|------|
| 主控芯片 | ESP32-S3 | 处理器+无线通信 | 双核 LX7 @ 240MHz, 16MB Flash, 8MB PSRAM |
| 音频功放 | AW88298 | I2S 功放输出 | 16-bit, I2C控制 (0x36), 1W输出 |
| 音频编解码 | ES7210 | 麦克风输入 | 双麦克风, I2C控制 (0x40) |
| 扬声器 | 内置 | 声音输出 | 1W |
| I2S引脚 | - | 音频数据传输 | BCK=G34, WS(LRCK)=G33, DOUT=G13, DIN=G14, MCLK=G0 |

### 1.2 I2S 引脚映射（来自 M5 官方文档）

```
麦克风 & 功放引脚映射:
ESP32-S3    →  ES7210 (0x40)    →  AW88298 (0x36)
G12 (SDA)   →  I2C_SYS_SDA      →  I2C_SYS_SDA
G11 (SCL)   →  I2C_SYS_SCL      →  I2C_SYS_SCL
G34         →  I2S_BCK           →  I2S_BCK
G33         →  I2S_WCK           →  I2S_WCK
G13         →  I2S_DATO (输出)   →  -
G14         →  I2S_DIN (输入)    →  -
G0          →  I2S_MCLK          →  -
AW9523B P0_2→  -                 →  AW_RST
AW9523B P1_3→  -                 →  AW_INT
```

**关键发现：ES7210 和 AW88298 共享 I2S_BCK (G34) 和 I2S_WCK (G33) 引脚。AW88298 的数据输入引脚 (I2S_DATI) 与 ES7210 的数据输出引脚 (I2S_DATO) 是分开的。**

### 1.3 蓝牙硬件限制（⚠️ 核心问题）

**ESP32-S3 仅支持 BLE 5.0，不支持经典蓝牙 (BR/EDR)。**

证据来源：
1. ESP32-S3 官方数据手册：*"ESP32-S3 is a low-power MCU-based system on a chip (SoC) with integrated 2.4 GHz Wi-Fi and Bluetooth® Low Energy (Bluetooth LE)."*
2. ESP32-A2DP 库官方声明：*"The esp32-s2, esp32-s3, esp32-c2, esp32-c3 and other variants do not support Classic Bluetooth, so A2DP is not possible."*
3. ESP-IDF A2DP 示例仅支持 ESP32（原版），不支持 ESP32-S3
4. 多个社区确认来源

**这意味着 M5 CoreS3 无法直接使用以下经典蓝牙协议：**
- ❌ A2DP (Advanced Audio Distribution Profile) - 音频流传输
- ❌ HSP (Headset Profile) - 耳机功能
- ❌ HFP (Hands-Free Profile) - 免提通话
- ❌ AVRCP (Audio/Video Remote Control Profile) - 音频遥控
- ❌ SPP (Serial Port Profile) - 串口通信

**BLE 音频也不可行：**
- LE Audio 需要 BLE 5.2+，ESP32-S3 仅支持 BLE 5.0
- ESP-IDF 官方标记 LE Audio 支持请求为 "Won't Do"

---

## 第二部分：替代方案设计

### 方案对比

| 方案 | 可行性 | 音质 | 延迟 | 开发难度 | 成本 |
|------|--------|------|------|----------|------|
| A: Wi-Fi HTTP 音频流 | ✅ 高 | 中高 | 200-500ms | 中 | 零 |
| B: 外部蓝牙模块扩展 | ✅ 高 | 高 | 50-150ms | 高 | ¥15-30 |
| C: USB 音频 (UAC2) | ⚠️ 实验性 | 高 | <10ms | 极高 | 零 |
| D: AirPlay 接收器 | ✅ 中 | 高 | 1-2s | 高 | 零 |

---

## 第三部分：方案A - Wi-Fi HTTP 音频流（推荐）

### 3.1 架构设计

```
┌──────────────┐         Wi-Fi HTTP          ┌──────────────────┐
│   音频源      │ ──────────────────────────→ │  M5 CoreS3       │
│  (手机/PC)   │   PCM/WAV 音频数据流         │                  │
│              │ ←────────────────────────── │  ESP32-S3        │
│              │     HTTP 控制指令             │  AW88298 功放    │
└──────────────┘                              │  内置扬声器      │
                                              └──────────────────┘
```

### 3.2 文件结构

```
src/
├── main.cpp                          # 主入口，整合所有模块
├── modules/
│   ├── CameraManager.h/cpp           # (现有) 摄像头管理
│   ├── FaceDetector.h/cpp            # (现有) 人脸检测
│   ├── ServoController.h/cpp         # (现有) 舵机控制
│   ├── DisplayManager.h/cpp          # (现有) 显示管理
│   ├── AudioOutput.h                 # (新建) 音频输出管理器头文件
│   ├── AudioOutput.cpp               # (新建) 音频输出管理器实现
│   ├── WiFiAudioServer.h             # (新建) Wi-Fi音频服务器头文件
│   ├── WiFiAudioServer.cpp           # (新建) Wi-Fi音频服务器实现
│   └── BTSpeakerUI.h/cpp             # (新建) 蓝牙音箱UI界面
```

### 3.3 platformio.ini 修改

需要在现有配置基础上添加音频相关依赖：

```ini
[env:m5stack-cores3]
platform = espressif32@6.7.0
board = esp32-s3-devkitc-1
framework = arduino
upload_speed = 1500000
monitor_speed = 115200
build_flags =
    -DESP32S3
    -DBOARD_HAS_PSRAM
    -mfix-esp32-psram-cache-issue
    -DCORE_DEBUG_LEVEL=5
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1
lib_deps =
    M5Unified=https://github.com/m5stack/M5Unified
    madhephaestus/ESP32Servo
    https://github.com/m5stack/M5CoreS3
    m5stack/M5GFX
    bblanchon/ArduinoJson@^7.0.0
    mathieucarbou/ESPAsyncWebServer@^3.1.5
    rlogiacco/CircularBuffer@^1.3.3
```

---

### Task 1: 创建 AudioOutput 模块

**Files:**
- Create: `src/modules/AudioOutput.h`
- Create: `src/modules/AudioOutput.cpp`

- [ ] **Step 1: 创建 AudioOutput.h 头文件**

```cpp
#ifndef AUDIO_OUTPUT_H
#define AUDIO_OUTPUT_H

#include <Arduino.h>
#include <driver/i2s.h>

class AudioOutput {
public:
    static constexpr int I2S_BCK = 34;
    static constexpr int I2S_WS = 33;
    static constexpr int I2S_DOUT = 13;
    static constexpr int I2S_MCLK = 0;
    static constexpr int SAMPLE_RATE = 44100;
    static constexpr int BITS_PER_SAMPLE = 16;
    static constexpr int DMA_BUF_COUNT = 8;
    static constexpr int DMA_BUF_LEN = 256;

    AudioOutput();
    ~AudioOutput();

    bool init();
    void deinit();
    size_t write(const uint8_t* data, size_t len);
    void setVolume(uint8_t volume);
    uint8_t getVolume() const;
    void mute();
    void unmute();
    bool isMuted() const;

private:
    bool m_initialized;
    uint8_t m_volume;
    bool m_muted;
    i2s_port_t m_i2sPort;

    bool initI2S();
    void configureAW88298();
};

#endif
```

- [ ] **Step 2: 创建 AudioOutput.cpp 实现文件**

```cpp
#include "AudioOutput.h"
#include <M5CoreS3.h>
#include <Wire.h>

AudioOutput::AudioOutput()
    : m_initialized(false)
    , m_volume(80)
    , m_muted(false)
    , m_i2sPort(I2S_NUM_0) {
}

AudioOutput::~AudioOutput() {
    deinit();
}

bool AudioOutput::init() {
    if (m_initialized) {
        return true;
    }

    configureAW88298();

    if (!initI2S()) {
        Serial.println("[AudioOutput] I2S init failed");
        return false;
    }

    m_initialized = true;
    Serial.println("[AudioOutput] Initialized successfully");
    return true;
}

void AudioOutput::deinit() {
    if (m_initialized) {
        i2s_driver_uninstall(m_i2sPort);
        m_initialized = false;
    }
}

bool AudioOutput::initI2S() {
    i2s_config_t i2sConfig = {};
    i2sConfig.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
    i2sConfig.sample_rate = SAMPLE_RATE;
    i2sConfig.bits_per_sample = (i2s_bits_per_sample_t)BITS_PER_SAMPLE;
    i2sConfig.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    i2sConfig.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    i2sConfig.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    i2sConfig.dma_buf_count = DMA_BUF_COUNT;
    i2sConfig.dma_buf_len = DMA_BUF_LEN;
    i2sConfig.use_apll = false;
    i2sConfig.tx_desc_auto_clear = true;
    i2sConfig.fixed_mclk = 0;

    i2s_pin_config_t pinConfig = {};
    pinConfig.bck_io_num = I2S_BCK;
    pinConfig.ws_io_num = I2S_WS;
    pinConfig.data_out_num = I2S_DOUT;
    pinConfig.data_in_num = I2S_PIN_NO_CHANGE;
    pinConfig.mck_io_num = I2S_MCLK;

    esp_err_t err = i2s_driver_install(m_i2sPort, &i2sConfig, 0, nullptr);
    if (err != ESP_OK) {
        Serial.printf("[AudioOutput] i2s_driver_install failed: 0x%x\n", err);
        return false;
    }

    err = i2s_set_pin(m_i2sPort, &pinConfig);
    if (err != ESP_OK) {
        Serial.printf("[AudioOutput] i2s_set_pin failed: 0x%x\n", err);
        i2s_driver_uninstall(m_i2sPort);
        return false;
    }

    i2s_zero_dma_buffer(m_i2sPort);
    return true;
}

void AudioOutput::configureAW88298() {
    Wire.beginTransmission(0x36);
    Wire.write(0x00);
    Wire.endTransmission();
    Serial.println("[AudioOutput] AW88298 configured via I2C");
}

size_t AudioOutput::write(const uint8_t* data, size_t len) {
    if (!m_initialized || m_muted) {
        return 0;
    }

    size_t bytesWritten = 0;
    esp_err_t err = i2s_write(m_i2sPort, data, len, &bytesWritten, portMAX_DELAY);
    if (err != ESP_OK) {
        return 0;
    }
    return bytesWritten;
}

void AudioOutput::setVolume(uint8_t volume) {
    m_volume = (volume > 100) ? 100 : volume;
}

uint8_t AudioOutput::getVolume() const {
    return m_volume;
}

void AudioOutput::mute() {
    m_muted = true;
    if (m_initialized) {
        i2s_zero_dma_buffer(m_i2sPort);
    }
}

void AudioOutput::unmute() {
    m_muted = false;
}

bool AudioOutput::isMuted() const {
    return m_muted;
}
```

- [ ] **Step 3: 验证编译通过**

Run: `cd /Users/mac/Desktop/ent2 && pio run -e m5stack-cores3`
Expected: BUILD SUCCESS（可能因缺少其他新模块而失败，需逐步添加）

---

### Task 2: 创建 WiFiAudioServer 模块

**Files:**
- Create: `src/modules/WiFiAudioServer.h`
- Create: `src/modules/WiFiAudioServer.cpp`

- [ ] **Step 1: 创建 WiFiAudioServer.h 头文件**

```cpp
#ifndef WIFI_AUDIO_SERVER_H
#define WIFI_AUDIO_SERVER_H

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include "AudioOutput.h"

class WiFiAudioServer {
public:
    static constexpr uint16_t HTTP_PORT = 80;
    static constexpr uint16_t AUDIO_PORT = 8080;
    static constexpr size_t AUDIO_BUFFER_SIZE = 4096;

    enum class State {
        IDLE,
        CONNECTING,
        STREAMING,
        ERROR
    };

    WiFiAudioServer(AudioOutput& audioOutput);
    ~WiFiAudioServer();

    bool init(const char* ssid, const char* password);
    void start();
    void stop();
    void update();

    State getState() const;
    const char* getIPAddress() const;
    uint32_t getBytesReceived() const;
    uint32_t getBufferUnderruns() const;

private:
    AudioOutput& m_audioOutput;
    AsyncWebServer m_webServer;
    WiFiServer m_audioServer;
    WiFiClient m_audioClient;

    State m_state;
    char m_ipAddress[16];
    uint32_t m_bytesReceived;
    uint32_t m_bufferUnderruns;
    bool m_serverRunning;

    uint8_t* m_audioBuffer;

    void setupWebPages();
    void handleAudioConnection();
    void setState(State state);

    static const char PAGE_INDEX[];
    static const char PAGE_PLAYER[];
};

#endif
```

- [ ] **Step 2: 创建 WiFiAudioServer.cpp 实现文件**

```cpp
#include "WiFiAudioServer.h"

const char WiFiAudioServer::PAGE_INDEX[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>M5 CoreS3 Speaker</title>
<style>
body{font-family:Arial,sans-serif;max-width:600px;margin:0 auto;padding:20px;background:#1a1a2e;color:#eee}
h1{color:#e94560;text-align:center}
.card{background:#16213e;border-radius:12px;padding:20px;margin:16px 0;box-shadow:0 4px 12px rgba(0,0,0,0.3)}
.status{text-align:center;font-size:24px;margin:20px 0}
.btn{display:inline-block;padding:12px 24px;margin:8px;border:none;border-radius:8px;font-size:16px;cursor:pointer;color:#fff}
.btn-play{background:#0f3460}.btn-stop{background:#e94560}
.btn-vol{background:#533483}
input[type=range]{width:100%;margin:12px 0}
</style>
</head>
<body>
<h1>M5 CoreS3 Speaker</h1>
<div class="card">
<div class="status" id="status">Idle</div>
</div>
<div class="card">
<h3>Audio Upload</h3>
<input type="file" id="audioFile" accept="audio/wav,audio/pcm,audio/x-wav">
<button class="btn btn-play" onclick="playFile()">Play</button>
<button class="btn btn-stop" onclick="stopPlay()">Stop</button>
</div>
<div class="card">
<h3>Volume</h3>
<input type="range" id="volume" min="0" max="100" value="80" oninput="setVolume(this.value)">
<p>Volume: <span id="volVal">80</span>%</p>
</div>
<script>
function setVolume(v){document.getElementById('volVal').v;fetch('/volume?v='+v)}
function playFile(){
  var f=document.getElementById('audioFile').files[0];
  if(!f){alert('Select a file first');return}
  document.getElementById('status').textContent='Streaming...';
  var reader=new FileReader();
  reader.onload=function(e){
    fetch('/audio',{method:'POST',body:e.target.result})
    .then(r=>{document.getElementById('status').textContent='Playing'})
    .catch(e=>{document.getElementById('status').textContent='Error'})
  };
  reader.readAsArrayBuffer(f)
}
function stopPlay(){
  fetch('/stop').then(r=>{document.getElementById('status').textContent='Idle'})
}
</script>
</body>
</html>
)rawliteral";

WiFiAudioServer::WiFiAudioServer(AudioOutput& audioOutput)
    : m_audioOutput(audioOutput)
    , m_webServer(HTTP_PORT)
    , m_audioServer(AUDIO_PORT)
    , m_state(State::IDLE)
    , m_bytesReceived(0)
    , m_bufferUnderruns(0)
    , m_serverRunning(false)
    , m_audioBuffer(nullptr) {
    m_ipAddress[0] = '\0';
}

WiFiAudioServer::~WiFiAudioServer() {
    stop();
    if (m_audioBuffer) {
        free(m_audioBuffer);
    }
}

bool WiFiAudioServer::init(const char* ssid, const char* password) {
    m_audioBuffer = (uint8_t*)malloc(AUDIO_BUFFER_SIZE);
    if (!m_audioBuffer) {
        Serial.println("[WiFiAudioServer] Buffer alloc failed");
        return false;
    }

    Serial.printf("[WiFiAudioServer] Connecting to %s", ssid);
    WiFi.begin(ssid, password);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 40) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("\n[WiFiAudioServer] WiFi connect failed");
        setState(State::ERROR);
        return false;
    }

    snprintf(m_ipAddress, sizeof(m_ipAddress), "%s", WiFi.localIP().toString().c_str());
    Serial.printf("\n[WiFiAudioServer] Connected! IP: %s\n", m_ipAddress);

    setupWebPages();
    return true;
}

void WiFiAudioServer::setupWebPages() {
    m_webServer.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send_P(200, "text/html", PAGE_INDEX);
    });

    m_webServer.on("/volume", HTTP_GET, [this](AsyncWebServerRequest* request) {
        if (request->hasParam("v")) {
            int vol = request->getParam("v")->value().toInt();
            m_audioOutput.setVolume((uint8_t)constrain(vol, 0, 100));
        }
        request->send(200, "text/plain", "OK");
    });

    m_webServer.on("/stop", HTTP_GET, [this](AsyncWebServerRequest* request) {
        m_audioOutput.mute();
        setState(State::IDLE);
        request->send(200, "text/plain", "OK");
    });

    m_webServer.on("/audio", HTTP_POST, [this](AsyncWebServerRequest* request) {
        request->send(200, "text/plain", "OK");
    }, nullptr, [this](AsyncWebServerRequest* request, uint8_t* data, size_t len, size_t index, size_t total) {
        if (index == 0) {
            m_audioOutput.unmute();
            setState(State::STREAMING);
            m_bytesReceived = 0;
        }

        size_t written = m_audioOutput.write(data, len);
        m_bytesReceived += written;

        if (index + len >= total) {
            setState(State::IDLE);
        }
    });

    m_webServer.on("/status", HTTP_GET, [this](AsyncWebServerRequest* request) {
        String json = "{";
        json += "\"state\":" + String(static_cast<int>(m_state)) + ",";
        json += "\"bytes\":" + String(m_bytesReceived) + ",";
        json += "\"underruns\":" + String(m_bufferUnderruns) + ",";
        json += "\"volume\":" + String(m_audioOutput.getVolume());
        json += "}";
        request->send(200, "application/json", json);
    });
}

void WiFiAudioServer::start() {
    if (m_serverRunning) return;

    m_webServer.begin();
    m_audioServer.begin();
    m_serverRunning = true;
    Serial.println("[WiFiAudioServer] Server started");
}

void WiFiAudioServer::stop() {
    if (!m_serverRunning) return;

    m_webServer.end();
    m_audioServer.stop();
    if (m_audioClient) {
        m_audioClient.stop();
    }
    m_serverRunning = false;
    setState(State::IDLE);
    Serial.println("[WiFiAudioServer] Server stopped");
}

void WiFiAudioServer::update() {
    if (!m_serverRunning) return;

    handleAudioConnection();
}

void WiFiAudioServer::handleAudioConnection() {
    if (!m_audioClient || !m_audioClient.connected()) {
        WiFiClient newClient = m_audioServer.available();
        if (newClient) {
            m_audioClient = newClient;
            m_audioOutput.unmute();
            setState(State::STREAMING);
            m_bytesReceived = 0;
            Serial.println("[WiFiAudioServer] Audio client connected");
        }
        return;
    }

    int available = m_audioClient.available();
    if (available > 0) {
        size_t toRead = min((size_t)available, AUDIO_BUFFER_SIZE);
        size_t bytesRead = m_audioClient.read(m_audioBuffer, toRead);
        if (bytesRead > 0) {
            m_audioOutput.write(m_audioBuffer, bytesRead);
            m_bytesReceived += bytesRead;
        }
    }

    if (!m_audioClient.connected()) {
        m_audioClient.stop();
        m_audioOutput.mute();
        setState(State::IDLE);
        Serial.println("[WiFiAudioServer] Audio client disconnected");
    }
}

WiFiAudioServer::State WiFiAudioServer::getState() const {
    return m_state;
}

const char* WiFiAudioServer::getIPAddress() const {
    return m_ipAddress;
}

uint32_t WiFiAudioServer::getBytesReceived() const {
    return m_bytesReceived;
}

uint32_t WiFiAudioServer::getBufferUnderruns() const {
    return m_bufferUnderruns;
}

void WiFiAudioServer::setState(State state) {
    m_state = state;
}
```

- [ ] **Step 3: 验证编译**

Run: `cd /Users/mac/Desktop/ent2 && pio run -e m5stack-cores3`

---

### Task 3: 创建 BTSpeakerUI 模块

**Files:**
- Create: `src/modules/BTSpeakerUI.h`
- Create: `src/modules/BTSpeakerUI.cpp`

- [ ] **Step 1: 创建 BTSpeakerUI.h 头文件**

```cpp
#ifndef BT_SPEAKER_UI_H
#define BT_SPEAKER_UI_H

#include <Arduino.h>
#include <M5CoreS3.h>
#include "WiFiAudioServer.h"

class BTSpeakerUI {
public:
    BTSpeakerUI();
    ~BTSpeakerUI();

    bool init();
    void update(WiFiAudioServer::State state, uint8_t volume, uint32_t bytesReceived);
    void drawStartupScreen();
    void drawConnectingScreen(const char* ssid);
    void drawReadyScreen(const char* ipAddress);
    void drawStreamingScreen(uint32_t bytesReceived);
    void drawErrorScreen(const char* message);

private:
    static constexpr uint16_t COLOR_BG = 0x1A1A;
    static constexpr uint16_t COLOR_ACCENT = 0xF9E6;
    static constexpr uint16_t COLOR_TEXT = 0xFFFF;
    static constexpr uint16_t COLOR_GREEN = 0x07E0;
    static constexpr uint16_t COLOR_RED = 0xF800;
    static constexpr uint16_t COLOR_BLUE = 0x1C9F;

    uint32_t m_lastUpdateMs;
    WiFiAudioServer::State m_lastState;

    void drawVolumeBar(uint8_t volume);
    void drawWiFiIcon(int x, int y, bool connected);
    void drawSpeakerIcon(int x, int y, bool active);
};

#endif
```

- [ ] **Step 2: 创建 BTSpeakerUI.cpp 实现文件**

```cpp
#include "BTSpeakerUI.h"

BTSpeakerUI::BTSpeakerUI()
    : m_lastUpdateMs(0)
    , m_lastState(WiFiAudioServer::State::IDLE) {
}

BTSpeakerUI::~BTSpeakerUI() {
}

bool BTSpeakerUI::init() {
    M5.Lcd.fillScreen(COLOR_BG);
    M5.Lcd.setTextColor(COLOR_TEXT, COLOR_BG);
    return true;
}

void BTSpeakerUI::update(WiFiAudioServer::State state, uint8_t volume, uint32_t bytesReceived) {
    uint32_t now = millis();
    if (now - m_lastUpdateMs < 500 && state == m_lastState) {
        return;
    }
    m_lastUpdateMs = now;
    m_lastState = state;

    switch (state) {
        case WiFiAudioServer::State::IDLE:
            drawReadyScreen("");
            break;
        case WiFiAudioServer::State::CONNECTING:
            drawConnectingScreen("");
            break;
        case WiFiAudioServer::State::STREAMING:
            drawStreamingScreen(bytesReceived);
            break;
        case WiFiAudioServer::State::ERROR:
            drawErrorScreen("Connection Error");
            break;
    }

    drawVolumeBar(volume);
}

void BTSpeakerUI::drawStartupScreen() {
    M5.Lcd.fillScreen(COLOR_BG);
    M5.Lcd.setTextSize(3);
    M5.Lcd.setCursor(30, 80);
    M5.Lcd.setTextColor(COLOR_ACCENT, COLOR_BG);
    M5.Lcd.print("M5 CoreS3");
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(50, 130);
    M5.Lcd.setTextColor(COLOR_TEXT, COLOR_BG);
    M5.Lcd.print("WiFi Speaker");
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(40, 180);
    M5.Lcd.print("Initializing...");
}

void BTSpeakerUI::drawConnectingScreen(const char* ssid) {
    M5.Lcd.fillScreen(COLOR_BG);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(30, 80);
    M5.Lcd.setTextColor(COLOR_ACCENT, COLOR_BG);
    M5.Lcd.print("Connecting WiFi");
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(30, 120);
    M5.Lcd.setTextColor(COLOR_TEXT, COLOR_BG);
    M5.Lcd.printf("SSID: %s", ssid);
    drawWiFiIcon(260, 20, false);
}

void BTSpeakerUI::drawReadyScreen(const char* ipAddress) {
    M5.Lcd.fillScreen(COLOR_BG);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(20, 40);
    M5.Lcd.setTextColor(COLOR_GREEN, COLOR_BG);
    M5.Lcd.print("Ready");
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(20, 80);
    M5.Lcd.setTextColor(COLOR_TEXT, COLOR_BG);
    M5.Lcd.printf("IP: %s", ipAddress);
    M5.Lcd.setCursor(20, 110);
    M5.Lcd.print("Open browser to play audio");
    drawWiFiIcon(260, 20, true);
    drawSpeakerIcon(220, 20, false);
}

void BTSpeakerUI::drawStreamingScreen(uint32_t bytesReceived) {
    M5.Lcd.fillScreen(COLOR_BG);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(20, 40);
    M5.Lcd.setTextColor(COLOR_BLUE, COLOR_BG);
    M5.Lcd.print("Streaming");
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(20, 80);
    M5.Lcd.setTextColor(COLOR_TEXT, COLOR_BG);
    M5.Lcd.printf("Received: %u KB", bytesReceived / 1024);
    drawSpeakerIcon(220, 20, true);
    drawWiFiIcon(260, 20, true);
}

void BTSpeakerUI::drawErrorScreen(const char* message) {
    M5.Lcd.fillScreen(COLOR_BG);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(20, 80);
    M5.Lcd.setTextColor(COLOR_RED, COLOR_BG);
    M5.Lcd.print("Error");
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(20, 120);
    M5.Lcd.setTextColor(COLOR_TEXT, COLOR_BG);
    M5.Lcd.print(message);
}

void BTSpeakerUI::drawVolumeBar(uint8_t volume) {
    int barWidth = 200;
    int barHeight = 16;
    int x = 60;
    int y = 200;

    M5.Lcd.fillRect(x, y, barWidth, barHeight, 0x4208);
    int fillWidth = (barWidth * volume) / 100;
    M5.Lcd.fillRect(x, y, fillWidth, barHeight, COLOR_ACCENT);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(10, y + 2);
    M5.Lcd.setTextColor(COLOR_TEXT, COLOR_BG);
    M5.Lcd.printf("V:%d", volume);
}

void BTSpeakerUI::drawWiFiIcon(int x, int y, bool connected) {
    uint16_t color = connected ? COLOR_GREEN : COLOR_RED;
    M5.Lcd.fillCircle(x, y, 8, color);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(x - 3, y - 4);
    M5.Lcd.setTextColor(0x0000, color);
    M5.Lcd.print("W");
}

void BTSpeakerUI::drawSpeakerIcon(int x, int y, bool active) {
    uint16_t color = active ? COLOR_BLUE : 0x4208;
    M5.Lcd.fillCircle(x, y, 8, color);
    M5.Lcd.setTextSize(1);
    M5.Lcd.setCursor(x - 3, y - 4);
    M5.Lcd.setTextColor(0x0000, color);
    M5.Lcd.print("S");
}
```

---

### Task 4: 修改 main.cpp 整合音频功能

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: 重写 main.cpp 整合 Wi-Fi 音频功能**

```cpp
#include <Arduino.h>
#include <M5CoreS3.h>

#include "modules/AudioOutput.h"
#include "modules/WiFiAudioServer.h"
#include "modules/BTSpeakerUI.h"

namespace Config {
    static constexpr const char* WIFI_SSID = "YOUR_SSID";
    static constexpr const char* WIFI_PASSWORD = "YOUR_PASSWORD";
    static constexpr uint32_t UI_UPDATE_INTERVAL_MS = 500;
}

AudioOutput g_audioOutput;
WiFiAudioServer g_wifiServer(g_audioOutput);
BTSpeakerUI g_ui;

uint32_t g_lastUiUpdateMs = 0;

void setup() {
    auto cfg = M5.config();
    CoreS3.begin(cfg);

    Serial.begin(115200);
    delay(300);
    Serial.println("M5 CoreS3 WiFi Speaker Starting...");

    g_ui.init();
    g_ui.drawStartupScreen();

    if (!g_audioOutput.init()) {
        g_ui.drawErrorScreen("Audio init failed");
        Serial.println("Audio init failed!");
        while (true) { delay(1000); }
    }
    Serial.println("Audio output initialized");

    g_ui.drawConnectingScreen(Config::WIFI_SSID);
    if (!g_wifiServer.init(Config::WIFI_SSID, Config::WIFI_PASSWORD)) {
        g_ui.drawErrorScreen("WiFi connect failed");
        Serial.println("WiFi connect failed!");
        while (true) { delay(1000); }
    }

    g_wifiServer.start();
    g_ui.drawReadyScreen(g_wifiServer.getIPAddress());
    Serial.printf("WiFi Speaker ready! IP: %s\n", g_wifiServer.getIPAddress());
    Serial.printf("Open http://%s/ in browser\n", g_wifiServer.getIPAddress());
}

void loop() {
    M5.update();
    g_wifiServer.update();

    uint32_t now = millis();
    if (now - g_lastUiUpdateMs >= Config::UI_UPDATE_INTERVAL_MS) {
        g_lastUiUpdateMs = now;
        g_ui.update(
            g_wifiServer.getState(),
            g_audioOutput.getVolume(),
            g_wifiServer.getBytesReceived()
        );
    }

    if (M5.BtnA.wasPressed()) {
        static bool muted = false;
        muted = !muted;
        if (muted) {
            g_audioOutput.mute();
        } else {
            g_audioOutput.unmute();
        }
    }

    if (M5.BtnB.wasPressed()) {
        uint8_t vol = g_audioOutput.getVolume();
        vol = (vol >= 100) ? 0 : vol + 20;
        g_audioOutput.setVolume(vol);
    }

    if (M5.BtnC.wasPressed()) {
        uint8_t vol = g_audioOutput.getVolume();
        vol = (vol < 20) ? 0 : vol - 20;
        g_audioOutput.setVolume(vol);
    }
}
```

- [ ] **Step 2: 更新 platformio.ini 添加依赖**

在 `lib_deps` 中添加：
```
bblanchon/ArduinoJson@^7.0.0
mathieucarbou/ESPAsyncWebServer@^3.1.5
```

- [ ] **Step 3: 编译验证**

Run: `cd /Users/mac/Desktop/ent2 && pio run -e m5stack-cores3`
Expected: BUILD SUCCESS

- [ ] **Step 4: 上传到设备测试**

Run: `cd /Users/mac/Desktop/ent2 && pio run -e m5stack-cores3 -t upload`
Expected: 固件上传成功，设备显示 WiFi Speaker 界面

- [ ] **Step 5: Commit**

```bash
git add src/modules/AudioOutput.h src/modules/AudioOutput.cpp src/modules/WiFiAudioServer.h src/modules/WiFiAudioServer.cpp src/modules/BTSpeakerUI.h src/modules/BTSpeakerUI.cpp src/main.cpp platformio.ini
git commit -m "feat: add WiFi audio speaker functionality for M5 CoreS3"
```

---

## 第四部分：方案B - 外部蓝牙模块扩展

### 4.1 硬件方案

使用支持经典蓝牙的外部模块通过 UART 连接到 M5 CoreS3：

**推荐模块：**

| 模块 | 协议 | 接口 | 价格 | 备注 |
|------|------|------|------|------|
| BK3254 | A2DP/HFP/AVRCP | UART | ¥8-15 | 成本最低，功能完整 |
| BM83 | A2DP/HFP/AVRCP/BLE | UART/I2C | ¥20-35 | Microchip方案，稳定可靠 |
| JDY-31 | A2DP/AVRCP | UART | ¥10-20 | 国产方案，文档丰富 |
| CSR8311 | A2DP/HFP/AVRCP | UART/USB | ¥15-30 | CSR方案，音质好 |

### 4.2 连接方案（以 BK3254 为例）

```
M5 CoreS3              BK3254 蓝牙模块
─────────              ────────────────
G17 (PORT.C TX)  ───→  RX
G18 (PORT.C RX)  ←───  TX
5V               ───→  VCC
GND              ───→  GND
G0  (I2S_MCLK)  ───→  PCM_CLK (可选)
G13 (I2S_DOUT)  ←───  PCM_OUT (数字音频直出)
G34 (I2S_BCK)   ───→  PCM_SYNC (可选)
```

### 4.3 软件架构

```cpp
// BluetoothModule.h - 外部蓝牙模块驱动
#ifndef BLUETOOTH_MODULE_H
#define BLUETOOTH_MODULE_H

#include <Arduino.h>
#include <HardwareSerial.h>

class BluetoothModule {
public:
    static constexpr int UART_RX = 18;
    static constexpr int UART_TX = 17;
    static constexpr int UART_BAUD = 115200;

    enum class BTState {
        OFF,
        READY,
        PAIRED,
        STREAMING,
        INCOMING_CALL,
        ON_CALL
    };

    BluetoothModule();
    ~BluetoothModule();

    bool init();
    void update();
    BTState getState() const;
    String getDeviceName() const;
    String getConnectedDevice() const;

    void answerCall();
    void rejectCall();
    void hangupCall();
    void dialLastNumber();
    void setVolume(uint8_t volume);

    void registerAudioCallback(void (*callback)(const uint8_t*, size_t));

private:
    HardwareSerial m_serial;
    BTState m_state;
    String m_deviceName;
    String m_connectedDevice;
    void (*m_audioCallback)(const uint8_t*, size_t);

    void processUartData();
    void sendCommand(const char* cmd);
};

#endif
```

### 4.4 实现步骤

- [ ] **Step 1: 采购 BK3254 或 JDY-31 蓝牙模块**
- [ ] **Step 2: 通过杜邦线连接到 M5 CoreS3 的 PORT.C (G17/G18)**
- [ ] **Step 3: 实现 BluetoothModule 驱动**
- [ ] **Step 4: 整合到 main.cpp，将蓝牙模块接收的音频数据转发到 AW88298**
- [ ] **Step 5: 测试配对和音频播放**

---

## 第五部分：方案C - USB 音频 (UAC2)

### 5.1 原理

ESP32-S3 内置 USB OTG 控制器，理论上可以实现 USB Audio Class 2.0 (UAC2) 设备，使 M5 CoreS3 作为 USB 声卡被电脑识别。

### 5.2 限制

- **实验性功能**：ESP-IDF 的 TinyUSB UAC2 支持仍处于早期阶段
- **需要自定义 ESP-IDF 配置**：启用 `CFG_TUD_AUDIO=1`
- **与 Arduino 框架兼容性差**：需要从源码构建 Arduino 核心
- **参考项目**：[ESP32-USB-Audio](https://github.com/pschatzmann/ESP32-USB-Audio)（实验性）

### 5.3 可行性评估

| 评估项 | 评分 | 说明 |
|--------|------|------|
| 技术可行性 | ⭐⭐☆☆☆ | 需要大量底层修改 |
| 开发周期 | ⭐☆☆☆☆ | 需要1-2周深度开发 |
| 稳定性 | ⭐⭐☆☆☆ | 实验性质，不稳定 |
| 推荐程度 | ⭐☆☆☆☆ | 仅适合技术探索 |

---

## 第六部分：方案D - AirPlay 接收器

### 6.1 原理

利用 ESP32-S3 的 Wi-Fi 能力实现 AirPlay 协议接收器，苹果设备可直接投送音频。

### 6.2 参考项目

- [airplay-esp32s](https://github.com/emlynmac/airplay-esp32s) - ESP32 AirPlay 2 接收器
- [squeezelite-esp32](https://github.com/sle118/squeezelite-esp32) - 集成 AirPlay 的音频播放器

### 6.3 限制

- AirPlay 延迟较高（1-2秒）
- 仅支持苹果生态
- 需要较多 Flash 空间
- ESP-IDF 框架（非 Arduino）

---

## 第七部分：性能评估

### 7.1 方案A（Wi-Fi 音频流）性能预估

| 指标 | 预估值 | 说明 |
|------|--------|------|
| 音频采样率 | 44.1kHz / 16-bit | CD 品质 |
| 音频延迟 | 200-500ms | HTTP 传输延迟 |
| 音质 | 中等 | 受限于 1W 扬声器 |
| 连接距离 | 10-30m | 取决于 Wi-Fi 信号 |
| 功耗（播放） | 150-200mA | Wi-Fi + I2S 功放 |
| 功耗（待机） | 30-50mA | Wi-Fi 保持连接 |
| 电池续航 | 2.5-3.3小时 | 500mAh 电池 |
| 并发连接 | 1-2 | 受限于 ESP32-S3 处理能力 |

### 7.2 方案B（外部蓝牙模块）性能预估

| 指标 | 预估值 | 说明 |
|------|--------|------|
| 音频采样率 | 44.1kHz / 16-bit | A2DP SBC 编码 |
| 音频延迟 | 50-150ms | 蓝牙 A2DP 典型延迟 |
| 音质 | 中高 | SBC 编码，受限于扬声器 |
| 连接距离 | 8-10m | 蓝牙 Class 2 |
| 功耗（播放） | 120-180mA | 蓝牙 + I2S 功放 |
| 功耗（待机） | 15-25mA | 蓝牙待机 |
| 电池续航 | 2.8-4.2小时 | 500mAh 电池 |
| 并发连接 | 1 | A2DP 点对点 |

### 7.3 AW88298 功放音质分析

- **输出功率**：1W @ 8Ω
- **THD+N**：< 1% @ 1W
- **动态范围**：> 90dB
- **信噪比**：> 85dB
- **频率响应**：20Hz - 20kHz (±3dB)
- **内置 DSP**：支持 DRC（动态范围压缩）、EQ 等功能

---

## 第八部分：实施建议

### 推荐实施路径

```
阶段1: 方案A（Wi-Fi 音频流）     ← 立即可行，零额外成本
  ↓
阶段2: 方案B（外部蓝牙模块）     ← 需要额外硬件，但体验最接近传统蓝牙音箱
  ↓
阶段3: 方案D（AirPlay）          ← 可选，扩展苹果生态支持
```

### 关键注意事项

1. **I2S 引脚冲突**：AW88298 和 ES7210 共享 BCK/WS 引脚，同时使用麦克风和扬声器时需注意时钟配置
2. **AW9523B IO 扩展器**：AW88298 的 RST 和 INT 引脚通过 AW9523B 扩展，初始化时需先配置 AW9523B
3. **PSRAM 利用**：8MB PSRAM 可用于音频缓冲，减少 Wi-Fi 传输抖动导致的断音
4. **功耗管理**：AXP2101 电源管理芯片可动态调整供电，播放音频时需确保总线供电充足
5. **散热**：长时间高音量播放可能导致 ESP32-S3 发热，建议添加散热措施

---

## 附录：ESP32-S3 vs ESP32 蓝牙能力对比

| 特性 | ESP32 (原版) | ESP32-S3 |
|------|-------------|----------|
| 经典蓝牙 (BR/EDR) | ✅ 支持 | ❌ 不支持 |
| BLE | 4.2 | 5.0 |
| A2DP | ✅ | ❌ |
| HSP/HFP | ✅ | ❌ |
| AVRCP | ✅ | ❌ |
| SPP | ✅ | ❌ |
| BLE Audio | ❌ | ❌ (需 5.2+) |
| Wi-Fi 音频 | ✅ | ✅ |
| USB 音频 | ❌ | ⚠️ 实验性 |

**结论：若需经典蓝牙 A2DP 功能，必须使用原版 ESP32 或添加外部蓝牙模块。M5 CoreS3 (ESP32-S3) 本身无法直接实现传统蓝牙音箱功能。**
