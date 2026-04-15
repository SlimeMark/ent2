#include <Arduino.h>
#include <M5Unified.h>

#include "app_config.h"
#include "app_state.h"
#include "audio_capture.h"
#include "audio_playback.h"
#include "chat_history.h"
#include "network_client.h"
#include "ui_manager.h"

namespace {

app::AppState g_appState;
app::ChatHistory g_chatHistory(app::AppConfig::MAX_HISTORY_MESSAGES);
app::UIManager g_uiManager(g_appState, g_chatHistory);
app::AudioCapture g_audioCapture(g_appState);
app::NetworkClient g_networkClient(g_appState, g_chatHistory);
app::AudioPlayback g_audioPlayback(g_appState);

[[noreturn]] void haltWithMessage(const char* message) {
  Serial.println(message);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setRotation(1);
  M5.Display.setTextColor(TFT_RED, TFT_BLACK);
  M5.Display.setCursor(12, 20);
  M5.Display.print(message);
  while (true) {
    delay(1000);
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(300);

  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(1);
  M5.Display.fillScreen(TFT_BLACK);

  Serial.println("[BOOT] CoreS3 remote voice terminal starting");

  if (!g_appState.begin()) {
    haltWithMessage("AppState init failed");
  }
  if (!g_chatHistory.begin()) {
    haltWithMessage("ChatHistory init failed");
  }

  g_chatHistory.addMessage(
      app::MessageRole::System,
      "Ready. Tap Start, speak, wait for server reply, then CoreS3 plays TTS.");

  g_uiManager.startTask();
  g_audioCapture.startTask();
  g_networkClient.startTask();
  g_audioPlayback.startTask();
}

void loop() { vTaskDelay(portMAX_DELAY); }
