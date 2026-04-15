#pragma once

#include "app_state.h"
#include "chat_history.h"

namespace app {

class NetworkClient {
 public:
  NetworkClient(AppState& appState, ChatHistory& history)
      : appState_(appState), history_(history) {}

  void startTask();

 private:
  static void taskEntry(void* arg);
  void taskLoop();

  bool ensureWifi();
  void refreshWifiLabel();
  void backgroundMaintainWifi();
  bool uploadAudio(AudioUploadItem* item);
  bool parseResponse(const String& payload, String& asrText,
                     String& assistantText, PlaybackItem*& playback);
  bool base64Encode(const uint8_t* data, size_t size, String& out);
  bool base64Decode(const char* input, uint8_t*& outData, size_t& outSize);
  bool wifiCredentialsConfigured() const;
  String buildRequestBody(const String& audioBase64, uint32_t sampleRate) const;

  AppState& appState_;
  ChatHistory& history_;
  uint32_t lastWifiAttemptMs_ = 0;
};

}  // namespace app
