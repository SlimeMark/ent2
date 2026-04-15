#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/event_groups.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

namespace app {

enum class ConversationState : uint8_t {
  Idle,
  Armed,
  Recording,
  Uploading,
  WaitingResponse,
  Playing,
  Error,
};

struct AudioUploadItem {
  int16_t* samples = nullptr;
  size_t sampleCount = 0;
  uint32_t sampleRate = 0;
};

struct PlaybackItem {
  uint8_t* bytes = nullptr;
  size_t size = 0;
  String format;
};

void freeAudioUploadItem(AudioUploadItem* item);
void freePlaybackItem(PlaybackItem* item);
const char* conversationStateLabel(ConversationState state);

class AppState {
 public:
  static constexpr EventBits_t kEventListeningEnabled = BIT0;
  static constexpr EventBits_t kEventWifiConnected = BIT1;
  static constexpr EventBits_t kEventPlaybackStopRequested = BIT2;

  bool begin();

  void requestListening(bool enabled);
  bool isListeningEnabled();
  void returnToStandbyState();

  void setConversationState(ConversationState state);
  ConversationState getConversationState();

  void setError(const String& error);
  void clearError();
  String getError();

  void setWifiStatus(bool connected, const String& label);
  bool isWifiConnected();
  String getWifiLabel();

  void requestPlaybackStop();
  bool consumePlaybackStopRequest();

  String getSessionId();

  QueueHandle_t uploadQueue() const { return uploadQueue_; }
  QueueHandle_t playbackQueue() const { return playbackQueue_; }
  SemaphoreHandle_t audioMutex() const { return audioMutex_; }
  EventGroupHandle_t eventGroup() const { return eventGroup_; }

 private:
  SemaphoreHandle_t stateMutex_ = nullptr;
  SemaphoreHandle_t audioMutex_ = nullptr;
  EventGroupHandle_t eventGroup_ = nullptr;
  QueueHandle_t uploadQueue_ = nullptr;
  QueueHandle_t playbackQueue_ = nullptr;

  ConversationState state_ = ConversationState::Idle;
  bool wifiConnected_ = false;
  String wifiLabel_ = "WiFi idle";
  String error_;
  String sessionId_;
};

}  // namespace app
