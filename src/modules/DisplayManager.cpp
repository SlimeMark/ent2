#include "DisplayManager.h"
#include <algorithm>

/**
 * @brief 构造函数
 */
DisplayManager::DisplayManager()
    : m_lastOverlayText(),
      m_lastFrameMs(0),
      m_headerDrawn(false) {
}

/**
 * @brief 析构函数
 */
DisplayManager::~DisplayManager() {
}

/**
 * @brief 初始化显示
 * @return 初始化是否成功
 */
bool DisplayManager::init() {
    M5.Lcd.fillScreen(BG_COLOR);
    ensureHeader();
    return true;
}

void DisplayManager::ensureHeader() {
    if (m_headerDrawn) {
        return;
    }

    M5.Lcd.fillRect(0, 0, M5.Lcd.width(), OVERLAY_HEIGHT, BG_COLOR);
    m_headerDrawn = true;
}

/**
 * @brief 显示状态信息
 * @param state 状态字符串
 * @param faceX 人脸 x 坐标
 * @param errorPx 误差值
 * @param faceWidth 人脸宽度
 * @param frameCounter 帧计数器
 */
void DisplayManager::drawStatus(const char* state, int faceX, int errorPx, int faceWidth, int frameCounter) {
    M5.Lcd.fillScreen(BG_COLOR);
    M5.Lcd.setTextColor(TEXT_COLOR, BG_COLOR);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(12, 12);
    M5.Lcd.println("CoreS3 Face Track");

    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(12, 52);
    M5.Lcd.printf("State: %s", state);

    M5.Lcd.setCursor(12, 84);
    if (faceX >= 0) {
        M5.Lcd.printf("Face X: %d", faceX);
    } else {
        M5.Lcd.println("Face X: none");
    }

    M5.Lcd.setCursor(12, 116);
    M5.Lcd.printf("Error: %d px", errorPx);

    M5.Lcd.setCursor(12, 148);
    M5.Lcd.printf("Deadzone: +/- 28");
    
    M5.Lcd.setCursor(12, 180);
    if (faceWidth > 0) {
        M5.Lcd.printf("Face Width: %d px", faceWidth);
    } else {
        M5.Lcd.println("Face Width: N/A");
    }

    M5.Lcd.setCursor(12, 212);
    M5.Lcd.printf("Frame: %d", frameCounter);
}

/**
 * @brief 显示摄像头图像和人脸检测框
 * @param fb 摄像头帧
 * @param faceInfo 人脸信息
 */
void DisplayManager::drawCameraAndFaces(camera_fb_t* fb, const FaceInfo& faceInfo) {
    if (!fb) {
        return;
    }

    ensureHeader();

    const int cropTop = std::min<int>(OVERLAY_HEIGHT, fb->height);
    const int visibleHeight = static_cast<int>(fb->height) - cropTop;
    if (visibleHeight <= 0) {
        return;
    }

    auto* image = reinterpret_cast<uint16_t*>(fb->buf);
    const int offset = cropTop * static_cast<int>(fb->width);
    M5.Lcd.pushImage(0, OVERLAY_HEIGHT, fb->width, visibleHeight, image + offset);

    const uint32_t now = millis();
    float fps = 0.0f;
    if (m_lastFrameMs != 0 && now > m_lastFrameMs) {
        fps = 1000.0f / static_cast<float>(now - m_lastFrameMs);
    }
    m_lastFrameMs = now;
    drawOverlay("", static_cast<int>(fps + 0.5f));

    // 绘制人脸检测框
    if (faceInfo.centerX >= 0 && faceInfo.width > 0 && faceInfo.height > 0) {
        int x1 = faceInfo.centerX - faceInfo.width / 2;
        int y1 = faceInfo.centerY - faceInfo.height / 2 - cropTop + static_cast<int>(OVERLAY_HEIGHT);
        int x2 = faceInfo.centerX + faceInfo.width / 2;
        int y2 = faceInfo.centerY + faceInfo.height / 2 - cropTop + static_cast<int>(OVERLAY_HEIGHT);

        // 确保坐标在屏幕范围内
        x1 = std::max(0, x1);
        y1 = std::max(static_cast<int>(OVERLAY_HEIGHT), y1);
        x2 = std::min(static_cast<int>(fb->width) - 1, x2);
        y2 = std::min(static_cast<int>(OVERLAY_HEIGHT) + visibleHeight - 1, y2);

        // 绘制矩形框
        if (x2 > x1 && y2 > y1) {
            M5.Lcd.drawRect(x1, y1, x2 - x1, y2 - y1, FACE_BOX_COLOR);
        }
    }
}

/**
 * @brief 显示覆盖信息
 * @param state 状态字符串
 * @param errorPx 误差值
 */
void DisplayManager::drawOverlay(const char* state, int errorPx) {
    ensureHeader();

    (void)state;

    String overlayText = "FPS: ";
    overlayText += errorPx;

    if (overlayText == m_lastOverlayText) {
        return;
    }

    M5.Lcd.fillRect(0, 0, M5.Lcd.width(), OVERLAY_HEIGHT, BG_COLOR);
    M5.Lcd.setTextColor(TEXT_COLOR, BG_COLOR);
    M5.Lcd.setTextSize(2);
    M5.Lcd.setCursor(12, 20);
    M5.Lcd.print(overlayText);
    m_lastOverlayText = overlayText;
}
