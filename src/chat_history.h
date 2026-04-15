#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <deque>
#include <vector>

namespace app {

enum class MessageRole : uint8_t {
  User,
  Assistant,
  System,
};

struct ChatMessage {
  MessageRole role = MessageRole::System;
  String text;
  String timestamp;
};

class ChatHistory {
 public:
  explicit ChatHistory(size_t maxMessages) : maxMessages_(maxMessages) {}

  bool begin();
  void addMessage(MessageRole role, const String& text,
                  const String& timestamp = "");
  std::vector<ChatMessage> snapshot();
  size_t size();

 private:
  static String makeTimestamp();

  SemaphoreHandle_t mutex_ = nullptr;
  std::deque<ChatMessage> messages_;
  size_t maxMessages_ = 0;
};

}  // namespace app
