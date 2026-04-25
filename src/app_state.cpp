#include "app_state.h"

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <inttypes.h>

namespace app {

namespace {

void freeHeapPtr(void* ptr) {
  if (ptr != nullptr) {
    heap_caps_free(ptr);
  }
}

String makeSessionId() {
  const uint64_t chipId = ESP.getEfuseMac();
  char buffer[24];
  snprintf(buffer, sizeof(buffer), "cores3-%08" PRIx32,
           static_cast<uint32_t>(chipId & 0xFFFFFFFFULL));
  return String(buffer);
}

}  // namespace

void freeAudioUploadItem(AudioUploadItem* item) {
  if (item == nullptr) {
    return;
  }
  freeHeapPtr(item->samples);
  delete item;
}

void freePlaybackItem(PlaybackItem* item) {
  if (item == nullptr) {
    return;
  }
  freeHeapPtr(item->bytes);
  delete item;
}

const char* conversationStateLabel(ConversationState state) {
  switch (state) {
    case ConversationState::Idle:
      return "Idle";
    case ConversationState::Armed:
      return "Listening";
    case ConversationState::Recording:
      return "Recording";
    case ConversationState::Uploading:
      return "Recognizing";
    case ConversationState::WaitingResponse:
      return "Thinking";
    case ConversationState::Playing:
      return "Speaking";
    case ConversationState::Error:
      return "Error";
  }
  return "Unknown";
}

bool AppState::begin() {
  stateMutex_ = xSemaphoreCreateMutex();
  audioMutex_ = xSemaphoreCreateMutex();
  eventGroup_ = xEventGroupCreate();
  uploadQueue_ = xQueueCreate(2, sizeof(AudioUploadItem*));
  playbackQueue_ = xQueueCreate(2, sizeof(PlaybackItem*));
  sessionId_ = makeSessionId();

  return stateMutex_ != nullptr && audioMutex_ != nullptr &&
         eventGroup_ != nullptr && uploadQueue_ != nullptr &&
         playbackQueue_ != nullptr;
}

void AppState::requestListening(bool enabled) {
  if (stateMutex_ == nullptr) {
    return;
  }
  xSemaphoreTake(stateMutex_, portMAX_DELAY);
  if (enabled) {
    error_ = "";
    if (state_ == ConversationState::Idle || state_ == ConversationState::Error) {
      state_ = ConversationState::Armed;
    }
    xEventGroupSetBits(eventGroup_, kEventListeningEnabled);
  } else {
    xEventGroupClearBits(eventGroup_, kEventListeningEnabled);
    xEventGroupSetBits(eventGroup_, kEventPlaybackStopRequested);
    if (state_ == ConversationState::Armed || state_ == ConversationState::Recording ||
        state_ == ConversationState::Playing) {
      state_ = ConversationState::Idle;
    }
  }
  xSemaphoreGive(stateMutex_);
}

bool AppState::isListeningEnabled() {
  return (xEventGroupGetBits(eventGroup_) & kEventListeningEnabled) != 0;
}

void AppState::returnToStandbyState() {
  setConversationState(isListeningEnabled() ? ConversationState::Armed
                                            : ConversationState::Idle);
}

void AppState::setConversationState(ConversationState state) {
  if (stateMutex_ == nullptr) {
    return;
  }
  xSemaphoreTake(stateMutex_, portMAX_DELAY);
  state_ = state;
  xSemaphoreGive(stateMutex_);
}

ConversationState AppState::getConversationState() {
  if (stateMutex_ == nullptr) {
    return ConversationState::Error;
  }
  xSemaphoreTake(stateMutex_, portMAX_DELAY);
  const ConversationState state = state_;
  xSemaphoreGive(stateMutex_);
  return state;
}

void AppState::setError(const String& error) {
  if (stateMutex_ == nullptr) {
    return;
  }
  xSemaphoreTake(stateMutex_, portMAX_DELAY);
  error_ = error;
  state_ = ConversationState::Error;
  xSemaphoreGive(stateMutex_);
}

void AppState::clearError() {
  if (stateMutex_ == nullptr) {
    return;
  }
  xSemaphoreTake(stateMutex_, portMAX_DELAY);
  error_ = "";
  xSemaphoreGive(stateMutex_);
}

String AppState::getError() {
  if (stateMutex_ == nullptr) {
    return "State mutex missing";
  }
  xSemaphoreTake(stateMutex_, portMAX_DELAY);
  const String error = error_;
  xSemaphoreGive(stateMutex_);
  return error;
}

void AppState::setWifiStatus(bool connected, const String& label) {
  if (stateMutex_ == nullptr) {
    return;
  }
  xSemaphoreTake(stateMutex_, portMAX_DELAY);
  wifiConnected_ = connected;
  wifiLabel_ = label;
  if (connected) {
    xEventGroupSetBits(eventGroup_, kEventWifiConnected);
  } else {
    xEventGroupClearBits(eventGroup_, kEventWifiConnected);
  }
  xSemaphoreGive(stateMutex_);
}

bool AppState::isWifiConnected() {
  return (xEventGroupGetBits(eventGroup_) & kEventWifiConnected) != 0;
}

String AppState::getWifiLabel() {
  if (stateMutex_ == nullptr) {
    return "WiFi unknown";
  }
  xSemaphoreTake(stateMutex_, portMAX_DELAY);
  const String label = wifiLabel_;
  xSemaphoreGive(stateMutex_);
  return label;
}

void AppState::requestPlaybackStop() {
  xEventGroupSetBits(eventGroup_, kEventPlaybackStopRequested);
}

bool AppState::consumePlaybackStopRequest() {
  const EventBits_t bits = xEventGroupGetBits(eventGroup_);
  if ((bits & kEventPlaybackStopRequested) == 0) {
    return false;
  }
  xEventGroupClearBits(eventGroup_, kEventPlaybackStopRequested);
  return true;
}

String AppState::getSessionId() { return sessionId_; }

}  // namespace app
