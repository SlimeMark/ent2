#include "network_client.h"

#include "app_config.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <mbedtls/base64.h>

#include <algorithm>
#include <cstring>

namespace app {

namespace {

uint8_t* allocBytes(size_t size) {
  auto* buffer = static_cast<uint8_t*>(
      heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (buffer == nullptr) {
    buffer = static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_8BIT));
  }
  return buffer;
}

const char* wifiStatusLabel(wl_status_t status) {
  switch (status) {
    case WL_IDLE_STATUS:
      return "idle";
    case WL_NO_SSID_AVAIL:
      return "no_ssid";
    case WL_SCAN_COMPLETED:
      return "scan_done";
    case WL_CONNECTED:
      return "connected";
    case WL_CONNECT_FAILED:
      return "auth_failed";
    case WL_CONNECTION_LOST:
      return "lost";
    case WL_DISCONNECTED:
      return "disconnected";
    default:
      return "unknown";
  }
}

}  // namespace

void NetworkClient::startTask() {
  xTaskCreatePinnedToCore(taskEntry, "network_task", 12288, this, 1, nullptr, 0);
}

void NetworkClient::taskEntry(void* arg) {
  static_cast<NetworkClient*>(arg)->taskLoop();
}

void NetworkClient::taskLoop() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.disconnect(false, true);
  refreshWifiLabel();

  while (true) {
    backgroundMaintainWifi();

    AudioUploadItem* item = nullptr;
    if (xQueueReceive(appState_.uploadQueue(), &item, pdMS_TO_TICKS(250)) != pdTRUE) {
      refreshWifiLabel();
      continue;
    }

    if (!uploadAudio(item)) {
      appState_.returnToStandbyState();
    }
  }
}

bool NetworkClient::ensureWifi() {
  if (!wifiCredentialsConfigured()) {
    appState_.setWifiStatus(false, "WiFi cfg missing");
    Serial.println("[NET] WiFi credentials not configured");
    return false;
  }

  if (WiFi.status() == WL_CONNECTED) {
    appState_.setWifiStatus(true, WiFi.localIP().toString());
    return true;
  }

  appState_.setWifiStatus(false, "WiFi connecting...");
  Serial.printf("[NET] connecting to WiFi SSID=%s\n", AppConfig::WIFI_SSID);
  WiFi.begin(AppConfig::WIFI_SSID, AppConfig::WIFI_PASSWORD);
  lastWifiAttemptMs_ = millis();

  const uint32_t startMs = millis();
  while (WiFi.status() != WL_CONNECTED &&
         millis() - startMs < AppConfig::WIFI_CONNECT_TIMEOUT_MS) {
    vTaskDelay(pdMS_TO_TICKS(250));
  }

  const bool ok = WiFi.status() == WL_CONNECTED;
  if (ok) {
    appState_.setWifiStatus(true, WiFi.localIP().toString());
    Serial.printf("[NET] WiFi connected ip=%s\n", WiFi.localIP().toString().c_str());
  } else {
    const String label = String("WiFi ") + wifiStatusLabel(WiFi.status());
    appState_.setWifiStatus(false, label);
    Serial.printf("[NET] WiFi failed status=%s\n", wifiStatusLabel(WiFi.status()));
  }
  return ok;
}

void NetworkClient::refreshWifiLabel() {
  if (!wifiCredentialsConfigured()) {
    appState_.setWifiStatus(false, "WiFi cfg missing");
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    appState_.setWifiStatus(true, WiFi.localIP().toString());
  } else {
    const String label = String("WiFi ") + wifiStatusLabel(WiFi.status());
    appState_.setWifiStatus(false, label);
  }
}

void NetworkClient::backgroundMaintainWifi() {
  if (!wifiCredentialsConfigured()) {
    return;
  }
  if (WiFi.status() == WL_CONNECTED) {
    return;
  }

  const uint32_t now = millis();
  if (lastWifiAttemptMs_ != 0 &&
      now - lastWifiAttemptMs_ < AppConfig::WIFI_RETRY_INTERVAL_MS) {
    return;
  }

  Serial.printf("[NET] background reconnect, status=%s\n",
                wifiStatusLabel(WiFi.status()));
  ensureWifi();
}

bool NetworkClient::uploadAudio(AudioUploadItem* item) {
  if (item == nullptr) {
    return false;
  }

  if (!ensureWifi()) {
    appState_.setError("WiFi unavailable");
    freeAudioUploadItem(item);
    return false;
  }

  String audioBase64;
  if (!base64Encode(reinterpret_cast<const uint8_t*>(item->samples),
                    item->sampleCount * sizeof(int16_t), audioBase64)) {
    appState_.setError("Base64 encode failed");
    freeAudioUploadItem(item);
    return false;
  }

  String requestBody = buildRequestBody(audioBase64, item->sampleRate);
  freeAudioUploadItem(item);

  HTTPClient http;
  http.setTimeout(
      static_cast<uint16_t>(std::min<uint32_t>(AppConfig::HTTP_TIMEOUT_MS, 65000)));
  if (!http.begin(AppConfig::SERVER_URL)) {
    appState_.setError("HTTP begin failed");
    return false;
  }

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");

  Serial.printf("[NET] POST %s\n", AppConfig::SERVER_URL);
  appState_.setConversationState(ConversationState::Uploading);
  const int httpCode = http.POST(requestBody);
  if (httpCode <= 0) {
    Serial.printf("[NET] POST failed code=%d\n", httpCode);
    http.end();
    appState_.setError("HTTP POST failed");
    return false;
  }

  appState_.setConversationState(ConversationState::WaitingResponse);
  const String payload = http.getString();
  Serial.printf("[NET] response code=%d bytes=%u\n", httpCode,
                static_cast<unsigned>(payload.length()));
  http.end();

  String asrText;
  String assistantText;
  PlaybackItem* playback = nullptr;
  if (!parseResponse(payload, asrText, assistantText, playback)) {
    appState_.setError("Response parse failed");
    return false;
  }

  if (!asrText.isEmpty()) {
    history_.addMessage(MessageRole::User, asrText);
  }
  if (!assistantText.isEmpty()) {
    history_.addMessage(MessageRole::Assistant, assistantText);
  }

  if (playback != nullptr && appState_.isListeningEnabled()) {
    if (xQueueSend(appState_.playbackQueue(), &playback, pdMS_TO_TICKS(1000)) != pdTRUE) {
      freePlaybackItem(playback);
      appState_.setError("Playback queue full");
      return false;
    }
    Serial.printf("[NET] queued playback format=%s size=%u\n",
                  playback->format.c_str(), static_cast<unsigned>(playback->size));
  } else if (playback != nullptr) {
    freePlaybackItem(playback);
    appState_.returnToStandbyState();
  } else {
    appState_.returnToStandbyState();
  }

  appState_.clearError();
  return true;
}

bool NetworkClient::parseResponse(const String& payload, String& asrText,
                                  String& assistantText,
                                  PlaybackItem*& playback) {
  playback = nullptr;
  auto* buffer = reinterpret_cast<char*>(allocBytes(payload.length() + 1));
  if (buffer == nullptr) {
    return false;
  }
  memcpy(buffer, payload.c_str(), payload.length() + 1);

  DynamicJsonDocument doc(8192);
  const auto error = deserializeJson(doc, buffer);
  if (error) {
    heap_caps_free(buffer);
    Serial.printf("[NET] JSON parse error: %s\n", error.c_str());
    return false;
  }

  asrText = String(doc["asr_text"] | "");
  assistantText = String(doc["assistant_text"] | "");
  const String format = String(doc["tts_format"] | "");
  const char* audioBase64 = doc["tts_audio_base64"] | "";

  if (!format.isEmpty() && strlen(audioBase64) > 0) {
    playback = new PlaybackItem;
    playback->format = format;
    size_t outSize = 0;
    if (!base64Decode(audioBase64, playback->bytes, outSize)) {
      delete playback;
      playback = nullptr;
      heap_caps_free(buffer);
      return false;
    }
    playback->size = outSize;
  }

  heap_caps_free(buffer);
  return true;
}

bool NetworkClient::base64Encode(const uint8_t* data, size_t size, String& out) {
  size_t outLen = 0;
  if (mbedtls_base64_encode(nullptr, 0, &outLen, data, size) !=
      MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
    return false;
  }

  auto* buffer = static_cast<unsigned char*>(allocBytes(outLen + 1));
  if (buffer == nullptr) {
    return false;
  }

  if (mbedtls_base64_encode(buffer, outLen + 1, &outLen, data, size) != 0) {
    heap_caps_free(buffer);
    return false;
  }

  buffer[outLen] = '\0';
  out = String(reinterpret_cast<const char*>(buffer));
  heap_caps_free(buffer);
  return true;
}

bool NetworkClient::base64Decode(const char* input, uint8_t*& outData,
                                 size_t& outSize) {
  outData = nullptr;
  outSize = 0;

  if (mbedtls_base64_decode(nullptr, 0, &outSize,
                            reinterpret_cast<const unsigned char*>(input),
                            strlen(input)) != MBEDTLS_ERR_BASE64_BUFFER_TOO_SMALL) {
    return false;
  }

  outData = allocBytes(outSize);
  if (outData == nullptr) {
    return false;
  }

  if (mbedtls_base64_decode(outData, outSize, &outSize,
                            reinterpret_cast<const unsigned char*>(input),
                            strlen(input)) != 0) {
    heap_caps_free(outData);
    outData = nullptr;
    outSize = 0;
    return false;
  }

  return true;
}

bool NetworkClient::wifiCredentialsConfigured() const {
  return String(AppConfig::WIFI_SSID) != "YOUR_WIFI_SSID" &&
         String(AppConfig::WIFI_PASSWORD) != "YOUR_WIFI_PASSWORD";
}

String NetworkClient::buildRequestBody(const String& audioBase64,
                                       uint32_t sampleRate) const {
  String body;
  body.reserve(audioBase64.length() + 320);
  body += "{\"session_id\":\"";
  body += appState_.getSessionId();
  body += "\",\"max_history_messages\":";
  body += String(AppConfig::MAX_HISTORY_MESSAGES);
  body += ",\"audio_format\":\"pcm_s16le\",\"audio_sample_rate\":";
  body += String(sampleRate);
  body += ",\"audio_channels\":";
  body += String(AppConfig::AUDIO_CHANNELS);
  body += ",\"client_timestamp_ms\":";
  body += String(millis());
  body += ",\"audio_base64\":\"";
  body += audioBase64;
  body += "\"}";
  return body;
}

}  // namespace app
