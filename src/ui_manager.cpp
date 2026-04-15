#include "ui_manager.h"

#include "app_config.h"

#include <M5Unified.h>

#include <algorithm>

namespace app {

namespace {

constexpr uint16_t kColorBg = 0x1082;
constexpr uint16_t kColorPanel = 0x18C3;
constexpr uint16_t kColorText = TFT_WHITE;
constexpr uint16_t kColorMuted = 0xAD55;
constexpr uint16_t kColorError = TFT_RED;
constexpr uint16_t kColorButtonStart = 0x3E98;
constexpr uint16_t kColorButtonPause = 0xD145;

}  // namespace

void UIManager::startTask() {
  xTaskCreatePinnedToCore(taskEntry, "ui_task", 8192, this, 2, nullptr, 1);
}

void UIManager::taskEntry(void* arg) {
  static_cast<UIManager*>(arg)->taskLoop();
}

void UIManager::taskLoop() {
  M5.Display.setRotation(1);
  M5.Display.fillScreen(kColorBg);
  M5.Display.setTextFont(1);
  M5.Display.setTextSize(1);

  while (true) {
    M5.update();
    handleTouch();
    drawFrame();
    vTaskDelay(pdMS_TO_TICKS(AppConfig::UI_REFRESH_MS));
  }
}

void UIManager::handleTouch() {
  if (M5.Touch.getCount() == 0) {
    return;
  }

  const auto touch = M5.Touch.getDetail(0);

  if (touch.isDragging() && inMessageArea(touch.x, touch.y)) {
    scrollOffset_ -= touch.deltaY();
    scrollOffset_ = std::max(0, std::min(scrollOffset_, maxScroll_));
    autoScrollToBottom_ = false;
  }

  if (touch.wasClicked() && inButtonArea(touch.x, touch.y)) {
    toggleListening();
  }
}

void UIManager::drawFrame() {
  const auto messages = history_.snapshot();
  if (messages.size() != lastMessageCount_) {
    lastMessageCount_ = messages.size();
    autoScrollToBottom_ = true;
  }

  const String wifiLabel = appState_.getWifiLabel();
  const String errorLabel = appState_.getError();

  M5.Display.startWrite();
  M5.Display.fillScreen(kColorBg);
  drawStatusBar(wifiLabel, errorLabel);
  drawMessages(messages);
  drawButton();
  M5.Display.endWrite();
}

void UIManager::drawStatusBar(const String& wifiLabel, const String& errorLabel) {
  M5.Display.fillRect(0, 0, M5.Display.width(), layout_.statusBarHeight,
                      kColorPanel);
  M5.Display.setTextColor(kColorText, kColorPanel);
  M5.Display.setCursor(8, 6);
  M5.Display.printf("State: %s",
                    conversationStateLabel(appState_.getConversationState()));

  M5.Display.setCursor(180, 6);
  M5.Display.print(wifiLabel);

  if (!errorLabel.isEmpty()) {
    M5.Display.setTextColor(kColorError, kColorPanel);
    M5.Display.setCursor(8, 20);
    M5.Display.print(errorLabel);
  }
}

void UIManager::drawMessages(const std::vector<ChatMessage>& messages) {
  const int left = 6;
  const int top = layout_.messageTop;
  const int width = M5.Display.width() - 12;
  const int height =
      M5.Display.height() - layout_.messageTop - layout_.buttonHeight - 10;
  const int lineHeight = 12;
  const size_t wrapChars = 34;

  M5.Display.fillRoundRect(left, top, width, height, 6, kColorPanel);

  const int contentHeight = calculateContentHeight(messages);
  maxScroll_ = std::max(0, contentHeight - (height - 8));
  if (autoScrollToBottom_) {
    scrollOffset_ = maxScroll_;
    autoScrollToBottom_ = false;
  } else {
    scrollOffset_ = std::max(0, std::min(scrollOffset_, maxScroll_));
  }

  int y = top + 4 - scrollOffset_;
  for (const auto& message : messages) {
    const auto wrapped = wrapText(message.text, wrapChars);
    const int bubbleHeight = 18 + static_cast<int>(wrapped.size()) * lineHeight;
    if (y + bubbleHeight < top || y > top + height) {
      y += bubbleHeight + 6;
      continue;
    }

    M5.Display.fillRoundRect(left + 4, y, width - 8, bubbleHeight, 5,
                             roleColor(message.role));
    M5.Display.setTextColor(kColorText, roleColor(message.role));
    M5.Display.setCursor(left + 12, y + 4);
    M5.Display.printf("%s  %s", roleLabel(message.role),
                      message.timestamp.c_str());

    int textY = y + 18;
    for (const auto& line : wrapped) {
      M5.Display.setCursor(left + 12, textY);
      M5.Display.print(line);
      textY += lineHeight;
    }
    y += bubbleHeight + 6;
  }
}

void UIManager::drawButton() {
  const int margin = layout_.buttonMargin;
  const int height = layout_.buttonHeight;
  const int y = M5.Display.height() - height - margin;
  const int width = M5.Display.width() - margin * 2;

  const bool listening = appState_.isListeningEnabled();
  const uint16_t color = listening ? kColorButtonPause : kColorButtonStart;
  const char* label = listening ? "Pause" : "Start";

  M5.Display.fillRoundRect(margin, y, width, height, 8, color);
  M5.Display.setTextColor(kColorText, color);
  M5.Display.setCursor((M5.Display.width() / 2) - 16, y + 16);
  M5.Display.print(label);
}

void UIManager::toggleListening() {
  const bool enable = !appState_.isListeningEnabled();
  Serial.printf("[UI] toggle listening -> %s\n", enable ? "on" : "off");
  appState_.requestListening(enable);
  if (!enable) {
    scrollOffset_ = maxScroll_;
  }
}

std::vector<String> UIManager::wrapText(const String& text,
                                        size_t maxChars) const {
  std::vector<String> lines;
  String normalized = text;
  normalized.replace("\r", "");

  int start = 0;
  while (start < normalized.length()) {
    int newline = normalized.indexOf('\n', start);
    String part =
        newline >= 0 ? normalized.substring(start, newline) : normalized.substring(start);
    while (part.length() > static_cast<int>(maxChars)) {
      int split = static_cast<int>(maxChars);
      for (int i = split; i > 0; --i) {
        if (part.charAt(i) == ' ') {
          split = i;
          break;
        }
      }
      lines.push_back(part.substring(0, split));
      while (split < part.length() && part.charAt(split) == ' ') {
        ++split;
      }
      part = part.substring(split);
    }
    lines.push_back(part);
    if (newline < 0) {
      break;
    }
    start = newline + 1;
  }

  if (lines.empty()) {
    lines.push_back("");
  }
  return lines;
}

int UIManager::calculateContentHeight(
    const std::vector<ChatMessage>& messages) const {
  int total = 0;
  for (const auto& message : messages) {
    total += 18 + static_cast<int>(wrapText(message.text, 34).size()) * 12 + 6;
  }
  return total;
}

bool UIManager::inButtonArea(int x, int y) const {
  const int margin = layout_.buttonMargin;
  const int top = M5.Display.height() - layout_.buttonHeight - margin;
  return x >= margin && x <= M5.Display.width() - margin && y >= top &&
         y <= top + layout_.buttonHeight;
}

bool UIManager::inMessageArea(int x, int y) const {
  const int top = layout_.messageTop;
  const int bottom =
      M5.Display.height() - layout_.buttonHeight - layout_.buttonMargin - 4;
  return x >= 0 && x <= M5.Display.width() && y >= top && y <= bottom;
}

uint16_t UIManager::roleColor(MessageRole role) const {
  switch (role) {
    case MessageRole::User:
      return 0x3186;
    case MessageRole::Assistant:
      return 0x1451;
    case MessageRole::System:
      return 0x4A49;
  }
  return kColorMuted;
}

const char* UIManager::roleLabel(MessageRole role) const {
  switch (role) {
    case MessageRole::User:
      return "User";
    case MessageRole::Assistant:
      return "Assistant";
    case MessageRole::System:
      return "System";
  }
  return "Unknown";
}

}  // namespace app
