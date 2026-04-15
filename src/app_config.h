#pragma once

#include <Arduino.h>

namespace app {

struct AppConfig {
  static constexpr const char* WIFI_SSID = "SpeedyWing IoT";
  static constexpr const char* WIFI_PASSWORD = "1145141919810";
  static constexpr const char* SERVER_URL = "http://192.168.5.100:4000/chat";

  static constexpr size_t MAX_HISTORY_MESSAGES = 12;
  static constexpr uint32_t SILENCE_TIMEOUT_MS = 1200;
  static constexpr uint32_t MIN_RECORDING_MS = 350;
  static constexpr uint32_t WIFI_CONNECT_TIMEOUT_MS = 15000;
  static constexpr uint32_t WIFI_RETRY_INTERVAL_MS = 5000;
  static constexpr uint32_t HTTP_TIMEOUT_MS = 90000;

  static constexpr uint32_t AUDIO_SAMPLE_RATE = 16000;
  static constexpr uint8_t AUDIO_CHANNELS = 1;
  static constexpr size_t AUDIO_CHUNK_SAMPLES = 512;
  static constexpr size_t MAX_RECORDING_SECONDS = 45;
  static constexpr size_t MAX_RECORDING_SAMPLES =
      AUDIO_SAMPLE_RATE * MAX_RECORDING_SECONDS;

  static constexpr uint32_t AUDIO_LEVEL_THRESHOLD = 900;
  static constexpr uint8_t MIC_OVER_SAMPLING = 1;
  static constexpr uint8_t MIC_MAGNIFICATION = 24;
  static constexpr uint8_t MIC_NOISE_FILTER_LEVEL = 64;
  static constexpr uint8_t SPEAKER_VOLUME = 180;
  static constexpr uint32_t RMS_LOG_INTERVAL_MS = 1000;

  static constexpr uint32_t UI_REFRESH_MS = 33;
  static constexpr uint32_t CAPTURE_IDLE_DELAY_MS = 20;
  static constexpr uint32_t CAPTURE_ERROR_DELAY_MS = 250;
  static constexpr uint32_t PLAYBACK_POLL_MS = 20;
};

}  // namespace app
