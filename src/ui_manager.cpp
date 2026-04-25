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

size_t utf8CharLength(uint8_t leadByte) {
  if ((leadByte & 0x80U) == 0) {
    return 1;
  }
  if ((leadByte & 0xE0U) == 0xC0U) {
    return 2;
  }
  if ((leadByte & 0xF0U) == 0xE0U) {
    return 3;
  }
  if ((leadByte & 0xF8U) == 0xF0U) {
    return 4;
  }
  return 1;
}

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
  M5.Display.setFont(&fonts::efontCN_16);
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
    const int previousScrollOffset = scrollOffset_;
    scrollOffset_ -= touch.deltaY();
    scrollOffset_ = std::max(0, std::min(scrollOffset_, maxScroll_));
    autoScrollToBottom_ = false;
    if (scrollOffset_ != previousScrollOffset) {
      forceRedraw_ = true;
    }
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
    forceRedraw_ = true;
  }

  const String wifiLabel = appState_.getWifiLabel();
  const String errorLabel = appState_.getError();
  const ConversationState state = appState_.getConversationState();
  const bool listening = appState_.isListeningEnabled();

  if (!shouldRedraw(messages, state, wifiLabel, errorLabel, listening)) {
    return;
  }

  M5.Display.startWrite();
  M5.Display.fillScreen(kColorBg);
  drawStatusBar(state, wifiLabel, errorLabel);
  drawMessages(messages);
  drawButton();
  M5.Display.endWrite();
  commitDrawState(messages, state, wifiLabel, errorLabel, listening);
}

void UIManager::drawStatusBar(ConversationState state, const String& wifiLabel,
                              const String& errorLabel) {
  M5.Display.fillRect(0, 0, M5.Display.width(), layout_.statusBarHeight,
                      kColorPanel);
  M5.Display.setTextColor(kColorText, kColorPanel);
  M5.Display.setCursor(8, 6);
  M5.Display.printf("State: %s", conversationStateLabel(state));

  M5.Display.setCursor(180, 6);
  M5.Display.print(wifiLabel);

  if (!errorLabel.isEmpty()) {
    M5.Display.setTextColor(kColorError, kColorPanel);
    M5.Display.setCursor(8, 20);
    M5.Display.print(errorLabel);
  } else if (state == ConversationState::WaitingResponse) {
    M5.Display.setTextColor(kColorMuted, kColorPanel);
    M5.Display.setCursor(8, 20);
    M5.Display.print("Thinking...");
  }
}

void UIManager::drawMessages(const std::vector<ChatMessage>& messages) {
  const int left = 6;
  const int top = layout_.messageTop;
  const int width = M5.Display.width() - 12;
  const int height =
      M5.Display.height() - layout_.messageTop - layout_.buttonHeight - 10;
  const int lineHeight = M5.Display.fontHeight() + 2;
  const int textWidth = width - 24;

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
    const auto wrapped = wrapText(message.text, textWidth);
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
  forceRedraw_ = true;
}

bool UIManager::shouldRedraw(const std::vector<ChatMessage>& messages,
                             ConversationState state, const String& wifiLabel,
                             const String& errorLabel, bool listening) const {
  if (forceRedraw_) {
    return true;
  }
  if (state != lastRenderedState_) {
    return true;
  }
  if (wifiLabel != lastRenderedWifiLabel_) {
    return true;
  }
  if (errorLabel != lastRenderedErrorLabel_) {
    return true;
  }
  if (listening != lastRenderedListening_) {
    return true;
  }
  if (scrollOffset_ != lastRenderedScrollOffset_) {
    return true;
  }
  return !messagesEqual(messages, lastRenderedMessages_);
}

bool UIManager::messagesEqual(const std::vector<ChatMessage>& lhs,
                              const std::vector<ChatMessage>& rhs) const {
  if (lhs.size() != rhs.size()) {
    return false;
  }
  for (size_t i = 0; i < lhs.size(); ++i) {
    if (lhs[i].role != rhs[i].role || lhs[i].text != rhs[i].text ||
        lhs[i].timestamp != rhs[i].timestamp) {
      return false;
    }
  }
  return true;
}

void UIManager::commitDrawState(const std::vector<ChatMessage>& messages,
                                ConversationState state, const String& wifiLabel,
                                const String& errorLabel, bool listening) {
  lastRenderedMessages_ = messages;
  lastRenderedState_ = state;
  lastRenderedWifiLabel_ = wifiLabel;
  lastRenderedErrorLabel_ = errorLabel;
  lastRenderedListening_ = listening;
  lastRenderedScrollOffset_ = scrollOffset_;
  forceRedraw_ = false;
}

std::vector<String> UIManager::wrapText(const String& text, int maxWidth) const {
  std::vector<String> lines;
  String normalized = text;
  normalized.replace("\r", "");

  String currentLine;
  String currentToken;

  for (size_t i = 0; i < normalized.length();) {
    const char ch = normalized.charAt(i);
    if (ch == '\n') {
      if (!currentToken.isEmpty()) {
        const String candidate = currentLine + currentToken;
        if (!currentLine.isEmpty() && M5.Display.textWidth(candidate) > maxWidth) {
          lines.push_back(currentLine);
          currentLine = currentToken;
        } else {
          currentLine = candidate;
        }
        currentToken = "";
      }
      lines.push_back(currentLine);
      currentLine = "";
      ++i;
      continue;
    }

    const size_t charLen = utf8CharLength(static_cast<uint8_t>(ch));
    const String glyph = normalized.substring(i, i + charLen);
    i += charLen;

    if (ch == ' ') {
      currentToken += glyph;
      const String candidate = currentLine + currentToken;
      if (!currentLine.isEmpty() && M5.Display.textWidth(candidate) > maxWidth) {
        lines.push_back(currentLine);
        currentLine = currentToken;
      } else {
        currentLine = candidate;
      }
      currentToken = "";
      continue;
    }

    currentToken += glyph;
    const String candidate = currentLine + currentToken;
    if (M5.Display.textWidth(candidate) <= maxWidth) {
      continue;
    }

    if (!currentLine.isEmpty()) {
      lines.push_back(currentLine);
      currentLine = "";
      while (!currentToken.isEmpty() && M5.Display.textWidth(currentToken) > maxWidth) {
        size_t split = 0;
        String partial;
        while (split < currentToken.length()) {
          const size_t partLen =
              utf8CharLength(static_cast<uint8_t>(currentToken.charAt(split)));
          const String next = partial + currentToken.substring(split, split + partLen);
          if (!partial.isEmpty() && M5.Display.textWidth(next) > maxWidth) {
            break;
          }
          partial = next;
          split += partLen;
        }
        if (partial.isEmpty()) {
          partial = currentToken;
          split = currentToken.length();
        }
        lines.push_back(partial);
        currentToken = currentToken.substring(split);
      }
      continue;
    }

    size_t split = 0;
    String partial;
    while (split < currentToken.length()) {
      const size_t partLen =
          utf8CharLength(static_cast<uint8_t>(currentToken.charAt(split)));
      const String next = partial + currentToken.substring(split, split + partLen);
      if (!partial.isEmpty() && M5.Display.textWidth(next) > maxWidth) {
        break;
      }
      partial = next;
      split += partLen;
    }

    if (partial.isEmpty()) {
      partial = currentToken;
      split = currentToken.length();
    }

    lines.push_back(partial);
    currentToken = currentToken.substring(split);
  }

  if (!currentToken.isEmpty()) {
    const String candidate = currentLine + currentToken;
    if (!currentLine.isEmpty() && M5.Display.textWidth(candidate) > maxWidth) {
      lines.push_back(currentLine);
      currentLine = currentToken;
    } else {
      currentLine = candidate;
    }
  }

  if (!currentLine.isEmpty()) {
    lines.push_back(currentLine);
  }

  if (lines.empty()) {
    lines.push_back("");
  }
  return lines;
}

int UIManager::calculateContentHeight(
    const std::vector<ChatMessage>& messages) const {
  const int width = M5.Display.width() - 12;
  const int textWidth = width - 24;
  const int lineHeight = M5.Display.fontHeight() + 2;
  int total = 0;
  for (const auto& message : messages) {
    total += 18 + static_cast<int>(wrapText(message.text, textWidth).size()) * lineHeight + 6;
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
