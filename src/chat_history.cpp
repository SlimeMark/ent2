#include "chat_history.h"

#include <inttypes.h>

namespace app {

bool ChatHistory::begin() {
  mutex_ = xSemaphoreCreateMutex();
  return mutex_ != nullptr;
}

void ChatHistory::addMessage(MessageRole role, const String& text,
                             const String& timestamp) {
  if (mutex_ == nullptr || text.isEmpty()) {
    return;
  }

  xSemaphoreTake(mutex_, portMAX_DELAY);
  ChatMessage message;
  message.role = role;
  message.text = text;
  message.timestamp = timestamp.isEmpty() ? makeTimestamp() : timestamp;
  messages_.push_back(message);
  while (messages_.size() > maxMessages_) {
    messages_.pop_front();
  }
  xSemaphoreGive(mutex_);
}

std::vector<ChatMessage> ChatHistory::snapshot() {
  std::vector<ChatMessage> copy;
  if (mutex_ == nullptr) {
    return copy;
  }

  xSemaphoreTake(mutex_, portMAX_DELAY);
  copy.assign(messages_.begin(), messages_.end());
  xSemaphoreGive(mutex_);
  return copy;
}

size_t ChatHistory::size() {
  if (mutex_ == nullptr) {
    return 0;
  }
  xSemaphoreTake(mutex_, portMAX_DELAY);
  const size_t count = messages_.size();
  xSemaphoreGive(mutex_);
  return count;
}

String ChatHistory::makeTimestamp() {
  const uint32_t ms = millis();
  const uint32_t totalSeconds = ms / 1000;
  const uint32_t minutes = totalSeconds / 60;
  const uint32_t seconds = totalSeconds % 60;
  char buffer[16];
  snprintf(buffer, sizeof(buffer), "%02" PRIu32 ":%02" PRIu32, minutes,
           seconds);
  return String(buffer);
}

}  // namespace app
