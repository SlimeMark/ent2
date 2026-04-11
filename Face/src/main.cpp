#include <Arduino.h>

#include <algorithm>
#include <list>
#include <memory>
#include <vector>

#include "M5CoreS3.h"
#include "esp_camera.h"
#include "human_face_detect_mnp01.hpp"
#include "human_face_detect_msr01.hpp"

namespace {

constexpr framesize_t kFrameSize = FRAMESIZE_QVGA;
constexpr uint32_t kStatusColor = TFT_WHITE;
constexpr uint32_t kBoxColor = TFT_GREEN;
constexpr uint32_t kErrorColor = TFT_RED;
constexpr uint32_t kOverlayBg = TFT_BLACK;
constexpr uint32_t kStatusHeight = 28;

struct DetectionFrame {
  camera_fb_t *fb = nullptr;
  std::vector<dl::detect::result_t> faces;
  bool frameReady = false;
  bool detectionOk = false;
};

bool g_cameraReady = false;
bool g_detectorReady = false;
uint32_t g_lastFpsTick = 0;
uint32_t g_frameCounter = 0;
uint32_t g_lastFps = 0;
std::unique_ptr<HumanFaceDetectMSR01> g_stage1;
std::unique_ptr<HumanFaceDetectMNP01> g_stage2;

int clampCoord(int value, int minValue, int maxValue) {
  return std::max(minValue, std::min(value, maxValue));
}

void cleanupFrame(DetectionFrame &frame) {
  if (frame.fb != nullptr) {
    CoreS3.Camera.free();
    frame.fb = nullptr;
  }
  frame.faces.clear();
  frame.frameReady = false;
  frame.detectionOk = false;
}

void updateFpsCounter() {
  ++g_frameCounter;
  const uint32_t now = millis();
  if (now - g_lastFpsTick >= 1000) {
    g_lastFps = g_frameCounter;
    g_frameCounter = 0;
    g_lastFpsTick = now;
  }
}

bool initDeviceAndCamera() {
  auto cfg = M5.config();
  CoreS3.begin(cfg);

  Serial.begin(115200);
  delay(100);
  Serial.println();
  Serial.println("CoreS3 face detect booting...");

  CoreS3.Display.setRotation(1);
  CoreS3.Display.fillScreen(TFT_BLACK);
  CoreS3.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  CoreS3.Display.setTextSize(2);
  CoreS3.Display.drawString("Initializing camera...", 8, 8);

  if (!CoreS3.Camera.begin()) {
    Serial.println("Camera init failed");
    CoreS3.Display.fillScreen(TFT_BLACK);
    CoreS3.Display.setTextColor(kErrorColor, TFT_BLACK);
    CoreS3.Display.drawString("Camera Init Fail", 8, 8);
    return false;
  }

  sensor_t *sensor = CoreS3.Camera.sensor;
  if (sensor == nullptr) {
    Serial.println("Camera sensor unavailable");
    CoreS3.Display.fillScreen(TFT_BLACK);
    CoreS3.Display.setTextColor(kErrorColor, TFT_BLACK);
    CoreS3.Display.drawString("Camera Sensor Fail", 8, 8);
    return false;
  }

  sensor->set_pixformat(sensor, PIXFORMAT_RGB565);
  sensor->set_framesize(sensor, kFrameSize);

  Serial.println("Camera init success");
  CoreS3.Display.fillScreen(TFT_BLACK);
  CoreS3.Display.setTextColor(kStatusColor, TFT_BLACK);
  CoreS3.Display.drawString("Camera OK", 8, 8);
  delay(500);
  g_lastFpsTick = millis();
  return true;
}

bool initFaceDetector() {
  g_stage1.reset(new HumanFaceDetectMSR01(0.1F, 0.5F, 10, 0.2F));
  g_stage2.reset(new HumanFaceDetectMNP01(0.5F, 0.3F, 5));
  return g_stage1 != nullptr && g_stage2 != nullptr;
}

DetectionFrame captureAndDetect() {
  DetectionFrame frame;

  if (!CoreS3.Camera.get()) {
    Serial.println("Camera capture failed");
    return frame;
  }

  frame.fb = CoreS3.Camera.fb;
  frame.frameReady = (frame.fb != nullptr);

  if (!frame.frameReady) {
    Serial.println("Camera framebuffer missing");
    return frame;
  }

  if (frame.fb->format != PIXFORMAT_RGB565) {
    Serial.printf("Unexpected pixel format: %d\n", frame.fb->format);
    return frame;
  }

  if (!g_detectorReady || !g_stage1 || !g_stage2) {
    Serial.println("Face detector not ready");
    return frame;
  }

  auto &candidates = g_stage1->infer(
      reinterpret_cast<uint16_t *>(frame.fb->buf),
      {static_cast<int>(frame.fb->height), static_cast<int>(frame.fb->width), 3});
  auto &results = g_stage2->infer(
      reinterpret_cast<uint16_t *>(frame.fb->buf),
      {static_cast<int>(frame.fb->height), static_cast<int>(frame.fb->width), 3},
      candidates);

  frame.faces.assign(results.begin(), results.end());
  frame.detectionOk = true;
  return frame;
}

void drawOverlayAndPresent(const DetectionFrame &frame) {
  if (!frame.frameReady || frame.fb == nullptr) {
    CoreS3.Display.fillScreen(TFT_BLACK);
    CoreS3.Display.setTextColor(kErrorColor, TFT_BLACK);
    CoreS3.Display.drawString("Capture Fail", 8, 8);
    return;
  }

  CoreS3.Display.pushImage(
      0, 0, frame.fb->width, frame.fb->height,
      reinterpret_cast<uint16_t *>(frame.fb->buf));

  for (const auto &face : frame.faces) {
    if (face.box.size() < 4) {
      continue;
    }

    const int x1 = clampCoord(face.box[0], 0, frame.fb->width - 1);
    const int y1 = clampCoord(face.box[1], 0, frame.fb->height - 1);
    const int x2 = clampCoord(face.box[2], 0, frame.fb->width - 1);
    const int y2 = clampCoord(face.box[3], 0, frame.fb->height - 1);
    const int w = std::max(1, x2 - x1 + 1);
    const int h = std::max(1, y2 - y1 + 1);

    CoreS3.Display.drawRect(x1, y1, w, h, kBoxColor);
  }

  CoreS3.Display.fillRect(0, 0, CoreS3.Display.width(), kStatusHeight, kOverlayBg);
  CoreS3.Display.setTextColor(kStatusColor, kOverlayBg);
  CoreS3.Display.setTextSize(2);
  CoreS3.Display.setCursor(6, 6);
  CoreS3.Display.printf("Camera OK  Face: %u  FPS: %lu",
                         static_cast<unsigned>(frame.faces.size()),
                         static_cast<unsigned long>(g_lastFps));
}

}  // namespace

void setup() {
  g_cameraReady = initDeviceAndCamera();
  if (g_cameraReady) {
    g_detectorReady = initFaceDetector();
    if (!g_detectorReady) {
      Serial.println("Face detector init failed");
      CoreS3.Display.fillScreen(TFT_BLACK);
      CoreS3.Display.setTextColor(kErrorColor, TFT_BLACK);
      CoreS3.Display.drawString("Detector Init Fail", 8, 8);
    }
  }
}

void loop() {
  CoreS3.update();

  if (!g_cameraReady || !g_detectorReady) {
    delay(250);
    return;
  }

  DetectionFrame frame = captureAndDetect();
  updateFpsCounter();
  drawOverlayAndPresent(frame);
  cleanupFrame(frame);
}
