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

// 配置参数结构体
struct Config {
    // 伺服电机配置
    static constexpr int SERVO_LEFT_PIN = 8;   // DinBase PORT.B
    static constexpr int SERVO_RIGHT_PIN = 9;  // DinBase PORT.B
    static constexpr int SERVO_FREQ = 50;
    static constexpr int SERVO_MIN_US = 500;
    static constexpr int SERVO_MAX_US = 2500;
    static constexpr int SERVO_STOP_US = 1500;
    
    // 伺服电机控制参数
    static constexpr int SERVO_NUDGE_US = 70;
    static constexpr int SERVO_NUDGE_MS = 80;
    static constexpr int SERVO_SETTLE_MS = 120;
    
    // 摄像头配置
    static constexpr int FRAME_WIDTH = 320;
    static constexpr int FRAME_HEIGHT = 240;
    
    // 人脸检测配置
    static constexpr int FACE_DEADZONE_PX = 28;
    static constexpr uint32_t LOOP_INTERVAL_MS = 120;
    static constexpr int MAX_SKIP_FRAMES = 2;  // 最大跳过帧数
    
    // PID 控制参数
    static constexpr float PID_KP = 0.5f;      // 比例系数
    static constexpr float PID_KI = 0.1f;      // 积分系数
    static constexpr float PID_KD = 0.2f;      // 微分系数
};

Servo servoLeft;
Servo servoRight;
uint32_t g_lastLoopMs = 0;
int g_lastFaceX = -1;
bool g_detectorReady = false;
std::unique_ptr<HumanFaceDetectMSR01> g_stage1;
std::unique_ptr<HumanFaceDetectMNP01> g_stage2;

// 帧计数器，用于实现帧跳过策略
int g_frameCounter = 0;

// PID 控制变量
float g_pidError = 0;
float g_pidIntegral = 0;
float g_pidLastError = 0;

void writeBothMicroseconds(int leftUs, int rightUs) {
  servoLeft.writeMicroseconds(leftUs);
  servoRight.writeMicroseconds(rightUs);
}

void stopBoth() {
  writeBothMicroseconds(Config::SERVO_STOP_US, Config::SERVO_STOP_US);
}

void nudgeLeft() {
  // 两个舵机同向轻拨，模拟云台向左转一点
  writeBothMicroseconds(Config::SERVO_STOP_US - Config::SERVO_NUDGE_US,
                        Config::SERVO_STOP_US - Config::SERVO_NUDGE_US);
  delay(Config::SERVO_NUDGE_MS);
  stopBoth();
  delay(Config::SERVO_SETTLE_MS);
}

void nudgeRight() {
  writeBothMicroseconds(Config::SERVO_STOP_US + Config::SERVO_NUDGE_US,
                        Config::SERVO_STOP_US + Config::SERVO_NUDGE_US);
  delay(Config::SERVO_NUDGE_MS);
  stopBoth();
  delay(Config::SERVO_SETTLE_MS);
}

void drawStatus(const char* state, int faceX, int errorPx, int faceWidth) {
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
  M5.Display.printf("Deadzone: +/- %d", Config::FACE_DEADZONE_PX);
  
  M5.Display.setCursor(12, 180);
  if (faceWidth > 0) {
    M5.Display.printf("Face Width: %d px", faceWidth);
  } else {
    M5.Display.println("Face Width: N/A");
  }

  M5.Display.setCursor(12, 212);
  M5.Display.printf("Frame: %d", g_frameCounter);

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

// 人脸信息结构体
struct FaceInfo {
  int centerX;      // 人脸中心 x 坐标
  int centerY;      // 人脸中心 y 坐标
  int width;        // 人脸宽度
  int height;       // 人脸高度
  float confidence; // 置信度
};

// 实现真正的人脸检测，返回人脸信息
// 空结构体表示没检测到人脸
FaceInfo detectFace(camera_fb_t* fb) {
  FaceInfo faceInfo = { -1, -1, 0, 0, 0.0f };
  
  if (!g_detectorReady || !g_stage1 || !g_stage2 || !fb) {
    return faceInfo;
  }

  if (fb->format != PIXFORMAT_RGB565) {
    return faceInfo;
  }

  // 只在需要检测的帧进行处理
  if (g_frameCounter % (Config::MAX_SKIP_FRAMES + 1) != 0) {
    g_frameCounter++;
    // 如果上一帧检测到人脸，返回上一帧的位置
    if (g_lastFaceX >= 0) {
      faceInfo.centerX = g_lastFaceX;
    }
    return faceInfo;
  }
  g_frameCounter++;

  auto &candidates = g_stage1->infer(
      reinterpret_cast<uint16_t *>(fb->buf),
      {static_cast<int>(fb->height), static_cast<int>(fb->width), 3});
  auto &results = g_stage2->infer(
      reinterpret_cast<uint16_t *>(fb->buf),
      {static_cast<int>(fb->height), static_cast<int>(fb->width), 3},
      candidates);

  if (results.empty()) {
    g_lastFaceX = -1;
    return faceInfo;
  }

  // 选择最大的人脸
  size_t bestFaceIndex = 0;
  int maxArea = 0;
  
  for (size_t i = 0; i < results.size(); i++) {
    const auto &face = results[i];
    if (face.box.size() >= 4) {
      int width = face.box[2] - face.box[0];
      int height = face.box[3] - face.box[1];
      int area = width * height;
      if (area > maxArea) {
        maxArea = area;
        bestFaceIndex = i;
      }
    }
  }

  // 返回最大人脸的信息
  const auto &bestFace = results[bestFaceIndex];
  if (bestFace.box.size() >= 4) {
    faceInfo.centerX = (bestFace.box[0] + bestFace.box[2]) / 2;
    faceInfo.centerY = (bestFace.box[1] + bestFace.box[3]) / 2;
    faceInfo.width = bestFace.box[2] - bestFace.box[0];
    faceInfo.height = bestFace.box[3] - bestFace.box[1];
    faceInfo.confidence = bestFace.score;
    g_lastFaceX = faceInfo.centerX;
  }

  return faceInfo;
}

// 计算 PID 控制输出
int calculatePID(int error) {
  g_pidError = error;
  g_pidIntegral += g_pidError;
  float derivative = g_pidError - g_pidLastError;
  
  // 计算 PID 输出
  float output = Config::PID_KP * g_pidError + 
                Config::PID_KI * g_pidIntegral + 
                Config::PID_KD * derivative;
  
  // 更新上一次误差
  g_pidLastError = g_pidError;
  
  // 限制输出范围
  if (output > Config::SERVO_NUDGE_US * 2) {
    output = Config::SERVO_NUDGE_US * 2;
  } else if (output < -Config::SERVO_NUDGE_US * 2) {
    output = -Config::SERVO_NUDGE_US * 2;
  }
  
  return static_cast<int>(output);
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

  servoLeft.setPeriodHertz(Config::SERVO_FREQ);
  servoRight.setPeriodHertz(Config::SERVO_FREQ);
  servoLeft.attach(Config::SERVO_LEFT_PIN, Config::SERVO_MIN_US, Config::SERVO_MAX_US);
  servoRight.attach(Config::SERVO_RIGHT_PIN, Config::SERVO_MIN_US, Config::SERVO_MAX_US);
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

  drawStatus("Waiting", -1, 0, 0);
}

// 根据误差调整伺服电机
void adjustServo(int error) {
  if (abs(error) < Config::FACE_DEADZONE_PX) {
    stopBoth();
    return;
  }

  // 计算 PID 输出
  int pidOutput = calculatePID(error);
  
  // 根据 PID 输出调整伺服电机
  int leftUs = Config::SERVO_STOP_US - pidOutput;
  int rightUs = Config::SERVO_STOP_US - pidOutput;
  
  // 限制伺服电机信号范围
  if (leftUs < Config::SERVO_MIN_US) leftUs = Config::SERVO_MIN_US;
  if (leftUs > Config::SERVO_MAX_US) leftUs = Config::SERVO_MAX_US;
  if (rightUs < Config::SERVO_MIN_US) rightUs = Config::SERVO_MIN_US;
  if (rightUs > Config::SERVO_MAX_US) rightUs = Config::SERVO_MAX_US;
  
  // 应用伺服电机信号
  writeBothMicroseconds(leftUs, rightUs);
  delay(Config::SERVO_NUDGE_MS);
  stopBoth();
  delay(Config::SERVO_SETTLE_MS);
}

void loop() {
  M5.update();

  const uint32_t now = millis();
  if (now - g_lastLoopMs < Config::LOOP_INTERVAL_MS) {
    delay(5);
    return;
  }
  g_lastLoopMs = now;

  if (!CoreS3.Camera.get()) {
    drawStatus("No frame", -1, 0, 0);
    delay(10);
    return;
  }

  camera_fb_t* fb = CoreS3.Camera.fb;
  const FaceInfo faceInfo = detectFace(fb);
  CoreS3.Camera.free();

  if (faceInfo.centerX < 0) {
    stopBoth();
    drawStatus("No face", -1, 0, 0);
    return;
  }

  const int frameCenterX = Config::FRAME_WIDTH / 2;
  const int errorPx = faceInfo.centerX - frameCenterX;

  if (abs(errorPx) < Config::FACE_DEADZONE_PX) {
    stopBoth();
    drawStatus("Centered", faceInfo.centerX, errorPx, faceInfo.width);
  } else {
    adjustServo(errorPx);
    drawStatus("Adjusting", faceInfo.centerX, errorPx, faceInfo.width);
  }
}