#include <Arduino.h>
#include <ESP32Servo.h>
#include <M5CoreS3.h>
#include <esp_camera.h>
#include <algorithm>
#include <list>
#include <memory>
#include <vector>

#include "human_face_detect_mnp01.hpp"
#include "human_face_detect_msr01.hpp"

namespace {

constexpr int SERVO_LEFT_PIN = 8;   // DinBase PORT.B
constexpr int SERVO_RIGHT_PIN = 9;  // DinBase PORT.B

constexpr int SERVO_FREQ = 50;
constexpr int SERVO_MIN_US = 500;
constexpr int SERVO_MAX_US = 2500;
constexpr int SERVO_STOP_US = 1500;

// 360° continuous servo 不能直接做角度闭环，
// 这里只能做“偏一点就短促拨一下”的粗略居中。
constexpr int SERVO_NUDGE_US = 70;
constexpr int SERVO_NUDGE_MS = 80;
constexpr int SERVO_SETTLE_MS = 120;

constexpr int FRAME_WIDTH = 320;
constexpr int FRAME_HEIGHT = 240;
constexpr int FACE_DEADZONE_PX = 28;
constexpr uint32_t LOOP_INTERVAL_MS = 120;

Servo servoLeft;
Servo servoRight;
uint32_t g_lastLoopMs = 0;
int g_lastFaceX = -1;
bool g_detectorReady = false;
std::unique_ptr<HumanFaceDetectMSR01> g_stage1;
std::unique_ptr<HumanFaceDetectMNP01> g_stage2;

void writeBothMicroseconds(int leftUs, int rightUs) {
  servoLeft.writeMicroseconds(leftUs);
  servoRight.writeMicroseconds(rightUs);
}

void stopBoth() {
  writeBothMicroseconds(SERVO_STOP_US, SERVO_STOP_US);
}

void nudgeLeft() {
  // 两个舵机同向轻拨，模拟云台向左转一点
  writeBothMicroseconds(SERVO_STOP_US - SERVO_NUDGE_US,
                        SERVO_STOP_US - SERVO_NUDGE_US);
  delay(SERVO_NUDGE_MS);
  stopBoth();
  delay(SERVO_SETTLE_MS);
}

void nudgeRight() {
  writeBothMicroseconds(SERVO_STOP_US + SERVO_NUDGE_US,
                        SERVO_STOP_US + SERVO_NUDGE_US);
  delay(SERVO_NUDGE_MS);
  stopBoth();
  delay(SERVO_SETTLE_MS);
}

void drawStatus(const char* state, int faceX, int errorPx) {
  M5.Display.fillScreen(BLACK);
  M5.Display.setTextColor(WHITE, BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(12, 12);
  M5.Display.println("CoreS3 Face Track");

  M5.Display.setTextSize(2);
  M5.Display.setCursor(12, 52);
  M5.Display.printf("State: %s", state);

  M5.Display.setCursor(12, 84);
  if (faceX >= 0) {
    M5.Display.printf("Face X: %d", faceX);
  } else {
    M5.Display.println("Face X: none");
  }

  M5.Display.setCursor(12, 116);
  M5.Display.printf("Error: %d px", errorPx);

  M5.Display.setCursor(12, 148);
  M5.Display.printf("Deadzone: +/- %d", FACE_DEADZONE_PX);


}

bool initCamera() {
  if (!CoreS3.Camera.begin()) {
    return false;
  }
  CoreS3.Camera.sensor->set_framesize(CoreS3.Camera.sensor, FRAMESIZE_QVGA);
  return true;
}

bool initFaceDetector() {
  g_stage1.reset(new HumanFaceDetectMSR01(0.1F, 0.5F, 10, 0.2F));
  g_stage2.reset(new HumanFaceDetectMNP01(0.5F, 0.3F, 5));
  return g_stage1 != nullptr && g_stage2 != nullptr;
}

// 实现真正的人脸检测，返回人脸中心 x 坐标
// -1 表示没检测到人脸
int detectFaceCenterX(camera_fb_t* fb) {
  if (!g_detectorReady || !g_stage1 || !g_stage2 || !fb) {
    return -1;
  }

  if (fb->format != PIXFORMAT_RGB565) {
    return -1;
  }

  auto &candidates = g_stage1->infer(
      reinterpret_cast<uint16_t *>(fb->buf),
      {static_cast<int>(fb->height), static_cast<int>(fb->width), 3});
  auto &results = g_stage2->infer(
      reinterpret_cast<uint16_t *>(fb->buf),
      {static_cast<int>(fb->height), static_cast<int>(fb->width), 3},
      candidates);

  if (results.empty()) {
    return -1;
  }

  // 返回第一个检测到的人脸中心 x 坐标
  const auto &face = results[0];
  if (face.box.size() < 4) {
    return -1;
  }
  return (face.box[0] + face.box[2]) / 2;
}

}  // namespace

void setup() {
  auto cfg = M5.config();
  CoreS3.begin(cfg);

  Serial.begin(115200);
  delay(300);
  Serial.println("CoreS3 camera face-centering scaffold start");

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  servoLeft.setPeriodHertz(SERVO_FREQ);
  servoRight.setPeriodHertz(SERVO_FREQ);
  servoLeft.attach(SERVO_LEFT_PIN, SERVO_MIN_US, SERVO_MAX_US);
  servoRight.attach(SERVO_RIGHT_PIN, SERVO_MIN_US, SERVO_MAX_US);
  stopBoth();

  CoreS3.Display.fillScreen(BLACK);
  CoreS3.Display.setTextColor(WHITE, BLACK);
  CoreS3.Display.setTextSize(2);
  CoreS3.Display.setCursor(16, 40);
  CoreS3.Display.println("Init camera...");

  if (!initCamera()) {
    CoreS3.Display.fillScreen(BLACK);
    CoreS3.Display.setCursor(16, 40);
    CoreS3.Display.println("Camera init failed");
    while (true) {
      delay(1000);
    }
  }

  CoreS3.Display.setCursor(16, 70);
  CoreS3.Display.println("Init face detector...");
  
  g_detectorReady = initFaceDetector();
  if (!g_detectorReady) {
    CoreS3.Display.fillScreen(BLACK);
    CoreS3.Display.setCursor(16, 40);
    CoreS3.Display.println("Face detector init failed");
    while (true) {
      delay(1000);
    }
  }

  drawStatus("Waiting", -1, 0);
}

void loop() {
  M5.update();

  const uint32_t now = millis();
  if (now - g_lastLoopMs < LOOP_INTERVAL_MS) {
    delay(5);
    return;
  }
  g_lastLoopMs = now;

  if (!CoreS3.Camera.get()) {
    drawStatus("No frame", -1, 0);
    delay(10);
    return;
  }

  camera_fb_t* fb = CoreS3.Camera.fb;
  const int faceX = detectFaceCenterX(fb);
  g_lastFaceX = faceX;
  CoreS3.Camera.free();

  if (faceX < 0) {
    stopBoth();
    drawStatus("No face", -1, 0);
    return;
  }

  const int frameCenterX = FRAME_WIDTH / 2;
  const int errorPx = faceX - frameCenterX;

  if (errorPx < -FACE_DEADZONE_PX) {
    drawStatus("Nudge Left", faceX, errorPx);
    nudgeLeft();
  } else if (errorPx > FACE_DEADZONE_PX) {
    drawStatus("Nudge Right", faceX, errorPx);
    nudgeRight();
  } else {
    stopBoth();
    drawStatus("Centered", faceX, errorPx);
  }
}