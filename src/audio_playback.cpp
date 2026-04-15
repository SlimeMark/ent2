#include "audio_playback.h"

#include "app_config.h"

#include <M5Unified.h>

namespace app {

void AudioPlayback::startTask() {
  xTaskCreatePinnedToCore(taskEntry, "playback_task", 6144, this, 2, nullptr, 0);
}

void AudioPlayback::taskEntry(void* arg) {
  static_cast<AudioPlayback*>(arg)->taskLoop();
}

void AudioPlayback::taskLoop() {
  while (true) {
    PlaybackItem* item = nullptr;
    if (xQueueReceive(appState_.playbackQueue(), &item, portMAX_DELAY) != pdTRUE) {
      continue;
    }
    if (item == nullptr) {
      continue;
    }

    appState_.setConversationState(ConversationState::Playing);
    if (!beginSpeaker()) {
      Serial.println("[PLAYBACK] speaker begin failed");
      appState_.setError("Speaker begin failed");
      freePlaybackItem(item);
      continue;
    }

    bool playOk = false;
    if (item->format.equalsIgnoreCase("wav")) {
      playOk = M5.Speaker.playWav(item->bytes, item->size, 1, 0, true);
    } else {
      // TODO: Add PCM/MP3 playback path if the server protocol changes.
      Serial.printf("[PLAYBACK] unsupported format=%s\n", item->format.c_str());
    }

    if (!playOk) {
      endSpeaker();
      freePlaybackItem(item);
      appState_.setError("TTS playback failed");
      continue;
    }

    while (M5.Speaker.isPlaying()) {
      if (appState_.consumePlaybackStopRequest()) {
        Serial.println("[PLAYBACK] playback stop requested");
        M5.Speaker.stop();
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(AppConfig::PLAYBACK_POLL_MS));
    }

    endSpeaker();
    freePlaybackItem(item);
    appState_.returnToStandbyState();
  }
}

bool AudioPlayback::beginSpeaker() {
  xSemaphoreTake(appState_.audioMutex(), portMAX_DELAY);
  while (M5.Mic.isRecording()) {
    xSemaphoreGive(appState_.audioMutex());
    vTaskDelay(pdMS_TO_TICKS(1));
    xSemaphoreTake(appState_.audioMutex(), portMAX_DELAY);
  }
  if (M5.Mic.isEnabled()) {
    M5.Mic.end();
  }
  M5.Speaker.setVolume(AppConfig::SPEAKER_VOLUME);
  const bool ok = M5.Speaker.begin();
  xSemaphoreGive(appState_.audioMutex());
  return ok;
}

void AudioPlayback::endSpeaker() {
  xSemaphoreTake(appState_.audioMutex(), portMAX_DELAY);
  if (M5.Speaker.isEnabled()) {
    M5.Speaker.end();
  }
  xSemaphoreGive(appState_.audioMutex());
}

}  // namespace app
