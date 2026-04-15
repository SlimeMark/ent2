#pragma once

#include "app_state.h"

namespace app {

class AudioPlayback {
 public:
  explicit AudioPlayback(AppState& appState) : appState_(appState) {}

  void startTask();

 private:
  static void taskEntry(void* arg);
  void taskLoop();
  bool beginSpeaker();
  void endSpeaker();

  AppState& appState_;
};

}  // namespace app
