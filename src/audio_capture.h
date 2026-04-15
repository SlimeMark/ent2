#pragma once

#include "app_state.h"

namespace app {

class AudioCapture {
 public:
  explicit AudioCapture(AppState& appState) : appState_(appState) {}

  void startTask();

 private:
  static void taskEntry(void* arg);
  void taskLoop();

  bool beginMic();
  void endMic();
  bool recordChunk(int16_t* chunk, size_t sampleCount);
  uint32_t computeRms(const int16_t* samples, size_t sampleCount) const;
  void finalizeRecording(size_t sampleCount, int16_t* workingBuffer);

  AppState& appState_;
};

}  // namespace app
