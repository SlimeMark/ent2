#include "audio_capture.h"

#include "app_config.h"

#include <M5Unified.h>
#include <esp_heap_caps.h>

#include <cmath>
#include <cstring>
#include <inttypes.h>

namespace app {

namespace {

int16_t* allocPcmSamples(size_t sampleCount) {
  const size_t bytes = sampleCount * sizeof(int16_t);
  auto* buffer = static_cast<int16_t*>(
      heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (buffer == nullptr) {
    buffer = static_cast<int16_t*>(heap_caps_malloc(bytes, MALLOC_CAP_8BIT));
  }
  return buffer;
}

}  // namespace

void AudioCapture::startTask() {
  xTaskCreatePinnedToCore(taskEntry, "capture_task", 8192, this, 2, nullptr, 0);
}

void AudioCapture::taskEntry(void* arg) {
  static_cast<AudioCapture*>(arg)->taskLoop();
}

void AudioCapture::taskLoop() {
  int16_t* workingBuffer = allocPcmSamples(AppConfig::MAX_RECORDING_SAMPLES);
  if (workingBuffer == nullptr) {
    appState_.setError("Capture alloc failed");
    Serial.println("[CAPTURE] failed to allocate recording buffer");
    vTaskDelete(nullptr);
    return;
  }

  int16_t chunk[AppConfig::AUDIO_CHUNK_SAMPLES] = {};
  bool micEnabled = false;
  size_t recordedSamples = 0;
  uint32_t lastVoiceMs = 0;
  uint32_t recordingStartedMs = 0;
  uint32_t lastRmsLogMs = 0;

  while (true) {
    const bool listeningEnabled = appState_.isListeningEnabled();
    const ConversationState state = appState_.getConversationState();

    if (!listeningEnabled ||
        state == ConversationState::Uploading ||
        state == ConversationState::WaitingResponse ||
        state == ConversationState::Playing) {
      if (micEnabled) {
        endMic();
        micEnabled = false;
      }
      recordedSamples = 0;
      lastVoiceMs = 0;
      recordingStartedMs = 0;
      if (!listeningEnabled &&
          (state == ConversationState::Armed || state == ConversationState::Recording)) {
        appState_.setConversationState(ConversationState::Idle);
      }
      vTaskDelay(pdMS_TO_TICKS(AppConfig::CAPTURE_IDLE_DELAY_MS));
      continue;
    }

    if (!micEnabled) {
      if (!beginMic()) {
        appState_.setError("Mic begin failed");
        vTaskDelay(pdMS_TO_TICKS(AppConfig::CAPTURE_ERROR_DELAY_MS));
        continue;
      }
      micEnabled = true;
      appState_.setConversationState(ConversationState::Armed);
      Serial.println("[CAPTURE] mic armed");
    }

    if (!recordChunk(chunk, AppConfig::AUDIO_CHUNK_SAMPLES)) {
      vTaskDelay(pdMS_TO_TICKS(5));
      continue;
    }

    const uint32_t rms = computeRms(chunk, AppConfig::AUDIO_CHUNK_SAMPLES);
    const bool voiceDetected = rms >= AppConfig::AUDIO_LEVEL_THRESHOLD;
    const uint32_t now = millis();

    if (appState_.getConversationState() == ConversationState::Armed) {
      if (now - lastRmsLogMs >= AppConfig::RMS_LOG_INTERVAL_MS) {
        Serial.printf("[CAPTURE] armed rms=%" PRIu32 " threshold=%" PRIu32 "\n", rms,
                      AppConfig::AUDIO_LEVEL_THRESHOLD);
        lastRmsLogMs = now;
      }
      if (voiceDetected) {
        memcpy(workingBuffer, chunk, sizeof(chunk));
        recordedSamples = AppConfig::AUDIO_CHUNK_SAMPLES;
        lastVoiceMs = now;
        recordingStartedMs = now;
        appState_.setConversationState(ConversationState::Recording);
        Serial.printf("[CAPTURE] voice start rms=%" PRIu32 " threshold=%" PRIu32
                      "\n",
                      rms, AppConfig::AUDIO_LEVEL_THRESHOLD);
      }
      continue;
    }

    if (recordedSamples + AppConfig::AUDIO_CHUNK_SAMPLES <=
        AppConfig::MAX_RECORDING_SAMPLES) {
      memcpy(workingBuffer + recordedSamples, chunk, sizeof(chunk));
      recordedSamples += AppConfig::AUDIO_CHUNK_SAMPLES;
    } else {
      Serial.println("[CAPTURE] max recording buffer reached");
      finalizeRecording(recordedSamples, workingBuffer);
      recordedSamples = 0;
      lastVoiceMs = 0;
      recordingStartedMs = 0;
      continue;
    }

    if (voiceDetected) {
      lastVoiceMs = now;
    } else if (now - lastVoiceMs >= AppConfig::SILENCE_TIMEOUT_MS) {
      const uint32_t recordingMs = now - recordingStartedMs;
      if (recordingMs >= AppConfig::MIN_RECORDING_MS) {
        Serial.printf(
            "[CAPTURE] utterance end duration=%" PRIu32
            "ms silence=%" PRIu32 "ms samples=%u\n",
            recordingMs, AppConfig::SILENCE_TIMEOUT_MS,
            static_cast<unsigned>(recordedSamples));
        finalizeRecording(recordedSamples, workingBuffer);
      } else {
        Serial.printf(
            "[CAPTURE] dropped short recording duration=%" PRIu32
            "ms min=%" PRIu32 "ms samples=%u\n",
            recordingMs, AppConfig::MIN_RECORDING_MS,
            static_cast<unsigned>(recordedSamples));
        appState_.setConversationState(ConversationState::Armed);
      }
      recordedSamples = 0;
      lastVoiceMs = 0;
      recordingStartedMs = 0;
    }
  }
}

bool AudioCapture::beginMic() {
  xSemaphoreTake(appState_.audioMutex(), portMAX_DELAY);
  if (M5.Speaker.isEnabled()) {
    M5.Speaker.end();
  }

  auto cfg = M5.Mic.config();
  cfg.sample_rate = AppConfig::AUDIO_SAMPLE_RATE;
  cfg.over_sampling = AppConfig::MIC_OVER_SAMPLING;
  cfg.magnification = AppConfig::MIC_MAGNIFICATION;
  cfg.noise_filter_level = AppConfig::MIC_NOISE_FILTER_LEVEL;
  M5.Mic.config(cfg);

  const bool ok = M5.Mic.begin();
  xSemaphoreGive(appState_.audioMutex());
  return ok;
}

void AudioCapture::endMic() {
  xSemaphoreTake(appState_.audioMutex(), portMAX_DELAY);
  if (M5.Mic.isEnabled()) {
    M5.Mic.end();
  }
  xSemaphoreGive(appState_.audioMutex());
}

bool AudioCapture::recordChunk(int16_t* chunk, size_t sampleCount) {
  bool ok = false;
  xSemaphoreTake(appState_.audioMutex(), portMAX_DELAY);
  if (M5.Mic.isEnabled()) {
    ok = M5.Mic.record(chunk, sampleCount, AppConfig::AUDIO_SAMPLE_RATE, false);
  }
  xSemaphoreGive(appState_.audioMutex());
  return ok;
}

uint32_t AudioCapture::computeRms(const int16_t* samples,
                                  size_t sampleCount) const {
  uint64_t sumSquares = 0;
  for (size_t i = 0; i < sampleCount; ++i) {
    const int32_t sample = samples[i];
    sumSquares += static_cast<uint64_t>(sample) * sample;
  }
  return static_cast<uint32_t>(sqrt(static_cast<float>(sumSquares) / sampleCount));
}

void AudioCapture::finalizeRecording(size_t sampleCount, int16_t* workingBuffer) {
  if (sampleCount == 0) {
    appState_.setConversationState(ConversationState::Armed);
    return;
  }

  const uint32_t durationMs =
      static_cast<uint32_t>((sampleCount * 1000ULL) / AppConfig::AUDIO_SAMPLE_RATE);
  const size_t pcmBytes = sampleCount * sizeof(int16_t);
  Serial.printf("[CAPTURE] finalize samples=%u duration=%" PRIu32 "ms pcm=%uB\n",
                static_cast<unsigned>(sampleCount), durationMs,
                static_cast<unsigned>(pcmBytes));

  auto* item = new AudioUploadItem;
  item->samples = allocPcmSamples(sampleCount);
  item->sampleCount = sampleCount;
  item->sampleRate = AppConfig::AUDIO_SAMPLE_RATE;
  if (item->samples == nullptr) {
    delete item;
    appState_.setError("Upload alloc failed");
    return;
  }
  memcpy(item->samples, workingBuffer, sampleCount * sizeof(int16_t));

  appState_.setConversationState(ConversationState::Uploading);
  if (xQueueSend(appState_.uploadQueue(), &item, pdMS_TO_TICKS(1000)) != pdTRUE) {
    Serial.println("[CAPTURE] upload queue full");
    freeAudioUploadItem(item);
    appState_.setError("Upload queue full");
    return;
  }
  Serial.printf("[CAPTURE] queued %u samples (%u ms) for upload\n",
                static_cast<unsigned>(sampleCount),
                static_cast<unsigned>(durationMs));
}

}  // namespace app
