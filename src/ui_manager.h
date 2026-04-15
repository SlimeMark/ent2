#pragma once

#include "app_state.h"
#include "chat_history.h"

#include <Arduino.h>
#include <vector>

namespace app {

class UIManager {
 public:
  UIManager(AppState& appState, ChatHistory& history)
      : appState_(appState), history_(history) {}

  void startTask();

 private:
  struct Layout {
    int statusBarHeight = 34;
    int messageTop = 36;
    int buttonHeight = 46;
    int buttonMargin = 8;
  };

  static void taskEntry(void* arg);
  void taskLoop();

  void handleTouch();
  void drawFrame();
  void drawStatusBar(const String& wifiLabel, const String& errorLabel);
  void drawMessages(const std::vector<ChatMessage>& messages);
  void drawButton();
  void toggleListening();

  std::vector<String> wrapText(const String& text, size_t maxChars) const;
  int calculateContentHeight(const std::vector<ChatMessage>& messages) const;
  bool inButtonArea(int x, int y) const;
  bool inMessageArea(int x, int y) const;
  uint16_t roleColor(MessageRole role) const;
  const char* roleLabel(MessageRole role) const;

  AppState& appState_;
  ChatHistory& history_;
  Layout layout_;
  int scrollOffset_ = 0;
  int maxScroll_ = 0;
  size_t lastMessageCount_ = 0;
  bool autoScrollToBottom_ = true;
};

}  // namespace app
